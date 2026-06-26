#include "tello/tello_client.hpp"

#include <chrono>
#include <thread>

namespace tello {

TelloClient::TelloClient()
    : initialized_(false),
    sdk_mode_confirmed_(false),
    consecutive_failures_(0),
    reliability_config_(),
    last_outage_failures_(0),
    connection_state_(ConnectionState::DISCONNECTED),
    pending_event_(ConnectionEvent::NONE),
    drone_ip_("192.168.10.1"),
    drone_port_(8889),
    local_port_(9000),
        last_video_recovery_attempt_tp_(std::chrono::steady_clock::now()),
        has_last_video_recovery_attempt_(false),
      command_socket_(nullptr),
      command_executor_(nullptr) {}

TelloClient::~TelloClient() {
    shutdown();
}

ResponseCode TelloClient::initialize(const std::string& drone_ip, uint16_t drone_port, uint16_t local_port) {
    drone_ip_ = drone_ip;
    drone_port_ = drone_port;
    local_port_ = local_port;

    shutdown();

    const ResponseCode open_rc = openCommandChannel();
    if (open_rc != ResponseCode::OK) {
        shutdown();
        return open_rc;
    }

    initialized_ = true;
    connection_state_ = ConnectionState::DISCONNECTED;
    pending_event_ = ConnectionEvent::NONE;
    has_last_video_recovery_attempt_ = false;
    return ResponseCode::OK;
}

ResponseCode TelloClient::openCommandChannel() {
    command_socket_ = std::make_shared<UdpSocket>();

    SocketConfig cfg;
    cfg.host = drone_ip_;
    cfg.port = drone_port_;
    // Keep the local command endpoint stable to avoid response routing drift.
    cfg.local_ip = "0.0.0.0";
    cfg.local_port = local_port_;
    cfg.timeout_ms = 1000;

    const ResponseCode open_rc = command_socket_->open(cfg);
    if (open_rc != ResponseCode::OK) {
        return open_rc;
    }

    command_executor_ = std::make_shared<CommandExecutor>(command_socket_);
    // Apply command retry policy from configurable reliability settings.
    CommandExecutor::RetryConfig retry_cfg;
    retry_cfg.max_attempts = reliability_config_.command_max_attempts;
    retry_cfg.timeout_ms = reliability_config_.command_timeout_ms;
    retry_cfg.retry_delay_ms = reliability_config_.command_retry_delay_ms;
    command_executor_->setRetryConfig(retry_cfg);

    sdk_mode_confirmed_ = false;
    consecutive_failures_ = 0;
    connection_state_ = ConnectionState::DISCONNECTED;
    return ResponseCode::OK;
}

ResponseCode TelloClient::recoverCommandSession() {
    connection_state_ = ConnectionState::RECOVERING;

    for (int32_t attempt = 1; attempt <= reliability_config_.recovery_attempts; ++attempt) {
        if (command_socket_ != nullptr) {
            command_socket_->close();
        }
        command_executor_.reset();
        command_socket_.reset();

        const ResponseCode open_rc = openCommandChannel();
        if (open_rc != ResponseCode::OK) {
            std::this_thread::sleep_for(std::chrono::milliseconds(reliability_config_.recovery_backoff_ms));
            continue;
        }

        // Re-enter SDK mode after reopening command channel.
        const ResponseCode sdk_rc = enterSdkMode();
        if (sdk_rc == ResponseCode::OK) {
            consecutive_failures_ = 0;
            return ResponseCode::OK;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(reliability_config_.recovery_backoff_ms));
    }

    connection_state_ = ConnectionState::DISCONNECTED;
    return ResponseCode::TIMEOUT;
}

ResponseCode TelloClient::ensureSdkMode() {
    if (sdk_mode_confirmed_) {
        return ResponseCode::OK;
    }

    return enterSdkMode();
}

void TelloClient::shutdown() {
    initialized_ = false;
    sdk_mode_confirmed_ = false;
    consecutive_failures_ = 0;
    last_outage_failures_ = 0;
    connection_state_ = ConnectionState::DISCONNECTED;
    pending_event_ = ConnectionEvent::NONE;
    has_last_video_recovery_attempt_ = false;

    if (command_socket_ != nullptr) {
        command_socket_->close();
    }

    command_executor_.reset();
    command_socket_.reset();
}

ResponseCode TelloClient::enterSdkMode() {
    if (!initialized_ || command_executor_ == nullptr) {
        return ResponseCode::ERROR;
    }

    const ResponseCode rc = command_executor_->executeCommand("command");
    if (rc == ResponseCode::OK) {
        sdk_mode_confirmed_ = true;
        markCommandSuccess();
    } else {
        sdk_mode_confirmed_ = false;
        markCommandFailure();
    }

    return rc;
}

ResponseCode TelloClient::takeoff() {
    return sendCommand("takeoff");
}

ResponseCode TelloClient::land() {
    return sendCommand("land");
}

ResponseCode TelloClient::emergency() {
    return sendCommand("emergency");
}

ResponseCode TelloClient::streamOn() {
    return sendCommand("streamon");
}

ResponseCode TelloClient::streamOff() {
    return sendCommand("streamoff");
}

TelloClient::VideoRecoveryStatus TelloClient::recoverVideoStreamIfStalled(
    int64_t last_packet_age_ms,
    int64_t stall_threshold_ms,
    int64_t recovery_cooldown_ms) {
    VideoRecoveryStatus status;

    if (stall_threshold_ms < 0) {
        stall_threshold_ms = 0;
    }
    if (recovery_cooldown_ms < 0) {
        recovery_cooldown_ms = 0;
    }

    if (last_packet_age_ms < 0 || last_packet_age_ms < stall_threshold_ms) {
        return status;
    }

    const auto now = std::chrono::steady_clock::now();
    if (has_last_video_recovery_attempt_) {
        const auto elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last_video_recovery_attempt_tp_).count();
        if (elapsed_ms < recovery_cooldown_ms) {
            return status;
        }
    }

