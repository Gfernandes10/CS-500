#include "tello/metrics.hpp"

namespace tello {

MetricsCollector::MetricsCollector()
    : mutex_(), snapshot_() {}

void MetricsCollector::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = Snapshot{};
}

void MetricsCollector::addCommandLatencySample(double latency_ms) {
    (void)latency_ms;
    // TODO: implement in Phase E
}

void MetricsCollector::incrementTelemetryPacket() {
    // TODO: implement in Phase E
}

void MetricsCollector::incrementVideoFrame() {
    // TODO: implement in Phase E
}

void MetricsCollector::updateRates(double elapsed_seconds) {
    (void)elapsed_seconds;
    // TODO: implement in Phase E
}

MetricsCollector::Snapshot MetricsCollector::getSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

std::string MetricsCollector::toCsvLine() const {
    // TODO: implement in Phase E
    return "";
}

} // namespace tello