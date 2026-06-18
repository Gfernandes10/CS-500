#ifndef TELLO_VIDEO_STREAM_ASSEMBLER_HPP
#define TELLO_VIDEO_STREAM_ASSEMBLER_HPP

#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace tello {

// ============================================================================
// Video Stream Assembler (D2 boundary)
// ============================================================================
/// Converts raw H264 UDP payload chunks into complete Annex-B NAL units.
class VideoStreamAssembler {
public:
    struct Stats {
        uint64_t packets_in = 0;      ///< Input UDP packets processed
        uint64_t bytes_in = 0;        ///< Input bytes processed
        uint64_t nal_units_out = 0;   ///< Complete NAL units emitted
        uint64_t parse_resyncs = 0;   ///< Times assembler had to resync/truncate buffer
        size_t buffered_bytes = 0;    ///< Bytes currently kept for next packet
    };

    using NalCallback = std::function<void(const std::vector<uint8_t>& nal)>;

    VideoStreamAssembler();

    /// Feed one UDP payload chunk to the assembler.
    void pushPacket(const uint8_t* data, size_t size);

    /// Convenience overload for vector packets.
    void pushPacket(const std::vector<uint8_t>& packet);

    /// Register callback called for each complete NAL unit.
    void setNalCallback(NalCallback callback);

    /// Reset parser state and counters.
    void reset();

    /// Get current parser statistics.
    Stats getStats() const;

private:
    static bool findStartCode(
        const std::vector<uint8_t>& data,
        size_t from,
        size_t& start_index,
        size_t& start_code_len);

    void emitNal(const uint8_t* begin, size_t size);

    mutable std::mutex mutex_;
    std::vector<uint8_t> buffer_;
    NalCallback nal_callback_;
    Stats stats_;

    static constexpr size_t kMaxBufferedBytes = 1024 * 1024;
};

} // namespace tello

#endif // TELLO_VIDEO_STREAM_ASSEMBLER_HPP