    has_last_video_recovery_attempt_ = true;
    last_video_recovery_attempt_tp_ = now;

    status.attempted = true;
    status.result = streamOn();
    if (status.result == ResponseCode::OK) {
        return status;
    }

    // Hard fallback for power-cycle cases: reopen command session and re-enter SDK mode.
    status.used_hard_recovery = true;
    constexpr int32_t kHardRecoveryAttempts = 3;
    constexpr int32_t kHardRecoveryBackoffMs = 1500;
    for (int32_t attempt = 0; attempt < kHardRecoveryAttempts; ++attempt) {
        const ResponseCode init_rc = initialize(drone_ip_, drone_port_, local_port_);
        if (init_rc != ResponseCode::OK) {
            status.result = init_rc;
            std::this_thread::sleep_for(std::chrono::milliseconds(kHardRecoveryBackoffMs));
            continue;
        }

        const ResponseCode sdk_rc = enterSdkMode();
        if (sdk_rc != ResponseCode::OK) {
            status.result = sdk_rc;
            std::this_thread::sleep_for(std::chrono::milliseconds(kHardRecoveryBackoffMs));
            continue;
        }

        const ResponseCode stream_rc = streamOn();
        status.result = stream_rc;
        if (stream_rc == ResponseCode::OK) {
            return status;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(kHardRecoveryBackoffMs));
    }

    return status;
}

ResponseCode TelloClient::getBattery(std::string& response) {
    return sendCommandWithResponse("battery?", response);
}

ResponseCode TelloClient::getSpeed(std::string& response) {
    return sendCommandWithResponse("speed?", response);
}

ResponseCode TelloClient::getFlightTime(std::string& response) {
    return sendCommandWithResponse("time?", response);
}

ResponseCode TelloClient::getSdkVersion(std::string& response) {
    return sendCommandWithResponse("sdk?", response);
}

ResponseCode TelloClient::getSerialNumber(std::string& response) {
    return sendCommandWithResponse("sn?", response);
}

