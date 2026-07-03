#include "tello/state_receiver.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <thread>
#include <vector>

namespace tello {

namespace {

const char* responseCodeToString(ResponseCode rc) {
    switch (rc) {
        case ResponseCode::OK:
            return "OK";
        case ResponseCode::ERROR:
            return "ERROR";
        case ResponseCode::TIMEOUT:
            return "TIMEOUT";
        case ResponseCode::PARSE_ERROR:
            return "PARSE_ERROR";
        default:
            return "UNKNOWN";
    }
}

} // namespace

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
            history_mutex_(),
            state_buffer_capacity_(300),
            state_buffer_(),
            recorded_samples_(),
            recorded_rc_samples_(),
            recording_enabled_(false),
            recording_start_timestamp_ms_(-1),
            recording_start_steady_tp_(std::chrono::steady_clock::now()),
            next_state_sequence_(0),
            next_rc_sequence_(0),
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

void StateReceiver::setStateBufferCapacity(size_t capacity) {
    std::lock_guard<std::mutex> lock(history_mutex_);
    state_buffer_capacity_ = capacity;

    while (state_buffer_.size() > state_buffer_capacity_) {
        state_buffer_.pop_front();
    }
}

size_t StateReceiver::getStateBufferCapacity() const {
    std::lock_guard<std::mutex> lock(history_mutex_);
    return state_buffer_capacity_;
}

std::vector<StateReceiver::StateSample> StateReceiver::getBufferedStateSamples() const {
    std::lock_guard<std::mutex> lock(history_mutex_);
    return std::vector<StateSample>(state_buffer_.begin(), state_buffer_.end());
}

void StateReceiver::clearBufferedStateSamples() {
    std::lock_guard<std::mutex> lock(history_mutex_);
    state_buffer_.clear();
}

void StateReceiver::startStateRecording() {
    std::lock_guard<std::mutex> lock(history_mutex_);
    recording_enabled_ = true;
    recording_start_timestamp_ms_ = -1;
    recording_start_steady_tp_ = std::chrono::steady_clock::now();
    recorded_samples_.clear();
    recorded_rc_samples_.clear();
}

void StateReceiver::stopStateRecording() {
    std::lock_guard<std::mutex> lock(history_mutex_);
    recording_enabled_ = false;
}

bool StateReceiver::isStateRecording() const {
    std::lock_guard<std::mutex> lock(history_mutex_);
    return recording_enabled_;
}

std::vector<StateReceiver::StateSample> StateReceiver::getRecordedStateSamples() const {
    std::lock_guard<std::mutex> lock(history_mutex_);
    return recorded_samples_;
}

std::vector<StateReceiver::RcCommandSample> StateReceiver::getRecordedRcCommandSamples() const {
    std::lock_guard<std::mutex> lock(history_mutex_);
    return recorded_rc_samples_;
}

size_t StateReceiver::getRecordedStateSampleCount() const {
    std::lock_guard<std::mutex> lock(history_mutex_);
    return recorded_samples_.size();
}

size_t StateReceiver::getRecordedRcCommandSampleCount() const {
    std::lock_guard<std::mutex> lock(history_mutex_);
    return recorded_rc_samples_.size();
}

void StateReceiver::recordRcCommandSample(
    int a,
    int b,
    int c,
    int d,
    const std::string& source,
    ResponseCode response) {
    const auto now_sys = std::chrono::system_clock::now();
    const auto now_steady = std::chrono::steady_clock::now();
    const int64_t ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now_sys.time_since_epoch()).count();

    std::lock_guard<std::mutex> lock(history_mutex_);
    if (!recording_enabled_) {
        return;
    }

    if (recording_start_timestamp_ms_ < 0) {
        recording_start_timestamp_ms_ = ts_ms;
        recording_start_steady_tp_ = now_steady;
    }

    RcCommandSample sample;
    sample.sequence = next_rc_sequence_++;
    sample.timestamp_ms = ts_ms;
    sample.recording_elapsed_ms = ts_ms - recording_start_timestamp_ms_;
    sample.steady_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now_steady - recording_start_steady_tp_).count();
    sample.a = a;
    sample.b = b;
    sample.c = c;
    sample.d = d;
    sample.source = source;
    sample.response = response;
    recorded_rc_samples_.push_back(std::move(sample));
}

void StateReceiver::clearRecordedStateSamples() {
    std::lock_guard<std::mutex> lock(history_mutex_);
    recorded_samples_.clear();
    recorded_rc_samples_.clear();
    recording_start_timestamp_ms_ = -1;
    recording_start_steady_tp_ = std::chrono::steady_clock::now();
}

