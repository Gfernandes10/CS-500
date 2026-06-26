#include "tello/command_executor.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <thread>
#include <utility>

namespace tello {
namespace {

// Remove leading/trailing whitespace and line breaks from socket responses.
std::string trim(const std::string& input) {
    size_t start = 0;
    while (start < input.size() &&
           std::isspace(static_cast<unsigned char>(input[start])) != 0) {
        ++start;
    }

    size_t end = input.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(input[end - 1])) != 0) {
        --end;
    }

    return input.substr(start, end - start);
}

// Lowercase normalization helps compare protocol control words like "ok".
std::string toLower(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

} // namespace

CommandExecutor::CommandExecutor(std::shared_ptr<UdpSocket> socket)
    : socket_(std::move(socket)),
      retry_config_(),
      last_response_(),
      last_error_() {}

void CommandExecutor::setRetryConfig(const RetryConfig& config) {
    // Keep configuration safe even if invalid values are provided.
    retry_config_.max_attempts = (config.max_attempts <= 0) ? 1 : config.max_attempts;
    retry_config_.timeout_ms = (config.timeout_ms < 0) ? 0 : config.timeout_ms;
    retry_config_.retry_delay_ms = (config.retry_delay_ms < 0) ? 0 : config.retry_delay_ms;
}

ResponseCode CommandExecutor::executeCommand(const std::string& command) {
    std::string response;
    return executeCommandWithResponse(command, response);
}

ResponseCode CommandExecutor::executeCommandWithResponse(
    const std::string& command,
    std::string& response
) {
    response.clear();
    last_response_.clear();
    last_error_.clear();

    const ResponseCode valid_rc = validateCommand(command);
    if (valid_rc != ResponseCode::OK) {
        return valid_rc;
    }

    if (socket_ == nullptr || !socket_->isOpen()) {
        last_error_ = "Socket is not available or not open";
        return ResponseCode::ERROR;
    }

    const bool expects_query_payload = !command.empty() && command.back() == '?';
    constexpr int32_t kControlFollowupTimeoutMs = 250;

    for (int32_t attempt = 1; attempt <= retry_config_.max_attempts; ++attempt) {
        const ResponseCode send_rc = sendCommandNoWait(command);
        if (send_rc != ResponseCode::OK) {
            if (attempt < retry_config_.max_attempts) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(retry_config_.retry_delay_ms));
                continue;
            }
            return send_rc;
        }

        // Wait for one response using the attempt timeout.
        const std::string raw_response = socket_->recvString(retry_config_.timeout_ms);
        std::string cleaned = trim(raw_response);

        if (cleaned.empty()) {
            last_error_ = "Timeout waiting for response";
            if (attempt < retry_config_.max_attempts) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(retry_config_.retry_delay_ms));
                continue;
            }
            return ResponseCode::TIMEOUT;
        }

        // Query commands should return payload (e.g. "85"), not plain "ok".
        // If we see "ok" here, it is often a delayed ack from a previous command,
        // so we read once more before deciding.
        if (expects_query_payload && toLower(cleaned) == "ok") {
            const std::string next_response = trim(socket_->recvString(retry_config_.timeout_ms));
            if (!next_response.empty()) {
                cleaned = next_response;
            }
        }

        if (expects_query_payload && toLower(cleaned) == "ok") {
            last_error_ = "Received stale ack while waiting for query payload";
            if (attempt < retry_config_.max_attempts) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(retry_config_.retry_delay_ms));
                continue;
            }
            return ResponseCode::PARSE_ERROR;
        }

        // Control commands can occasionally receive a stale "error" from a previous
        // exchange right before the actual ack for the current command arrives.
        // Do one short follow-up read to avoid false negatives in logs/state.
        if (!expects_query_payload && toLower(cleaned) == "error") {
            const std::string followup = trim(socket_->recvString(kControlFollowupTimeoutMs));
            if (!followup.empty() && toLower(followup) == "ok") {
                cleaned = followup;
            }
        }

        response = cleaned;
        last_response_ = cleaned;

        const ResponseCode parse_rc = parseResponse(cleaned);
        if (parse_rc == ResponseCode::OK) {
            return ResponseCode::OK;
        }

        // If drone explicitly says "error", do not retry blindly.
        return parse_rc;
    }

    // Defensive fallback; loop should have returned earlier.
    return ResponseCode::ERROR;
}

std::string CommandExecutor::getLastResponse() const {
    return last_response_;
}

std::string CommandExecutor::getLastError() const {
    return last_error_;
}

bool CommandExecutor::wasLastResponseOk() const {
    return toLower(trim(last_response_)) == "ok";
}

ResponseCode CommandExecutor::sendCommandNoWait(const std::string& command) {
    const ResponseCode valid_rc = validateCommand(command);
    if (valid_rc != ResponseCode::OK) {
        return valid_rc;
    }

    if (socket_ == nullptr || !socket_->isOpen()) {
        last_error_ = "Socket is not available or not open";
        return ResponseCode::ERROR;
    }

    const int32_t sent = socket_->sendString(command);
    if (sent <= 0) {
        last_error_ = "Failed to send command";
        return ResponseCode::ERROR;
    }

    return ResponseCode::OK;
}

ResponseCode CommandExecutor::parseResponse(const std::string& response) {
    const std::string cleaned = trim(response);
    if (cleaned.empty()) {
        last_error_ = "Empty response";
        return ResponseCode::TIMEOUT;
    }

    const std::string lowered = toLower(cleaned);

    // Control responses from Tello SDK.
    if (lowered == "ok") {
        last_error_.clear();
        return ResponseCode::OK;
    }

    if (lowered == "error") {
        last_error_ = "Drone returned error";
        return ResponseCode::ERROR;
    }

    // Query responses (for example: battery?, speed?, time?) are valid non-empty payloads.
    last_error_.clear();
    return ResponseCode::OK;
}

ResponseCode CommandExecutor::validateCommand(const std::string& command) {
    if (command.empty()) {
        last_error_ = "Command cannot be empty";
        return ResponseCode::ERROR;
    }

    return ResponseCode::OK;
}

} // namespace tello