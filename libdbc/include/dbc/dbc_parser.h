/**
 * @file dbc_parser.h
 * @brief DBC (CAN Database) file parser
 *
 * Provides parsing and decoding capabilities for DBC files,
 * the standard format for describing CAN message and signal definitions.
 *
 * @copyright Copyright (c) 2026 Elytron Defense
 * @license MIT License
 */

#ifndef DBC_PARSER_H
#define DBC_PARSER_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>
#include <optional>

namespace dbc {

/**
 * @brief Signal definition from DBC file
 *
 * Represents a single signal within a CAN message, including
 * its position, length, encoding parameters, and metadata.
 */
struct Signal {
    std::string name;           ///< Signal name
    size_t start_bit;           ///< Starting bit position (LSB first for Intel)
    size_t length;              ///< Signal length in bits
    bool is_little_endian;      ///< true = Intel (little-endian), false = Motorola (big-endian)
    bool is_signed;             ///< true = signed, false = unsigned
    double factor;              ///< Scaling factor
    double offset;              ///< Offset value
    double min_value;           ///< Minimum physical value
    double max_value;           ///< Maximum physical value
    std::string unit;           ///< Unit string (e.g., "V", "°C")
    std::map<int, std::string> value_table;  ///< Value table for enumerated values
    bool is_multiplexer;        ///< true if this is a multiplexer signal
    int multiplexer_value;      ///< Multiplexer value (for multiplexed signals)
    std::string multiplexer_group; ///< Multiplexer group identifier

    Signal();
};

/**
 * @brief CAN message definition from DBC file
 *
 * Represents a complete CAN message including its ID, length,
 * and all contained signals.
 */
struct Message {
    uint32_t id;                    ///< CAN identifier
    std::string name;               ///< Message name
    size_t length;                  ///< Message length in bytes (DLC)
    std::string transmitter;        ///< Sending ECU/node name
    std::map<std::string, Signal> signals;  ///< Signal map by name
    std::vector<std::string> signal_order;  ///< Order of signals as defined in DBC

    Message();
    Message(uint32_t msg_id, const std::string& msg_name, size_t msg_length);

    /**
     * @brief Get signal by name
     * @param name Signal name
     * @return Pointer to signal or nullptr if not found
     */
    const Signal* getSignal(const std::string& name) const;

    /**
     * @brief Check if message contains a signal
     * @param name Signal name
     * @return true if signal exists
     */
    bool hasSignal(const std::string& name) const;
};

/**
 * @brief Decoded signal value
 *
 * Represents a decoded signal with its physical value and
 * optional human-readable name from value table.
 */
struct DecodedSignal {
    std::string name;           ///< Signal name
    double value;               ///< Physical value (after factor/offset)
    std::string value_name;     ///< Human-readable name from value table
    std::string unit;           ///< Unit string
    bool valid;                 ///< true if signal was successfully decoded

    DecodedSignal();
    DecodedSignal(const std::string& signal_name, double signal_value);
};

/**
 * @brief Decoded CAN message
 *
 * Represents a complete decoded CAN message with all
 * signal values extracted and converted to physical units.
 */
struct DecodedMessage {
    uint32_t id;                        ///< CAN identifier
    std::string name;                   ///< Message name
    std::vector<DecodedSignal> signals; ///< Decoded signals
    uint64_t timestamp_us;              ///< Timestamp in microseconds
    bool valid;                         ///< true if message was successfully decoded

    DecodedMessage();
    DecodedMessage(uint32_t msg_id, const std::string& msg_name);
};

/**
 * @brief DBC Parser class
 *
 * Parses DBC files and provides methods for decoding/encoding
 * CAN frames based on the message and signal definitions.
 *
 * @example
 * dbc::Parser parser;
 * parser.parseFile("SMCU_FES0.dbc");
 *
 * auto* msg = parser.getMessageById(0x123);
 * if (msg) {
 *     auto decoded = parser.decodeFrame(0x123, data, 8);
 *     for (const auto& sig : decoded.signals) {
 *         std::cout << sig.name << " = " << sig.value << " " << sig.unit << std::endl;
 *     }
 * }
 */
class Parser {
public:
    /**
     * @brief Constructor
     */
    Parser();

