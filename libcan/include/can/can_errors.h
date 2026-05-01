/**
 * @file can_errors.h
 * @brief Error handling utilities for CAN library
 *
 * Provides exception classes and error handling utilities
 * for the CAN library.
 *
 * @copyright Copyright (c) 2024 AVL Test Equipment
 * @license MIT License
 */

#ifndef CAN_ERRORS_H
#define CAN_ERRORS_H

#include <exception>
#include <string>
#include <system_error>

namespace can {

/**
 * @brief Base exception class for CAN library errors
 */
class CanException : public std::exception {
public:
    explicit CanException(const std::string& what)
        : message_(what) {}

    explicit CanException(const char* what)
        : message_(what) {}

    const char* what() const noexcept override {
        return message_.c_str();
    }

    const std::string& message() const {
        return message_;
    }

protected:
    std::string message_;
};

/**
 * @brief Exception for interface-related errors
 */
class InterfaceException : public CanException {
public:
    explicit InterfaceException(const std::string& what)
        : CanException(what) {}

    explicit InterfaceException(const std::string& interface, const std::string& what)
        : CanException("Interface '" + interface + "': " + what),
          interface_(interface) {}

    const std::string& getInterface() const {
        return interface_;
    }

private:
    std::string interface_;
};

/**
 * @brief Exception for frame-related errors
 */
class FrameException : public CanException {
public:
    explicit FrameException(const std::string& what)
        : CanException(what) {}
};

/**
 * @brief Exception for configuration errors
 */
class ConfigException : public CanException {
public:
    explicit ConfigException(const std::string& what)
        : CanException(what) {}
};

/**
 * @brief Exception for runtime errors
 */
class RuntimeException : public CanException {
public:
    explicit RuntimeException(const std::string& what)
        : CanException(what) {}
};

/**
 * @brief Throw exception with system error
 * @param ec System error code
 * @param message Error message
 * @throws std::system_error
 */
inline void throw_system_error(const std::error_code& ec, const std::string& message) {
    throw std::system_error(ec, message);
}

/**
 * @brief Throw exception with errno
 * @param message Error message
 * @throws std::system_error
 */
inline void throw_errno_error(const std::string& message) {
    throw std::system_error(errno, std::generic_category(), message);
}

} // namespace can

#endif // CAN_ERRORS_H
