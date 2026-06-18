#ifndef TELLO_COMMAND_EXECUTOR_HPP
#define TELLO_COMMAND_EXECUTOR_HPP

#include "types.hpp"
#include "udp_socket.hpp"
#include <string>
#include <cstdint>
#include <memory>

namespace tello {

// ============================================================================
// Command Executor
// ============================================================================
/// Executes Tello commands with retry logic, timeout, and response parsing.
class CommandExecutor {
public:
    /// Retry configuration
    struct RetryConfig {
        int32_t max_attempts = 3;         ///< Maximum send attempts (default: 3)
        int32_t timeout_ms = 1000;        ///< Timeout per attempt (default: 1000ms)
        int32_t retry_delay_ms = 100;     ///< Delay between retries (default: 100ms)
    };

    /// Constructor
    /// @param socket Shared pointer to UDP socket (must be open and configured).
    explicit CommandExecutor(std::shared_ptr<UdpSocket> socket);

    /// Destructor
    ~CommandExecutor() = default;

    /// Configure retry behavior.
    /// @param config Retry configuration.
    void setRetryConfig(const RetryConfig& config);

    /// Execute a command and wait for response.
    /// @param command Command string (e.g., "command", "takeoff", "battery?").
    /// @return ResponseCode::OK if successful response received, error otherwise.
    ResponseCode executeCommand(const std::string& command);

    /// Execute a command and capture the response string.
    /// @param command Command string.
    /// @param response Output parameter: response from drone.
    /// @return ResponseCode::OK if successful, error otherwise.
    ResponseCode executeCommandWithResponse(const std::string& command, std::string& response);

    /// Get the last response received (useful for debugging).
    /// @return Last response string.
    std::string getLastResponse() const;

    /// Get the last error message (if any).
    /// @return Last error message.
    std::string getLastError() const;

    /// Check if the last response indicates success.
    /// @return True if response is "ok", false otherwise.
    bool wasLastResponseOk() const;

    /// Send a command without waiting for response (fire-and-forget).
    /// @param command Command string.
    /// @return ResponseCode::OK if send succeeded, error otherwise.
    ResponseCode sendCommandNoWait(const std::string& command);

private:
    std::shared_ptr<UdpSocket> socket_;     ///< UDP socket reference
    RetryConfig retry_config_;              ///< Current retry configuration
    std::string last_response_;             ///< Last response received
    std::string last_error_;                ///< Last error message

    /// Parse response and determine if it indicates success.
    /// @param response Raw response string.
    /// @return ResponseCode based on response content.
    ResponseCode parseResponse(const std::string& response);

    /// Validate command before sending (basic format check).
    /// @param command Command to validate.
    /// @return ResponseCode::OK if valid, error otherwise.
    ResponseCode validateCommand(const std::string& command);
};

} // namespace tello

#endif // TELLO_COMMAND_EXECUTOR_HPP