    /**
     * @brief Destructor
     */
    ~Parser();

    // Non-copyable
    Parser(const Parser&) = delete;
    Parser& operator=(const Parser&) = delete;

    // Movable
    Parser(Parser&&) noexcept;
    Parser& operator=(Parser&&) noexcept;

    /**
     * @brief Parse DBC file from disk
     * @param filepath Path to .dbc file
     * @return true if parsing successful
     */
    bool parseFile(const std::string& filepath);

    /**
     * @brief Parse DBC content from string
     * @param dbc_content Raw DBC file content
     * @return true if parsing successful
     */
    bool parseString(const std::string& dbc_content);

    /**
     * @brief Parse multiple DBC files
     * @param filepaths List of .dbc file paths
     * @return true if all files parsed successfully
     */
    bool parseMultiple(const std::vector<std::string>& filepaths);

    /**
     * @brief Get message by CAN identifier
     * @param id CAN identifier
     * @return Pointer to message or nullptr if not found
     */
    const Message* getMessageById(uint32_t id) const;

    /**
     * @brief Get message by name
     * @param name Message name
     * @return Pointer to message or nullptr if not found
     */
    const Message* getMessageByName(const std::string& name) const;

    /**
     * @brief Get all messages
     * @return Map of all messages by ID
     */
    const std::map<uint32_t, Message>& getAllMessages() const;

    /**
     * @brief Decode a CAN frame
     * @param id CAN identifier
     * @param data Pointer to data bytes
     * @param dlc Data length code
     * @param timestamp_us Optional timestamp
     * @return Decoded message with all signal values
     */
    DecodedMessage decodeFrame(uint32_t id, const uint8_t* data, size_t dlc,
                               uint64_t timestamp_us = 0) const;

    /**
     * @brief Encode signal values into a CAN frame
     * @param id CAN identifier
     * @param signal_values Map of signal names to values
     * @param data Output buffer for frame data
     * @param dlc Output data length code
     * @return true if encoding successful
     */
    bool encodeFrame(uint32_t id, const std::map<std::string, double>& signal_values,
                     uint8_t* data, size_t& dlc) const;

    /**
     * @brief Get human-readable name for a signal value
     * @param msg_id Message ID
     * @param signal_name Signal name
     * @param value Integer value to look up
     * @return Value table name or empty string if not found
     */
    std::string getSignalValueName(uint32_t msg_id, const std::string& signal_name,
                                   int value) const;

    /**
     * @brief Check if DBC is loaded
     * @return true if at least one DBC has been parsed
     */
    bool isLoaded() const;

    /**
     * @brief Clear all parsed data
     */
    void clear();

    /**
     * @brief Get list of all message names
     * @return Vector of message names
     */
    std::vector<std::string> getMessageNames() const;

    /**
     * @brief Get signal by message ID and name
     * @param msg_id Message ID
     * @param signal_name Signal name
     * @return Pointer to signal or nullptr if not found
     */
    const Signal* getSignal(uint32_t msg_id, const std::string& signal_name) const;

    /**
     * @brief Get all signals for a message
     * @param msg_id Message ID
     * @return Vector of all signals in the message
     */
    std::vector<Signal> getSignals(uint32_t msg_id) const;

private:
    std::map<uint32_t, Message> messages_;
    std::map<std::string, uint32_t> name_to_id_;
    bool is_loaded_;

    // Parsing helpers
    bool parseLine(const std::string& line);
    void parseMessageLine(const std::string& line);
    void parseSignalLine(const std::string& line);
    void parseValueTableLine(const std::string& line);
    void parseAttributeLine(const std::string& line);

    // Signal encoding/decoding
    double decodeSignalValue(const uint8_t* data, const Signal& signal) const;
    void encodeSignalValue(uint8_t* data, const Signal& signal, double value) const;
    uint64_t extractBits(const uint8_t* data, size_t start_bit, size_t length,
                         bool is_little_endian) const;
    void insertBits(uint8_t* data, uint64_t value, size_t start_bit, size_t length,
                    bool is_little_endian) const;

    // Utility
    std::string trim(const std::string& str) const;
    std::vector<std::string> tokenize(const std::string& str, char delimiter) const;
};

} // namespace dbc

#endif // DBC_PARSER_H
