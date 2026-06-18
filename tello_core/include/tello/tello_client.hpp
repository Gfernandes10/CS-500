#ifndef TELLO_TELLO_CLIENT_HPP
#define TELLO_TELLO_CLIENT_HPP

#include "types.hpp"
#include "udp_socket.hpp"
#include "command_executor.hpp"
#include "logger.hpp"
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace tello {

// ============================================================================
// High-Level Tello Client
// ============================================================================
/// Facade for high-level drone commands and basic SDK lifecycle.
class TelloClient {
public:
    struct VideoRecoveryStatus {
        bool attempted = false;
        ResponseCode result = ResponseCode::OK;
        bool used_hard_recovery = false;
    };

    struct ReliabilityConfig {
        int32_t command_max_attempts = 3;
        int32_t command_timeout_ms = 700;
        int32_t command_retry_delay_ms = 100;

        int32_t recovery_threshold = 3;
        int32_t recovery_attempts = 3;
        int32_t recovery_backoff_ms = 200;
    };

    enum class ConnectionState {
        DISCONNECTED = 0,
        RECOVERING = 1,
        CONNECTED = 2,
    };

    enum class ConnectionEvent {
        NONE = 0,
        LOST = 1,
        RESTORED = 2,
    };

    /// Constructor
    TelloClient();

    /// Destructor
    ~TelloClient();

    /// Initialize command channel and internal components.
    /// @param drone_ip Tello IP address (default: 192.168.10.1).
    /// @param drone_port Tello command port (default: 8889).
    /// @param local_port Local UDP port for command socket binding if needed.
    /// @return ResponseCode::OK on success, error otherwise.
    ResponseCode initialize(
        const std::string& drone_ip = "192.168.10.1",
        uint16_t drone_port = 8889,
        uint16_t local_port = 9000
    );

    /// Shutdown client and release resources.
    void shutdown();

    /// Enter SDK mode (must be called before other commands).
    /// Sends: command
    ResponseCode enterSdkMode();

    /// Basic flight commands
    ResponseCode takeoff();
    ResponseCode land();
    ResponseCode emergency();

    /// Streaming commands
    ResponseCode streamOn();
    ResponseCode streamOff();

    /// Attempt to recover video stream when receiver indicates packet stall.
    /// If stall condition is not met or cooldown is active, no command is sent.
    /// @param last_packet_age_ms Latest packet age from video receiver stats.
    /// @param stall_threshold_ms Minimum age considered stalled.
    /// @param recovery_cooldown_ms Minimum delay between recovery attempts.
    /// @return Status indicating whether recovery was attempted and command result.
    VideoRecoveryStatus recoverVideoStreamIfStalled(
        int64_t last_packet_age_ms,
        int64_t stall_threshold_ms,
        int64_t recovery_cooldown_ms
    );

    /// Read commands
    ResponseCode getBattery(std::string& response);
    ResponseCode getSpeed(std::string& response);
    ResponseCode getFlightTime(std::string& response);
    ResponseCode getSdkVersion(std::string& response);
    ResponseCode getSerialNumber(std::string& response);

    /// Generic command interfaces
    ResponseCode sendCommand(const std::string& command);
    ResponseCode sendCommandWithResponse(const std::string& command, std::string& response);

    /// State
    bool isInitialized() const;
    ConnectionState getConnectionState() const;
    ConnectionEvent consumeConnectionEvent();
    int32_t getConsecutiveFailures() const;
    int32_t getLastOutageFailures() const;
    ReliabilityConfig getReliabilityConfig() const;

    /// Configure retry/recovery policy.
    /// Should be called before initialize(); if called while initialized,
    /// new values apply to subsequent operations.
    void setReliabilityConfig(const ReliabilityConfig& config);

    /// Access internals (for advanced modules / testing)
    std::shared_ptr<UdpSocket> getCommandSocket() const;
    std::shared_ptr<CommandExecutor> getCommandExecutor() const;

private:
    ResponseCode openCommandChannel();
    ResponseCode ensureSdkMode();
    ResponseCode recoverCommandSession();
    void markCommandSuccess();
    void markCommandFailure();

    bool initialized_;
    bool sdk_mode_confirmed_;
    int32_t consecutive_failures_;
    ReliabilityConfig reliability_config_;
    int32_t last_outage_failures_;
    ConnectionState connection_state_;
    ConnectionEvent pending_event_;
    std::string drone_ip_;
    uint16_t drone_port_;
    uint16_t local_port_;
    std::chrono::steady_clock::time_point last_video_recovery_attempt_tp_;
    bool has_last_video_recovery_attempt_;
    std::shared_ptr<UdpSocket> command_socket_;
    std::shared_ptr<CommandExecutor> command_executor_;
};

} // namespace tello

#endif // TELLO_TELLO_CLIENT_HPP