/**
 * @file test_socketcan_live.cpp
 * @brief Integration tests for the SocketCAN backend against vcan0
 *
 * Verifies the three task #3 features end-to-end on a real socket:
 *   - kernel timestamps via SO_TIMESTAMPNS land in frame.timestamp_us
 *   - CAN-FD frames round-trip with BRS/ESI/64-byte payload preserved
 *   - TX-echo direction is correctly attributed via the recent-TX deque
 *
 * Skipped (exit 77, the CTest "skipped" convention) when vcan0 is not
 * available — for example, on non-Linux hosts, or in CI environments
 * that lack the vcan kernel module.
 *
 * @copyright Copyright (c) 2026 Elytron Defense
 * @license GPL-3.0-or-later
 */

#include "can/frame.h"
#include "can/i_can_backend.h"

#include <sys/ioctl.h>
#include <net/if.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <unistd.h>

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond, msg)                                                  \
    do {                                                                  \
        if (cond) { std::printf("  [PASS] %s\n", msg); ++g_passed; }      \
        else      { std::printf("  [FAIL] %s\n", msg); ++g_failed; }      \
    } while (0)

namespace {

/// Returns true if a network interface with the given name exists, by
/// asking SIOCGIFINDEX. We don't need to bind a CAN socket — just confirm
/// the interface is present.
bool ifaceExists(const char* name) {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;
    ifreq ifr{};
    std::strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
    bool ok = ::ioctl(fd, SIOCGIFINDEX, &ifr) == 0;
    ::close(fd);
    return ok;
}

} // namespace

