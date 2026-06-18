#include "tello/state_receiver.hpp"

#include <chrono>
#include <thread>
#include <vector>

namespace tello {

StateReceiver::StateReceiver()
    : socket_(nullptr),
      parser_(),
      state_mutex_(),
      latest_state_{},
      running_(false),
      has_state_(false),
      worker_(),
    stats_mutex_(),
    stats_(),
    last_packet_tp_(),
    has_last_packet_tp_(false),
      last_error_("") {}

StateReceiver::~StateReceiver() {
    stop();
}

ResponseCode StateReceiver::start(const std::string& local_ip, uint16_t local_port, int32_t timeout_ms) {
    if (running_.load()) {
        return ResponseCode::OK;
    }

    socket_ = std::make_shared<UdpSocket>();
    const ResponseCode bind_rc = socket_->bind(local_ip, local_port, timeout_ms);
    if (bind_rc != ResponseCode::OK) {
        last_error_ = "Failed to bind telemetry socket";
        socket_.reset();
        return bind_rc;
    }

    has_state_ = false;
    last_error_.clear();
    resetTelemetryStats();
    running_ = true;

    // Launch background receive loop.
    worker_ = std::thread(&StateReceiver::receiveLoop, this);
    return ResponseCode::OK;
}

void StateReceiver::stop() {
    if (!running_.load()) {
        return;
    }

    running_ = false;

    // Closing the socket helps unblock recv timeout loop faster.
    if (socket_ != nullptr) {
        socket_->close();
    }

    if (worker_.joinable()) {
        worker_.join();
    }

    socket_.reset();
}

bool StateReceiver::isRunning() const {
    return running_.load();
}

TelloState StateReceiver::getLatestState() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return latest_state_;
}

bool StateReceiver::hasReceivedState() const {
    return has_state_.load();
}

std::string StateReceiver::getLastError() const {
    return last_error_;
}

StateReceiver::TelemetryStats StateReceiver::getTelemetryStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    TelemetryStats snapshot = stats_;

    if (has_last_packet_tp_) {
        const auto now = std::chrono::steady_clock::now();
        const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_packet_tp_).count();
        snapshot.last_packet_age_ms = static_cast<int64_t>(age);
    } else {
        snapshot.last_packet_age_ms = -1;
    }

    return snapshot;
}

void StateReceiver::resetTelemetryStats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = TelemetryStats{};
    has_last_packet_tp_ = false;
}

void StateReceiver::receiveLoop() {
    // We keep this loop resilient: packet-level parse errors do not kill the receiver.
    std::vector<uint8_t> buffer(4096);
    constexpr double kEmaAlpha = 0.2;

    while (running_.load()) {
        if (socket_ == nullptr || !socket_->isOpen()) {
            last_error_ = "Telemetry socket is not open";
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        int32_t bytes_received = 0;
        const ResponseCode recv_rc = socket_->recv(
            buffer.data(),
            static_cast<int32_t>(buffer.size()),
            bytes_received);

        if (!running_.load()) {
            break;
        }

        if (recv_rc == ResponseCode::TIMEOUT) {
            // Normal: no packet in this interval.
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                ++stats_.recv_timeouts;
            }
            continue;
        }

        if (recv_rc != ResponseCode::OK) {
            last_error_ = "Telemetry receive error";
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                ++stats_.recv_errors;
            }
            continue;
        }

        if (bytes_received <= 0) {
            continue;
        }

        {
            const auto now = std::chrono::steady_clock::now();
            std::lock_guard<std::mutex> lock(stats_mutex_);

            ++stats_.packets_total;
            if (has_last_packet_tp_) {
                const double dt = std::chrono::duration<double>(now - last_packet_tp_).count();
                if (dt > 0.0) {
                    const double hz_inst = 1.0 / dt;
                    stats_.rx_hz_instant = hz_inst;
                    if (stats_.rx_hz_ema <= 0.0) {
                        stats_.rx_hz_ema = hz_inst;
                    } else {
                        stats_.rx_hz_ema = (kEmaAlpha * hz_inst) + ((1.0 - kEmaAlpha) * stats_.rx_hz_ema);
                    }
                }
            }
            last_packet_tp_ = now;
            has_last_packet_tp_ = true;
        }

        const std::string raw_state(
            reinterpret_cast<const char*>(buffer.data()),
            static_cast<size_t>(bytes_received));

        TelloState parsed{};
        const ResponseCode parse_rc = parser_.parse(raw_state, parsed);
        if (parse_rc != ResponseCode::OK) {
            last_error_ = parser_.getLastError();
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                ++stats_.packets_invalid;
            }
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            latest_state_ = parsed;
        }

        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            ++stats_.packets_valid;
        }

        has_state_ = true;
    }
}

} // namespace tello