ResponseCode StateReceiver::exportRecordedStateCsv(const std::string& file_path) const {
    std::vector<StateSample> samples;
    std::vector<RcCommandSample> rc_samples;
    {
        std::lock_guard<std::mutex> lock(history_mutex_);
        samples = recorded_samples_;
        rc_samples = recorded_rc_samples_;
    }

    std::ofstream csv(file_path, std::ios::out | std::ios::trunc);
    if (!csv.is_open()) {
        return ResponseCode::ERROR;
    }

    csv << "state_sequence,timestamp_ms,recording_elapsed_ms,steady_elapsed_ms,"
        << "pitch,roll,yaw,"
        << "vgx,vgy,vgz,"
        << "templ,temph,tof,h,bat,"
        << "baro,time,agx,agy,agz,"
        << "mid,x,y,z,"
        << "rc_a,rc_b,rc_c,rc_d,rc_source,rc_result,rc_timestamp_ms,rc_steady_elapsed_ms,rc_sequence"
        << '\n';

    size_t i_rc = 0;
    const RcCommandSample* active_rc = nullptr;

    for (const auto& s : samples) {
        while (i_rc < rc_samples.size() && rc_samples[i_rc].timestamp_ms <= s.timestamp_ms) {
            active_rc = &rc_samples[i_rc];
            ++i_rc;
        }

        csv << s.sequence << ','
            << s.timestamp_ms << ','
            << s.recording_elapsed_ms << ','
            << s.steady_elapsed_ms << ','
            << s.state.pitch << ','
            << s.state.roll << ','
            << s.state.yaw << ','
            << s.state.vgx << ','
            << s.state.vgy << ','
            << s.state.vgz << ','
            << s.state.templ << ','
            << s.state.temph << ','
            << s.state.tof << ','
            << s.state.h << ','
            << s.state.bat << ','
            << s.state.baro << ','
            << s.state.time << ','
            << s.state.agx << ','
            << s.state.agy << ','
            << s.state.agz << ','
            << s.state.mid << ','
            << s.state.x << ','
            << s.state.y << ','
            << s.state.z << ',';

        if (active_rc != nullptr) {
            csv << active_rc->a << ','
                << active_rc->b << ','
                << active_rc->c << ','
                << active_rc->d << ','
                << '"' << active_rc->source << '"' << ','
                << '"' << responseCodeToString(active_rc->response) << '"' << ','
                << active_rc->timestamp_ms << ','
                << active_rc->steady_elapsed_ms << ','
                << active_rc->sequence;
        } else {
            csv << ",,,,,,,,";
        }

        csv << '\n';
    }

    return ResponseCode::OK;
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

                const int64_t interarrival_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - last_packet_tp_).count();
                stats_.last_interarrival_ms = interarrival_ms;
                stats_.max_interarrival_ms = std::max(stats_.max_interarrival_ms, interarrival_ms);
                if (interarrival_ms >= 300) {
                    ++stats_.gap_events_300ms;
                    stats_.last_gap_ms = interarrival_ms;
                    stats_.last_gap_sequence = stats_.packets_total;
                }
                if (interarrival_ms >= 500) {
                    ++stats_.gap_events_500ms;
                }
                if (interarrival_ms >= 1000) {
                    ++stats_.gap_events_1000ms;
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

        const auto now_sys = std::chrono::system_clock::now();
        const auto now_steady = std::chrono::steady_clock::now();
        const int64_t ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now_sys.time_since_epoch()).count();

        {
            std::lock_guard<std::mutex> lock(history_mutex_);

            StateSample sample;
            sample.sequence = next_state_sequence_++;
            sample.timestamp_ms = ts_ms;
            sample.state = parsed;

            if (recording_enabled_) {
                if (recording_start_timestamp_ms_ < 0) {
                    recording_start_timestamp_ms_ = ts_ms;
                    recording_start_steady_tp_ = now_steady;
                }
                sample.recording_elapsed_ms = ts_ms - recording_start_timestamp_ms_;
                sample.steady_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now_steady - recording_start_steady_tp_).count();
                recorded_samples_.push_back(sample);
            }

            if (state_buffer_capacity_ > 0) {
                if (state_buffer_.size() >= state_buffer_capacity_) {
                    state_buffer_.pop_front();
                }
                state_buffer_.push_back(sample);
            }
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