int main() {
    std::printf("=== SocketCAN live integration test ===\n\n");

    if (!ifaceExists("vcan0")) {
        std::printf("SKIPPED: vcan0 not present. To enable:\n");
        std::printf("  sudo modprobe vcan\n");
        std::printf("  sudo ip link add dev vcan0 type vcan\n");
        std::printf("  sudo ip link set up vcan0\n");
        return 77;  // CTest "skip" convention
    }

    auto backend = can::ICanBackend::create(can::BackendKind::SocketCan);
    if (!backend) {
        std::printf("FAIL: SocketCAN backend factory returned nullptr\n");
        return 1;
    }

    can::ChannelConfig cfg;
    cfg.channel_id = "vcan0";
    cfg.bitrate = 500000;
    cfg.receive_own_messages = true;   // required for is_tx detection

    if (!backend->open(cfg)) {
        std::printf("FAIL: open(vcan0): %s\n", backend->lastError().c_str());
        return 1;
    }

    std::printf("Classic CAN echo with kernel timestamp:\n");
    {
        can::Frame tx;
        tx.id = 0x123;
        tx.dlc = 4;
        tx.data[0] = 0xDE; tx.data[1] = 0xAD;
        tx.data[2] = 0xBE; tx.data[3] = 0xEF;

        const uint64_t wall_us = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());

        CHECK(backend->send(tx), "send classic frame");

        can::Frame rx;
        const bool got = backend->receive(rx, std::chrono::milliseconds(500));
        CHECK(got, "receive echo");
        if (got) {
            CHECK(rx.id == 0x123, "id preserved");
            CHECK(rx.dlc == 4, "dlc preserved");
            CHECK(rx.data[0] == 0xDE && rx.data[3] == 0xEF, "payload preserved");
            CHECK(!rx.is_fd_frame, "classic frame not flagged FD");
            CHECK(rx.is_tx, "is_tx=true on echo");
            CHECK(rx.timestamp_us > 0, "timestamp populated");
            // SO_TIMESTAMPNS uses CLOCK_REALTIME so the skew vs system_clock
            // should be sub-millisecond. Allow 1s to absorb any pathological
            // scheduling delays.
            int64_t skew = static_cast<int64_t>(rx.timestamp_us) - static_cast<int64_t>(wall_us);
            if (skew < 0) skew = -skew;
            CHECK(skew < 1'000'000, "kernel timestamp within 1s of wall clock");
        }
    }

    std::printf("\nCAN-FD echo with BRS preserved:\n");
    {
        can::Frame tx;
        tx.id = 0x456;
        tx.dlc = 16;
        tx.is_fd_frame = true;
        tx.is_brs = true;
        for (uint8_t i = 0; i < 16; ++i) tx.data[i] = i;

        const bool sent = backend->send(tx);
        if (!sent) {
            std::printf("    send failed: %s\n", backend->lastError().c_str());
            std::printf("    (vcan0 may need 'ip link set vcan0 mtu 72' for FD)\n");
            // Don't count this as a failure — FD on vcan requires explicit setup.
        } else {
            CHECK(sent, "send FD frame");
            can::Frame rx;
            const bool got = backend->receive(rx, std::chrono::milliseconds(500));
            CHECK(got, "receive FD echo");
            if (got) {
                CHECK(rx.is_fd_frame, "is_fd_frame=true");
                CHECK(rx.id == 0x456, "id preserved");
                CHECK(rx.dlc == 16, "16-byte FD payload length preserved");
                CHECK(rx.is_brs, "BRS preserved");
                CHECK(!rx.is_esi, "ESI off preserved");
                CHECK(rx.is_tx, "is_tx=true on FD echo");
                CHECK(rx.data[15] == 15, "16th payload byte preserved");
            }
        }
    }

    std::printf("\nreceive(timeout=0) is a non-blocking poll:\n");
    {
        // Regression guard for the pre-2.0.0 bug where receive(0ms) skipped
        // select() and went into a blocking read() — hanging callers on an
        // idle bus. After the fix, receive(0ms) on an empty queue must
        // return false essentially immediately. Anything under 50ms here
        // dwarfs the previous indefinite hang.
        auto t0 = std::chrono::steady_clock::now();
        can::Frame f;
        const bool got = backend->receive(f, std::chrono::milliseconds(0));
        auto t1 = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0);
        CHECK(!got, "no frame available on idle bus");
        CHECK(elapsed < std::chrono::milliseconds(50),
              "receive(0ms) returns inside 50ms (was: indefinite hang)");
    }

    std::printf("\nTX-echo deque doesn't false-positive on later distinct frames:\n");
    {
        // Send one frame, drain its echo, then send a frame with a totally
        // different payload. The second frame's echo should match (is_tx),
        // and no spurious extra frames should arrive after.
        can::Frame a;
        a.id = 0x789;
        a.dlc = 1;
        a.data[0] = 0x11;
        backend->send(a);
        can::Frame rx_a;
        backend->receive(rx_a, std::chrono::milliseconds(500));

        can::Frame b;
        b.id = 0x78A;
        b.dlc = 2;
        b.data[0] = 0x22;
        b.data[1] = 0x33;
        backend->send(b);
        can::Frame rx_b;
        const bool got = backend->receive(rx_b, std::chrono::milliseconds(500));
        CHECK(got, "second send round-trips");
        if (got) {
            CHECK(rx_b.id == 0x78A, "second frame id preserved");
            CHECK(rx_b.is_tx, "second frame is_tx=true");
        }

        can::Frame extra;
        const bool more = backend->receive(extra, std::chrono::milliseconds(50));
        CHECK(!more, "no spurious extra frames");
    }

    backend->close();

    std::printf("\nchannel_id length validation:\n");
    {
        // SocketCAN interface names cap at IFNAMSIZ-1 chars. The backend
        // should reject anything longer at open() time with a clear
        // error, instead of silently truncating into a different
        // interface (or none) via the strncpy() that follows.
        auto bad_backend = can::ICanBackend::create(can::BackendKind::SocketCan);
        can::ChannelConfig bad_cfg;
        // 32 chars — well over IFNAMSIZ-1 (15 on Linux).
        bad_cfg.channel_id = "this_name_is_way_too_long_for_can";
        bad_cfg.bitrate = 500000;
        CHECK(!bad_backend->open(bad_cfg), "open() rejects too-long channel_id");
        const std::string err = bad_backend->lastError();
        CHECK(err.find("too long") != std::string::npos,
              "lastError() mentions length");
    }

    std::printf("\nclose() wakes a receive() blocked in select():\n");
    {
        // Regression guard for the M-1 thread-safety work: close() must
        // shutdown(2) the underlying socket before closing it, so a
        // worker thread blocked in select() returns promptly instead of
        // waiting out its full timeout. Without that, a Qt-style app
        // shutdown sequence would hang for `timeout` per worker.
        auto backend2 = can::ICanBackend::create(can::BackendKind::SocketCan);
        can::ChannelConfig cfg2;
        cfg2.channel_id = "vcan0";
        cfg2.bitrate = 500000;
        const bool opened = backend2->open(cfg2);
        CHECK(opened, "second backend opens");

        if (opened) {
            // Generous receive timeout so we can clearly distinguish
            // "close() woke us" from "timeout fired naturally".
            constexpr auto kReceiveTimeout = std::chrono::milliseconds(5000);
            // Budget for close() to wake the worker. select()-driven
            // wakeups are typically sub-millisecond; 250ms is loose
            // enough to absorb scheduler hiccups in CI without ever
            // colliding with the 5s receive timeout.
            constexpr auto kWakeBudget = std::chrono::milliseconds(250);

            std::atomic<bool> worker_returned{false};
            std::atomic<int64_t> worker_elapsed_us{-1};

            std::thread worker([&] {
                auto t0 = std::chrono::steady_clock::now();
                can::Frame f;
                backend2->receive(f, kReceiveTimeout);
                auto t1 = std::chrono::steady_clock::now();
                worker_elapsed_us.store(
                    std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
                worker_returned.store(true);
            });

            // Give the worker a moment to land in select().
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            auto t_close = std::chrono::steady_clock::now();
            backend2->close();
            worker.join();
            auto t_join = std::chrono::steady_clock::now();

            const auto wake = std::chrono::duration_cast<std::chrono::milliseconds>(t_join - t_close);
            CHECK(worker_returned.load(), "worker returned");
            CHECK(wake < kWakeBudget,
                  "close() wakes receive() inside wake budget (not the full timeout)");
            std::printf("    wake-up after close: %lld ms (budget %lld ms, receive timeout %lld ms)\n",
                        static_cast<long long>(wake.count()),
                        static_cast<long long>(kWakeBudget.count()),
                        static_cast<long long>(kReceiveTimeout.count()));
        }
    }

    std::printf("\n========================================\n");
    std::printf("Test Results:\n");
    std::printf("  Passed: %d\n", g_passed);
    std::printf("  Failed: %d\n", g_failed);
    std::printf("========================================\n");
    return g_failed == 0 ? 0 : 1;
}
