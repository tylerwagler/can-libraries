/**
 * @file dbc_parser.cpp
 * @brief DBC file parser implementation
 *
 * Implements parsing of DBC (CAN Database) files and provides
 * frame encoding/decoding capabilities.
 *
 * @copyright Copyright (c) 2026 Elytron Defense
 * @license MIT License
 */

#include "dbc/dbc_parser.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iostream>

namespace dbc {

// ============================================================================
// Signal Implementation
// ============================================================================

Signal::Signal()
    : start_bit(0)
    , length(8)
    , is_little_endian(true)
    , is_signed(false)
    , factor(1.0)
    , offset(0.0)
    , min_value(0.0)
    , max_value(0.0)
    , is_multiplexer(false)
    , multiplexer_value(-1)
{
}

// ============================================================================
// Message Implementation
// ============================================================================

Message::Message()
    : id(0)
    , length(8)
{
}

Message::Message(uint32_t msg_id, const std::string& msg_name, size_t msg_length)
    : id(msg_id)
    , name(msg_name)
    , length(msg_length)
{
}

const Signal* Message::getSignal(const std::string& name) const {
    auto it = signals.find(name);
    if (it != signals.end()) {
        return &(it->second);
    }
    return nullptr;
}

bool Message::hasSignal(const std::string& name) const {
    return signals.find(name) != signals.end();
}

// ============================================================================
// DecodedSignal Implementation
// ============================================================================

DecodedSignal::DecodedSignal()
    : value(0.0)
    , valid(false)
{
}

DecodedSignal::DecodedSignal(const std::string& signal_name, double signal_value)
    : name(signal_name)
    , value(signal_value)
    , valid(true)
{
}

// ============================================================================
// DecodedMessage Implementation
// ============================================================================

DecodedMessage::DecodedMessage()
    : id(0)
    , timestamp_us(0)
    , valid(false)
{
}

DecodedMessage::DecodedMessage(uint32_t msg_id, const std::string& msg_name)
    : id(msg_id)
    , name(msg_name)
    , timestamp_us(0)
    , valid(true)
{
}

// ============================================================================
// Parser Implementation
// ============================================================================

Parser::Parser()
    : is_loaded_(false)
{
}

Parser::~Parser() = default;

Parser::Parser(Parser&&) noexcept = default;
Parser& Parser::operator=(Parser&&) noexcept = default;

bool Parser::parseFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Failed to open DBC file: " << filepath << std::endl;
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return parseString(buffer.str());
}

bool Parser::parseString(const std::string& dbc_content) {
    std::istringstream stream(dbc_content);
    std::string line;

    while (std::getline(stream, line)) {
        // Remove comments
        size_t comment_pos = line.find("||");
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }

        // Trim and skip empty lines
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        parseLine(line);
    }

    return is_loaded_;
}

bool Parser::parseMultiple(const std::vector<std::string>& filepaths) {
    bool all_success = true;
    for (const auto& filepath : filepaths) {
        if (!parseFile(filepath)) {
            all_success = false;
        }
    }
    return all_success;
}

const Message* Parser::getMessageById(uint32_t id) const {
    auto it = messages_.find(id);
    if (it != messages_.end()) {
        return &(it->second);
    }
    return nullptr;
}

const Message* Parser::getMessageByName(const std::string& name) const {
    auto it = name_to_id_.find(name);
    if (it != name_to_id_.end()) {
        auto msg_it = messages_.find(it->second);
        if (msg_it != messages_.end()) {
            return &(msg_it->second);
        }
    }
    return nullptr;
}

const std::map<uint32_t, Message>& Parser::getAllMessages() const {
    return messages_;
}

