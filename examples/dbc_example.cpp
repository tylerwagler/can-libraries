/**
 * @file dbc_example.cpp
 * @brief Demonstrates Vector_DBC integration via CanLibraries::dbc
 *
 * Loads a DBC file passed on the command line, iterates its messages
 * and signals, and prints a summary. Uses Tobias Lorenz's Vector_DBC
 * library (vendored at third_party/Vector_DBC).
 *
 * @copyright Copyright (c) 2026 Elytron Defense
 * @license GPL-3.0-or-later
 */

#include <Vector/DBC.h>

#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <file.dbc>\n";
        return 1;
    }

    std::ifstream in(argv[1]);
    if (!in.is_open()) {
        std::cerr << "Failed to open " << argv[1] << "\n";
        return 1;
    }

    Vector::DBC::Network network;
    in >> network;

    std::cout << "Loaded " << argv[1] << "\n"
              << "  messages: " << network.messages.size() << "\n"
              << "  nodes:    " << network.nodes.size() << "\n\n";

    for (const auto& [id, msg] : network.messages) {
        std::cout << "BO_ " << id << " " << msg.name
                  << " (" << msg.size << " bytes, tx=" << msg.transmitter << ")\n";
        for (const auto& [sname, sig] : msg.signals) {
            std::cout << "    SG_ " << sname
                      << "  start=" << sig.startBit
                      << "  len=" << sig.bitSize
                      << "  factor=" << sig.factor
                      << "  offset=" << sig.offset
                      << "  unit=\"" << sig.unit << "\"\n";
        }
    }

    return 0;
}
