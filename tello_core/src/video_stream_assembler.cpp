#include "tello/video_stream_assembler.hpp"

#include <algorithm>

namespace tello {

VideoStreamAssembler::VideoStreamAssembler()
    : mutex_(),
      buffer_(),
      nal_callback_(nullptr),
      stats_() {
    buffer_.reserve(64 * 1024);
}

void VideoStreamAssembler::pushPacket(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return;
    }

    std::vector<std::vector<uint8_t>> emitted_nals;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        ++stats_.packets_in;
        stats_.bytes_in += static_cast<uint64_t>(size);

        buffer_.insert(buffer_.end(), data, data + size);

        while (true) {
            size_t first_start = 0;
            size_t first_len = 0;
            if (!findStartCode(buffer_, 0, first_start, first_len)) {
                if (buffer_.size() > kMaxBufferedBytes) {
                    // Keep tail bytes to allow detection of split start code across packets.
                    const size_t keep = std::min<size_t>(buffer_.size(), 4);
                    std::vector<uint8_t> tail(buffer_.end() - keep, buffer_.end());
                    buffer_.swap(tail);
                    ++stats_.parse_resyncs;
                }
                break;
            }

            if (first_start > 0) {
                buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(first_start));
                ++stats_.parse_resyncs;
                continue;
            }

            size_t second_start = 0;
            size_t second_len = 0;
            if (!findStartCode(buffer_, first_len, second_start, second_len)) {
                break;
            }

            const size_t nal_begin = first_len;
            const size_t nal_size = second_start - nal_begin;
            if (nal_size > 0) {
                emitted_nals.emplace_back(buffer_.begin() + static_cast<std::ptrdiff_t>(nal_begin),
                                          buffer_.begin() + static_cast<std::ptrdiff_t>(second_start));
                ++stats_.nal_units_out;
            }

            buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(second_start));
        }

        stats_.buffered_bytes = buffer_.size();
    }

    for (const auto& nal : emitted_nals) {
        emitNal(nal.data(), nal.size());
    }
}

void VideoStreamAssembler::pushPacket(const std::vector<uint8_t>& packet) {
    if (packet.empty()) {
        return;
    }
    pushPacket(packet.data(), packet.size());
}

void VideoStreamAssembler::setNalCallback(NalCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    nal_callback_ = std::move(callback);
}

void VideoStreamAssembler::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.clear();
    stats_ = Stats{};
}

VideoStreamAssembler::Stats VideoStreamAssembler::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Stats snapshot = stats_;
    snapshot.buffered_bytes = buffer_.size();
    return snapshot;
}

bool VideoStreamAssembler::findStartCode(
    const std::vector<uint8_t>& data,
    size_t from,
    size_t& start_index,
    size_t& start_code_len) {
    if (data.size() < 3 || from >= data.size()) {
        return false;
    }

    for (size_t i = from; i + 3 <= data.size(); ++i) {
        if (i + 4 <= data.size() &&
            data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x00 && data[i + 3] == 0x01) {
            start_index = i;
            start_code_len = 4;
            return true;
        }
        if (data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x01) {
            start_index = i;
            start_code_len = 3;
            return true;
        }
    }

    return false;
}

void VideoStreamAssembler::emitNal(const uint8_t* begin, size_t size) {
    NalCallback callback_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback_copy = nal_callback_;
    }

    if (!callback_copy || begin == nullptr || size == 0) {
        return;
    }

    std::vector<uint8_t> nal(begin, begin + size);
    callback_copy(nal);
}

} // namespace tello