DecodedMessage Parser::decodeFrame(uint32_t id, const uint8_t* data, size_t dlc,
                                   uint64_t timestamp_us) const {
    DecodedMessage decoded(id, "");
    decoded.timestamp_us = timestamp_us;

    const Message* msg = getMessageById(id);
    if (!msg) {
        decoded.valid = false;
        decoded.name = "Unknown";
        return decoded;
    }

    decoded.name = msg->name;

    for (const auto& [signal_name, signal] : msg->signals) {
        DecodedSignal decoded_signal;
        decoded_signal.name = signal_name;
        decoded_signal.unit = signal.unit;

        // Extract raw value from data
        uint64_t raw_value = extractBits(data, signal.start_bit, signal.length,
                                         signal.is_little_endian);

        // Apply signedness
        if (signal.is_signed && signal.length < 64) {
            uint64_t sign_bit = (1ULL << (signal.length - 1));
            if (raw_value & sign_bit) {
                // Negative value - sign extend
                raw_value |= (~0ULL << signal.length);
            }
        }

        // Apply factor and offset
        double physical_value = static_cast<double>(raw_value) * signal.factor + signal.offset;
        decoded_signal.value = physical_value;

        // Look up value table if applicable
        if (!signal.value_table.empty()) {
            auto it = signal.value_table.find(static_cast<int>(raw_value));
            if (it != signal.value_table.end()) {
                decoded_signal.value_name = it->second;
            }
        }

        decoded_signal.valid = true;
        decoded.signals.push_back(decoded_signal);
    }

    return decoded;
}

bool Parser::encodeFrame(uint32_t id, const std::map<std::string, double>& signal_values,
                         uint8_t* data, size_t& dlc) const {
    const Message* msg = getMessageById(id);
    if (!msg) {
        return false;
    }

    // Initialize data buffer
    std::memset(data, 0, msg->length);
    dlc = msg->length;

    for (const auto& [signal_name, value] : signal_values) {
        auto it = msg->signals.find(signal_name);
        if (it == msg->signals.end()) {
            continue; // Skip unknown signals
        }

        const Signal& signal = it->second;
        encodeSignalValue(data, signal, value);
    }

    return true;
}

std::string Parser::getSignalValueName(uint32_t msg_id, const std::string& signal_name,
                                       int value) const {
    const Message* msg = getMessageById(msg_id);
    if (!msg) {
        return "";
    }

    auto it = msg->signals.find(signal_name);
    if (it == msg->signals.end()) {
        return "";
    }

    const auto& value_table = it->second.value_table;
    auto vt_it = value_table.find(value);
    if (vt_it != value_table.end()) {
        return vt_it->second;
    }

    return "";
}

bool Parser::isLoaded() const {
    return is_loaded_;
}

void Parser::clear() {
    messages_.clear();
    name_to_id_.clear();
    is_loaded_ = false;
}

std::vector<std::string> Parser::getMessageNames() const {
    std::vector<std::string> names;
    names.reserve(messages_.size());
    for (const auto& [id, msg] : messages_) {
        names.push_back(msg.name);
    }
    return names;
}

const Signal* Parser::getSignal(uint32_t msg_id, const std::string& signal_name) const {
    const Message* msg = getMessageById(msg_id);
    if (!msg) {
        return nullptr;
    }
    return msg->getSignal(signal_name);
}

std::vector<Signal> Parser::getSignals(uint32_t msg_id) const {
    std::vector<Signal> result;
    const Message* msg = getMessageById(msg_id);
    if (msg) {
        for (const auto& [name, signal] : msg->signals) {
            result.push_back(signal);
        }
    }
    return result;
}

// ============================================================================
// Parsing Helpers
// ============================================================================

bool Parser::parseLine(const std::string& line) {
    if (line.empty()) {
        return false;
    }

    // Parse based on line prefix
    if (line.substr(0, 3) == "BO_") {
        parseMessageLine(line);
        return true;
    }

    if (line.substr(0, 3) == "SG_") {
        parseSignalLine(line);
        return true;
    }

    if (line.substr(0, 3) == "VAL_") {
        parseValueTableLine(line);
        return true;
    }

    // Ignore other line types for now (attributes, environment variables, etc.)
    return false;
}