ResponseCode TelloClient::sendCommand(const std::string& command) {
    if (!initialized_ || command_executor_ == nullptr) {
        return ResponseCode::ERROR;
    }

    const bool is_sdk_command = (command == "command");
    const bool expects_query_payload = !command.empty() && command.back() == '?';
    if (!is_sdk_command) {
        const ResponseCode sdk_rc = ensureSdkMode();
        if (sdk_rc != ResponseCode::OK) {
            if (consecutive_failures_ >= reliability_config_.recovery_threshold) {
                const ResponseCode recovery_rc = recoverCommandSession();
                if (recovery_rc != ResponseCode::OK) {
                    const ResponseCode reinit_rc = initialize(drone_ip_, drone_port_, local_port_);
                    if (reinit_rc != ResponseCode::OK) {
                        return recovery_rc;
                    }

                    const ResponseCode reinit_sdk_rc = enterSdkMode();
                    if (reinit_sdk_rc != ResponseCode::OK) {
                        return recovery_rc;
                    }
                }
            }
            if (ensureSdkMode() != ResponseCode::OK) {
                return sdk_rc;
            }
        }
    }

    ResponseCode rc = command_executor_->executeCommand(command);
    if (rc == ResponseCode::OK) {
        markCommandSuccess();
        if (is_sdk_command) {
            sdk_mode_confirmed_ = true;
        }
        return rc;
    }

    // Some control-path "error" replies are stale/ambiguous on UDP and may
    // still correspond to a successfully executed action. Do not immediately
    // force RECOVERING for non-query control commands.
    if (rc == ResponseCode::ERROR && !expects_query_payload && !is_sdk_command) {
        return rc;
    }

    sdk_mode_confirmed_ = false;
    markCommandFailure();
    if (consecutive_failures_ >= reliability_config_.recovery_threshold) {
        const ResponseCode recovery_rc = recoverCommandSession();
        if (recovery_rc != ResponseCode::OK) {
            const ResponseCode reinit_rc = initialize(drone_ip_, drone_port_, local_port_);
            if (reinit_rc != ResponseCode::OK) {
                return recovery_rc;
            }

            const ResponseCode reinit_sdk_rc = enterSdkMode();
            if (reinit_sdk_rc != ResponseCode::OK) {
                return recovery_rc;
            }
        }
        rc = command_executor_->executeCommand(command);
        if (rc == ResponseCode::OK) {
            markCommandSuccess();
        } else {
            markCommandFailure();
        }
    }

    return rc;
}

ResponseCode TelloClient::sendCommandWithResponse(const std::string& command, std::string& response) {
    response.clear();
    if (!initialized_ || command_executor_ == nullptr) {
        return ResponseCode::ERROR;
    }

    const bool is_sdk_command = (command == "command");
    const bool expects_query_payload = !command.empty() && command.back() == '?';

    const ResponseCode sdk_rc = ensureSdkMode();
    if (sdk_rc != ResponseCode::OK) {
        if (consecutive_failures_ >= reliability_config_.recovery_threshold) {
            const ResponseCode recovery_rc = recoverCommandSession();
            if (recovery_rc != ResponseCode::OK) {
                const ResponseCode reinit_rc = initialize(drone_ip_, drone_port_, local_port_);
                if (reinit_rc != ResponseCode::OK) {
                    return recovery_rc;
                }

                const ResponseCode reinit_sdk_rc = enterSdkMode();
                if (reinit_sdk_rc != ResponseCode::OK) {
                    return recovery_rc;
                }
            }
        } else {
            return sdk_rc;
        }
    }

    ResponseCode rc = command_executor_->executeCommandWithResponse(command, response);
    if (rc == ResponseCode::OK) {
        markCommandSuccess();
        return rc;
    }

    // Keep channel state stable on ambiguous control-command "error" replies,
    // so a following safety command (for example, land) is not penalized.
    if (rc == ResponseCode::ERROR && !expects_query_payload && !is_sdk_command) {
        return rc;
    }

    sdk_mode_confirmed_ = false;
    markCommandFailure();
    if (consecutive_failures_ >= reliability_config_.recovery_threshold) {
        const ResponseCode recovery_rc = recoverCommandSession();
        if (recovery_rc != ResponseCode::OK) {
            return recovery_rc;
        }

        response.clear();
        rc = command_executor_->executeCommandWithResponse(command, response);
        if (rc == ResponseCode::OK) {
            markCommandSuccess();
            return rc;
        } else {
            markCommandFailure();
        }
    }

    // For command queries, one direct recovery retry helps after Wi-Fi reassociation.
    if (rc == ResponseCode::TIMEOUT) {
        const ResponseCode recovery_rc = recoverCommandSession();
        if (recovery_rc == ResponseCode::OK) {
            response.clear();
            rc = command_executor_->executeCommandWithResponse(command, response);
            if (rc == ResponseCode::OK) {
                markCommandSuccess();
            } else {
                markCommandFailure();
            }
        } else {
            // Last resort: fully reinitialize command channel from stored settings.
            const ResponseCode reinit_rc = initialize(drone_ip_, drone_port_, local_port_);
            if (reinit_rc != ResponseCode::OK) {
                return recovery_rc;
            }

            const ResponseCode reinit_sdk_rc = enterSdkMode();
            if (reinit_sdk_rc != ResponseCode::OK) {
                return recovery_rc;
            }

            response.clear();
            rc = command_executor_->executeCommandWithResponse(command, response);
            if (rc == ResponseCode::OK) {
                markCommandSuccess();
            } else {
                markCommandFailure();
            }
        }
    }

    return rc;
}

