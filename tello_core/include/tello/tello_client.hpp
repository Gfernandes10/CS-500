#ifndef TELLO_TELLO_CLIENT_HPP
#define TELLO_TELLO_CLIENT_HPP

#include "types.hpp"
#include "udp_socket.hpp"
#include "command_executor.hpp"
#include "logger.hpp"
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

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
        std::string stage;
        bool command_channel_available = false;
    };

    struct ReliabilityConfig {
        int32_t command_max_attempts = 3;
        int32_t command_timeout_ms = 700;
        int32_t command_retry_delay_ms = 100;

        int32_t recovery_threshold = 3;
        int32_t recovery_attempts = 3;
        int32_t recovery_backoff_ms = 200;
    };

    struct KeepaliveStats {
        bool running = false;
        uint64_t tick_total = 0;
        uint64_t success_total = 0;
        uint64_t failure_total = 0;
        uint64_t skipped_busy_total = 0;
        uint64_t skipped_uninitialized_total = 0;
        std::string last_command;
        std::string last_response;
        ResponseCode last_result = ResponseCode::ERROR;
        int64_t last_latency_ms = 0;
        int64_t last_success_age_ms = -1;
        int64_t last_tick_age_ms = -1;
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

    /// Attempt a full SDK/video recovery after a drone power-cycle.
    /// Performs one non-blocking-ish sequence: reinitialize, command, battery?, streamon.
    VideoRecoveryStatus recoverAfterPowerCycle();

    /// Read commands
    ResponseCode getBattery(std::string& response);
    ResponseCode getSpeed(std::string& response);
    ResponseCode getFlightTime(std::string& response);
    ResponseCode getSdkVersion(std::string& response);
    ResponseCode getSerialNumber(std::string& response);

    /// Generic command interfaces
    ResponseCode sendCommand(const std::string& command);
    ResponseCode sendCommandWithResponse(const std::string& command, std::string& response);
    /// Send a command without waiting for an SDK response.
    /// Intended for high-frequency control streams such as continuous rc.
    ResponseCode sendCommandNoWait(const std::string& command);

    /// Start a background SDK keepalive command loop.
    /// This is useful for GUI/ROS/API users that may keep SDK mode open without
    /// sending frequent commands. The loop sends a lightweight query command.
    ResponseCode startSdkKeepalive(
        int32_t interval_ms = 5000,
        const std::string& command = "battery?"
    );

    /// Stop the background SDK keepalive loop if it is running.
    void stopSdkKeepalive();

    /// Check whether SDK keepalive is currently running.
    bool isSdkKeepaliveRunning() const;

    /// Get diagnostic counters for the background SDK keepalive loop.
    KeepaliveStats getSdkKeepaliveStats() const;

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
    void keepaliveLoop();
    void markCommandSuccess();
    void markCommandFailure();

    mutable std::recursive_mutex command_mutex_;
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

    std::atomic<int32_t> foreground_command_requests_;
    std::atomic<bool> keepalive_running_;
    std::thread keepalive_thread_;
    mutable std::mutex keepalive_mutex_;
    std::condition_variable keepalive_cv_;
    int32_t keepalive_interval_ms_;
    std::string keepalive_command_;
    KeepaliveStats keepalive_stats_;
    bool has_keepalive_last_success_;
    bool has_keepalive_last_tick_;
    std::chrono::steady_clock::time_point keepalive_last_success_tp_;
    std::chrono::steady_clock::time_point keepalive_last_tick_tp_;
};

} // namespace tello

#endif // TELLO_TELLO_CLIENT_HPP