void Parser::parseMessageLine(const std::string& line) {
    // Format: BO_ ID MessageName: Length Transmitter
    // Example: BO_ 256 BCU_00_Cmd: 8 BCU
    // Example: BO_ 512 SMCU_00_DATA1: 8 SMCU_00

    std::vector<std::string> tokens = tokenize(line, ' ');
    if (tokens.size() < 5) {
        return;
    }

    // Parse message ID - tokens[1] is the ID
    uint32_t msg_id = 0;
    try {
        std::string id_str = tokens[1];
        msg_id = std::stoul(id_str, nullptr, 10); // Decimal ID
    } catch (...) {
        return;
    }

    // Parse message name - tokens[2] is the name (may have trailing colon)
    std::string msg_name = tokens[2];
    if (!msg_name.empty() && msg_name.back() == ':') {
        msg_name.pop_back();
    }

    // Parse length
    size_t length = 8;
    try {
        length = std::stoul(tokens[3]);
    } catch (...) {
        length = 8;
    }

    // Create message
    Message msg(msg_id, msg_name, length);
    if (tokens.size() > 4) {
        msg.transmitter = tokens[4];
    }

    messages_[msg_id] = msg;
    name_to_id_[msg_name] = msg_id;
    is_loaded_ = true;
}

void Parser::parseSignalLine(const std::string& line) {
    // Format: SG_ SignalName : Start|Length@ByteOrder+ (Factor,Offset) [Min|Max] "Unit" Receiver
    // Example: SG_ SMCU_Stat : 0|8@1+ (1,0) [0|255] "" SMCU

    // Find the message this signal belongs to
    // Signal lines are typically preceded by message lines
    // We need to parse the signal and add it to the appropriate message

    // Extract signal name
    size_t name_end = line.find(' ');
    if (name_end == std::string::npos) {
        return;
    }
    std::string signal_name = line.substr(3, name_end - 3);
    signal_name = trim(signal_name);

    // Find position specification: : Start|Length@ByteOrder
    size_t pos_start = line.find(':');
    if (pos_start == std::string::npos) {
        return;
    }

    size_t pos_end = line.find('@', pos_start);
    if (pos_end == std::string::npos) {
        return;
    }

    std::string pos_str = line.substr(pos_start + 1, pos_end - pos_start - 1);
    pos_str = trim(pos_str);

    // Parse start|length
    size_t pipe_pos = pos_str.find('|');
    if (pipe_pos == std::string::npos) {
        return;
    }

    size_t start_bit = 0;
    size_t length = 8;
    try {
        start_bit = std::stoul(pos_str.substr(0, pipe_pos));
        length = std::stoul(pos_str.substr(pipe_pos + 1));
    } catch (...) {
        return;
    }

    // Parse byte order: @1+ (Intel/little-endian) or @0- (Motorola/big-endian)
    bool is_little_endian = true;
    bool is_signed = false;
    if (pos_end + 1 < line.size()) {
        char byte_order = line[pos_end + 1];
        is_little_endian = (byte_order == '1');
        is_signed = (byte_order == '+' || byte_order == '1');
        if (byte_order == '-') {
            is_signed = true;
        }
    }

    // Parse factor and offset: (Factor,Offset)
    double factor = 1.0;
    double offset = 0.0;
    size_t paren_start = line.find('(');
    size_t paren_end = line.find(')', paren_start);
    if (paren_start != std::string::npos && paren_end != std::string::npos) {
        std::string params = line.substr(paren_start + 1, paren_end - paren_start - 1);
        std::vector<std::string> param_tokens = tokenize(params, ',');
        if (param_tokens.size() >= 2) {
            try {
                factor = std::stod(param_tokens[0]);
                offset = std::stod(param_tokens[1]);
            } catch (...) {
                // Use defaults
            }
        }
    }

    // Parse min|max: [Min|Max]
    double min_val = 0.0;
    double max_val = 0.0;
    size_t bracket_start = line.find('[');
    size_t bracket_end = line.find(']', bracket_start);
    if (bracket_start != std::string::npos && bracket_end != std::string::npos) {
        std::string range = line.substr(bracket_start + 1, bracket_end - bracket_start - 1);
        size_t pipe_pos = range.find('|');
        if (pipe_pos != std::string::npos) {
            try {
                min_val = std::stod(range.substr(0, pipe_pos));
                max_val = std::stod(range.substr(pipe_pos + 1));
            } catch (...) {
                // Use defaults
            }
        }
    }

    // Parse unit: "Unit"
    std::string unit;
    size_t quote_start = line.find('"');
    if (quote_start != std::string::npos) {
        size_t quote_end = line.find('"', quote_start + 1);
        if (quote_end != std::string::npos) {
            unit = line.substr(quote_start + 1, quote_end - quote_start - 1);
        }
    }

    // Create signal
    Signal signal;
    signal.name = signal_name;
    signal.start_bit = start_bit;
    signal.length = length;
    signal.is_little_endian = is_little_endian;
    signal.is_signed = is_signed;
    signal.factor = factor;
    signal.offset = offset;
    signal.min_value = min_val;
    signal.max_value = max_val;
    signal.unit = unit;

    // Find the current message to add this signal to
    // This is a simplified approach - in a real parser, we'd track the current message context
    if (!messages_.empty()) {
        // Add to the most recently added message (simplified)
        auto& msg = messages_.rbegin()->second;
        msg.signals[signal_name] = signal;
        msg.signal_order.push_back(signal_name);
    }
}