bool TelloClient::isInitialized() const {
    return initialized_;
}

TelloClient::ConnectionState TelloClient::getConnectionState() const {
    return connection_state_;
}

TelloClient::ConnectionEvent TelloClient::consumeConnectionEvent() {
    const ConnectionEvent evt = pending_event_;
    pending_event_ = ConnectionEvent::NONE;
    return evt;
}

int32_t TelloClient::getConsecutiveFailures() const {
    return consecutive_failures_;
}

int32_t TelloClient::getLastOutageFailures() const {
    return last_outage_failures_;
}

TelloClient::ReliabilityConfig TelloClient::getReliabilityConfig() const {
    return reliability_config_;
}

void TelloClient::setReliabilityConfig(const ReliabilityConfig& config) {
    reliability_config_ = config;

    // Keep configuration safe if caller provides invalid values.
    if (reliability_config_.command_max_attempts <= 0) {
        reliability_config_.command_max_attempts = 1;
    }
    if (reliability_config_.command_timeout_ms < 0) {
        reliability_config_.command_timeout_ms = 0;
    }
    if (reliability_config_.command_retry_delay_ms < 0) {
        reliability_config_.command_retry_delay_ms = 0;
    }
    if (reliability_config_.recovery_threshold <= 0) {
        reliability_config_.recovery_threshold = 1;
    }
    if (reliability_config_.recovery_attempts <= 0) {
        reliability_config_.recovery_attempts = 1;
    }
    if (reliability_config_.recovery_backoff_ms < 0) {
        reliability_config_.recovery_backoff_ms = 0;
    }

    // If already initialized, update executor immediately for subsequent commands.
    if (command_executor_ != nullptr) {
        CommandExecutor::RetryConfig retry_cfg;
        retry_cfg.max_attempts = reliability_config_.command_max_attempts;
        retry_cfg.timeout_ms = reliability_config_.command_timeout_ms;
        retry_cfg.retry_delay_ms = reliability_config_.command_retry_delay_ms;
        command_executor_->setRetryConfig(retry_cfg);
    }
}

std::shared_ptr<UdpSocket> TelloClient::getCommandSocket() const {
    return command_socket_;
}

std::shared_ptr<CommandExecutor> TelloClient::getCommandExecutor() const {
    return command_executor_;
}

void TelloClient::markCommandSuccess() {
    const int32_t failures_before_recovery = consecutive_failures_;
    consecutive_failures_ = 0;
    if (connection_state_ != ConnectionState::CONNECTED) {
        if (connection_state_ == ConnectionState::RECOVERING) {
            last_outage_failures_ = failures_before_recovery;
            pending_event_ = ConnectionEvent::RESTORED;
        }
        connection_state_ = ConnectionState::CONNECTED;
    }
}

void TelloClient::markCommandFailure() {
    ++consecutive_failures_;
    if (connection_state_ == ConnectionState::CONNECTED) {
        pending_event_ = ConnectionEvent::LOST;
    }
    connection_state_ = ConnectionState::RECOVERING;
}

} // namespace tello