void Parser::parseValueTableLine(const std::string& line) {
    // Format: VAL_ SignalName Value1 "Text1" Value2 "Text2" ... ;
    // Example: VAL_ SMCU_Stat 0 "Idle" 1 "Running" 2 "Error" ;

    // Find signal name
    size_t name_end = line.find(' ');
    if (name_end == std::string::npos) {
        return;
    }
    std::string signal_name = line.substr(4, name_end - 4);
    signal_name = trim(signal_name);

    // Find which message this signal belongs to
    // Search through all messages for this signal
    Signal* target_signal = nullptr;
    for (auto& [id, msg] : messages_) {
        auto sig_it = msg.signals.find(signal_name);
        if (sig_it != msg.signals.end()) {
            target_signal = &(sig_it->second);
            break;
        }
    }

    if (!target_signal) {
        return;
    }

    // Parse value table entries
    std::string rest = line.substr(name_end + 1);
    rest = trim(rest);

    // Remove trailing semicolon
    if (!rest.empty() && rest.back() == ';') {
        rest.pop_back();
    }

    // Parse entries: Value1 "Text1" Value2 "Text2" ...
    std::istringstream stream(rest);
    std::string token;
    bool expecting_value = true;
    int current_value = 0;

    while (stream >> token) {
        if (expecting_value) {
            try {
                current_value = std::stoi(token);
                expecting_value = false;
            } catch (...) {
                expecting_value = true;
            }
        } else {
            // This should be a quoted string
            if (token.front() == '"') {
                // Handle multi-word strings
                std::string value_name = token.substr(1);
                while (value_name.back() != '"' && stream >> token) {
                    value_name += " " + token;
                }
                if (!value_name.empty() && value_name.back() == '"') {
                    value_name.pop_back();
                }
                target_signal->value_table[current_value] = value_name;
            }
            expecting_value = true;
        }
    }
}

void Parser::parseAttributeLine(const std::string& line) {
    // Attributes are ignored for basic parsing
    // Could be extended to store custom attributes
    (void)line; // Suppress unused parameter warning
}

// ============================================================================
// Bit Manipulation
// ============================================================================

uint64_t Parser::extractBits(const uint8_t* data, size_t start_bit, size_t length,
                             bool is_little_endian) const {
    if (length == 0 || length > 64) {
        return 0;
    }

    uint64_t result = 0;

    if (is_little_endian) {
        // Intel format: bits start from LSB of the starting byte
        size_t byte_index = start_bit / 8;
        size_t bit_offset = start_bit % 8;

        for (size_t i = 0; i < length; ++i) {
            size_t current_bit = bit_offset + i;
            size_t current_byte = byte_index + (current_bit / 8);
            size_t bit_in_byte = current_bit % 8;

            if (data[current_byte] & (1 << bit_in_byte)) {
                result |= (1ULL << i);
            }
        }
    } else {
        // Motorola format: big-endian within the message
        // Bit 0 is the MSB of the first byte
        for (size_t i = 0; i < length; ++i) {
            size_t bit_position = start_bit + i;
            size_t byte_index = bit_position / 8;
            size_t bit_offset = 7 - (bit_position % 8);

            if (data[byte_index] & (1 << bit_offset)) {
                result |= (1ULL << (length - 1 - i));
            }
        }
    }

    return result;
}

void Parser::insertBits(uint8_t* data, uint64_t value, size_t start_bit, size_t length,
                        bool is_little_endian) const {
    if (length == 0 || length > 64) {
        return;
    }

    if (is_little_endian) {
        // Intel format
        size_t byte_index = start_bit / 8;
        size_t bit_offset = start_bit % 8;

        for (size_t i = 0; i < length; ++i) {
            size_t current_bit = bit_offset + i;
            size_t current_byte = byte_index + (current_bit / 8);
            size_t bit_in_byte = current_bit % 8;

            if (value & (1ULL << i)) {
                data[current_byte] |= (1 << bit_in_byte);
            } else {
                data[current_byte] &= ~(1 << bit_in_byte);
            }
        }
    } else {
        // Motorola format
        for (size_t i = 0; i < length; ++i) {
            size_t bit_position = start_bit + i;
            size_t byte_index = bit_position / 8;
            size_t bit_offset = 7 - (bit_position % 8);

            if (value & (1ULL << (length - 1 - i))) {
                data[byte_index] |= (1 << bit_offset);
            } else {
                data[byte_index] &= ~(1 << bit_offset);
            }
        }
    }
}

double Parser::decodeSignalValue(const uint8_t* data, const Signal& signal) const {
    uint64_t raw_value = extractBits(data, signal.start_bit, signal.length,
                                     signal.is_little_endian);

    // Handle signed values
    if (signal.is_signed && signal.length < 64) {
        uint64_t sign_bit = (1ULL << (signal.length - 1));
        if (raw_value & sign_bit) {
            raw_value |= (~0ULL << signal.length);
        }
    }

    return static_cast<double>(raw_value) * signal.factor + signal.offset;
}

void Parser::encodeSignalValue(uint8_t* data, const Signal& signal, double value) const {
    // Apply inverse of factor and offset
    double raw_value = (value - signal.offset) / signal.factor;

    // Convert to integer
    uint64_t int_value;
    if (signal.is_signed) {
        int_value = static_cast<int64_t>(std::round(raw_value));
    } else {
        int_value = static_cast<uint64_t>(std::round(raw_value));
    }

    insertBits(data, int_value, signal.start_bit, signal.length, signal.is_little_endian);
}

// ============================================================================
// Utility Functions
// ============================================================================

std::string Parser::trim(const std::string& str) const {
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) {
        return "";
    }
    size_t end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}

std::vector<std::string> Parser::tokenize(const std::string& str, char delimiter) const {
    std::vector<std::string> tokens;
    std::istringstream stream(str);
    std::string token;

    while (std::getline(stream, token, delimiter)) {
        std::string trimmed = trim(token);
        if (!trimmed.empty()) {
            tokens.push_back(trimmed);
        }
    }

    return tokens;
}

} // namespace dbc
