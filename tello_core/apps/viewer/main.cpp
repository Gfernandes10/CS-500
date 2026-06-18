#include "tello/tello_client.hpp"
#include "tello/video_decoder_ffmpeg.hpp"
#include "tello/video_receiver.hpp"
#include "tello/video_stream_assembler.hpp"

#ifdef TELLO_HAS_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#endif

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_keep_running{true};

void handleSignal(int signal) {
    (void)signal;
    g_keep_running = false;
}

int32_t parseIntArg(const char* value, int32_t fallback) {
    if (value == nullptr) {
        return fallback;
    }
    try {
        return std::stoi(std::string(value));
    } catch (...) {
        return fallback;
    }
}

std::string sanitizeCsvField(const std::string& input) {
    std::string out = input;
    for (char& c : out) {
        if (c == ',' || c == '\n' || c == '\r') {
            c = ';';
        }
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    int32_t interval_ms = 1000;
    int32_t duration_s = 0;
    std::string metrics_csv_path;
    std::string test_id;
    std::string scenario;
    std::string notes;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--interval-ms" && i + 1 < argc) {
            interval_ms = parseIntArg(argv[++i], interval_ms);
        } else if (arg == "--duration-s" && i + 1 < argc) {
            duration_s = parseIntArg(argv[++i], duration_s);
        } else if (arg == "--metrics-csv" && i + 1 < argc) {
            metrics_csv_path = argv[++i];
        } else if (arg == "--test-id" && i + 1 < argc) {
            test_id = argv[++i];
        } else if (arg == "--scenario" && i + 1 < argc) {
            scenario = argv[++i];
        } else if (arg == "--notes" && i + 1 < argc) {
            notes = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: tello_viewer [--interval-ms N] [--duration-s N] [--metrics-csv PATH] [--test-id ID] [--scenario NAME] [--notes TEXT]" << std::endl;
            return 0;
        }
    }

    const std::string test_id_csv = sanitizeCsvField(test_id);
    const std::string scenario_csv = sanitizeCsvField(scenario);
    const std::string notes_csv = sanitizeCsvField(notes);
    const std::string run_mode_csv = "VIEWER";

    if (interval_ms <= 0) {
        interval_ms = 1000;
    }
    const bool has_duration_limit = duration_s > 0;

    std::ofstream metrics_csv;
    const bool metrics_enabled = !metrics_csv_path.empty();
    if (metrics_enabled) {
        metrics_csv.open(metrics_csv_path, std::ios::out | std::ios::trunc);
        if (!metrics_csv.is_open()) {
            std::cerr << "[tello_viewer] failed to open metrics csv: " << metrics_csv_path << std::endl;
            return 2;
        }
        metrics_csv
            << "test_id,scenario,notes,run_mode,elapsed_ms,packets_total,nal_units,frames_decoded,keyframes,decode_fps_ema,decode_errors,width,height,paused,overlay_enabled"
            << std::endl;
        std::cout << "[tello_viewer] metrics_csv: " << metrics_csv_path << std::endl;
    }

    std::signal(SIGINT, handleSignal);

#ifndef TELLO_HAS_FFMPEG
    std::cerr << "[tello_viewer] FFmpeg backend not built. Reconfigure with TELLO_ENABLE_FFMPEG=ON." << std::endl;
    return 2;
#else
    tello::TelloClient client;
    const tello::ResponseCode init_rc = client.initialize("192.168.10.1", 8889, 9000);
    std::cout << "[tello_viewer] initialize: " << static_cast<int>(init_rc) << std::endl;
    if (init_rc != tello::ResponseCode::OK) {
        return 1;
    }

    const tello::ResponseCode sdk_rc = client.enterSdkMode();
    std::cout << "[tello_viewer] enterSdkMode: " << static_cast<int>(sdk_rc) << std::endl;
    if (sdk_rc != tello::ResponseCode::OK) {
        client.shutdown();
        return 1;
    }

    const tello::ResponseCode stream_on_rc = client.streamOn();
    std::cout << "[tello_viewer] streamOn: " << static_cast<int>(stream_on_rc) << std::endl;
    if (stream_on_rc != tello::ResponseCode::OK) {
        client.shutdown();
        return 1;
    }

    tello::VideoDecoderFfmpeg decoder;
    if (!decoder.initialize()) {
        std::cerr << "[tello_viewer] decoder initialize failed" << std::endl;
        (void)client.streamOff();
        client.shutdown();
        return 1;
    }

    std::atomic<uint64_t> callback_frames{0};
    std::atomic<uint64_t> callback_keyframes{0};
    std::atomic<int32_t> callback_width{0};
    std::atomic<int32_t> callback_height{0};
    std::vector<uint8_t> latest_luma;
    std::vector<uint8_t> latest_bgr;
    int32_t latest_width = 0;
    int32_t latest_height = 0;
    int32_t latest_bgr_stride = 0;
    bool latest_has_color = false;
    std::mutex latest_frame_mutex;

#ifdef TELLO_HAS_OPENCV
    cv::namedWindow("tello_viewer", cv::WINDOW_AUTOSIZE);
    std::cout << "[tello_viewer] render_window: enabled (OpenCV)" << std::endl;
    std::cout << "[tello_viewer] controls: Q/ESC=quit, P=pause, S=snapshot, O=overlay" << std::endl;
#else
    std::cout << "[tello_viewer] render_window: disabled (OpenCV not built)" << std::endl;
#endif

    decoder.setFrameCallback([&](const tello::VideoDecoderFfmpeg::DecodedFrameInfo& frame_info) {
        ++callback_frames;
        if (frame_info.is_key_frame) {
            ++callback_keyframes;
        }
        callback_width = frame_info.width;
        callback_height = frame_info.height;
    });

    decoder.setFrameDataCallback([&](const tello::VideoDecoderFfmpeg::DecodedFrame& frame) {
        if (frame.luma.empty() || frame.info.width <= 0 || frame.info.height <= 0) {
            return;
        }

        std::lock_guard<std::mutex> lock(latest_frame_mutex);
        latest_luma = frame.luma;
        latest_bgr = frame.bgr;
        latest_width = frame.info.width;
        latest_height = frame.info.height;
        latest_bgr_stride = frame.bgr_stride;
        latest_has_color = !frame.bgr.empty() && frame.bgr_stride > 0;
    });

    tello::VideoStreamAssembler assembler;
    std::atomic<bool> decoder_seen_sps{false};
    std::atomic<bool> decoder_seen_pps{false};
    std::atomic<bool> decoder_synced{false};

    assembler.setNalCallback([&](const std::vector<uint8_t>& nal) {
        if (nal.empty()) {
            return;
        }

        const uint8_t nal_type = static_cast<uint8_t>(nal[0] & 0x1F);
        if (nal_type == 7) {
            decoder_seen_sps = true;
        } else if (nal_type == 8) {
            decoder_seen_pps = true;
        } else if (nal_type == 5 && decoder_seen_sps.load() && decoder_seen_pps.load()) {
            decoder_synced = true;
        }

        const bool allow_decode = (nal_type == 7 || nal_type == 8 || nal_type == 5 || decoder_synced.load());
        if (allow_decode) {
            (void)decoder.decodeNal(nal);
        }
    });

    tello::VideoReceiver receiver;
    receiver.setPacketCallback([&](const std::vector<uint8_t>& packet) {
        assembler.pushPacket(packet);
    });

    const tello::ResponseCode recv_rc = receiver.start("0.0.0.0", 11111, 500);
    std::cout << "[tello_viewer] video_receiver.start: " << static_cast<int>(recv_rc) << std::endl;
    if (recv_rc != tello::ResponseCode::OK) {
        (void)client.streamOff();
        client.shutdown();
        return 1;
    }

    const auto started_at = std::chrono::steady_clock::now();
    auto last_log_tp = started_at;
#ifdef TELLO_HAS_OPENCV
    bool paused = false;
    bool overlay_enabled = true;
    cv::Mat paused_frame;
    uint64_t snapshot_counter = 0;
#endif
    while (g_keep_running.load()) {
        if (has_duration_limit) {
            const auto elapsed_s = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - started_at).count();
            if (elapsed_s >= duration_s) {
                break;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(30));

        const tello::VideoReceiver::VideoStats rx_stats = receiver.getVideoStats();
        const tello::VideoStreamAssembler::Stats parser_stats = assembler.getStats();
        const tello::VideoDecoderFfmpeg::Stats dec_stats = decoder.getStats();

#ifdef TELLO_HAS_OPENCV
        {
            std::vector<uint8_t> local_luma;
            std::vector<uint8_t> local_bgr;
            int32_t local_width = 0;
            int32_t local_height = 0;
            int32_t local_bgr_stride = 0;
            bool local_has_color = false;

            std::lock_guard<std::mutex> lock(latest_frame_mutex);
            local_luma = latest_luma;
            local_bgr = latest_bgr;
            local_width = latest_width;
            local_height = latest_height;
            local_bgr_stride = latest_bgr_stride;
            local_has_color = latest_has_color;

            cv::Mat display;
            if (!paused) {
                if (local_has_color && !local_bgr.empty() && local_width > 0 && local_height > 0) {
                    cv::Mat color(local_height, local_width, CV_8UC3, local_bgr.data(), local_bgr_stride);
                    display = color.clone();
                } else if (!local_luma.empty() && local_width > 0 && local_height > 0) {
                    cv::Mat luma(local_height, local_width, CV_8UC1, local_luma.data());
                    cv::cvtColor(luma, display, cv::COLOR_GRAY2BGR);
                }

                if (!display.empty()) {
                    paused_frame = display.clone();
                }
            } else if (!paused_frame.empty()) {
                display = paused_frame.clone();
            }

            if (!display.empty()) {
                if (overlay_enabled) {
                    const cv::Scalar text_color(0, 255, 0);
                    const cv::Scalar shadow_color(0, 0, 0);
                    const int font = cv::FONT_HERSHEY_SIMPLEX;
                    const double scale = 0.6;
                    const int thickness = 1;

                    const std::string line1 = "fps=" + std::to_string(dec_stats.decode_fps_ema)
                        + " frames=" + std::to_string(dec_stats.frames_decoded)
                        + " keyframes=" + std::to_string(callback_keyframes.load());
                    const std::string line2 = "packets=" + std::to_string(rx_stats.packets_total)
                        + " nal=" + std::to_string(parser_stats.nal_units_out)
                        + " dec_errors=" + std::to_string(dec_stats.decode_errors);
                    const std::string line3 = std::string("state=") + (paused ? "PAUSED" : "LIVE")
                        + " controls: Q/ESC quit, P pause, S snapshot, O overlay";

                    cv::putText(display, line1, cv::Point(10, 24), font, scale, shadow_color, thickness + 2, cv::LINE_AA);
                    cv::putText(display, line1, cv::Point(10, 24), font, scale, text_color, thickness, cv::LINE_AA);
                    cv::putText(display, line2, cv::Point(10, 48), font, scale, shadow_color, thickness + 2, cv::LINE_AA);
                    cv::putText(display, line2, cv::Point(10, 48), font, scale, text_color, thickness, cv::LINE_AA);
                    cv::putText(display, line3, cv::Point(10, 72), font, scale, shadow_color, thickness + 2, cv::LINE_AA);
                    cv::putText(display, line3, cv::Point(10, 72), font, scale, text_color, thickness, cv::LINE_AA);
                }

                cv::imshow("tello_viewer", display);
            }
        }

        const int key = cv::waitKey(1);
        if (key == 27 || key == 'q' || key == 'Q') {
            g_keep_running = false;
            break;
        }
        if (key == 'p' || key == 'P') {
            paused = !paused;
            std::cout << "[tello_viewer] paused=" << (paused ? "true" : "false") << std::endl;
        }
        if (key == 'o' || key == 'O') {
            overlay_enabled = !overlay_enabled;
            std::cout << "[tello_viewer] overlay=" << (overlay_enabled ? "on" : "off") << std::endl;
        }
        if (key == 's' || key == 'S') {
            if (!paused_frame.empty()) {
                const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                const std::string filename = "tello_snapshot_" + std::to_string(now_ms)
                    + "_" + std::to_string(snapshot_counter++) + ".png";
                const bool saved = cv::imwrite(filename, paused_frame);
                std::cout << "[tello_viewer] snapshot=" << (saved ? filename : "save_failed") << std::endl;
            } else {
                std::cout << "[tello_viewer] snapshot skipped (no frame available)" << std::endl;
            }
        }
#endif

        const auto now = std::chrono::steady_clock::now();
        const auto log_elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last_log_tp).count();
        if (log_elapsed_ms < interval_ms) {
            continue;
        }
        last_log_tp = now;

    #ifdef TELLO_HAS_OPENCV
        const int paused_int = paused ? 1 : 0;
        const int overlay_int = overlay_enabled ? 1 : 0;
    #else
        const int paused_int = 0;
        const int overlay_int = 0;
    #endif

        std::cout << "[tello_viewer]"
                  << " packets=" << rx_stats.packets_total
                  << " nal=" << parser_stats.nal_units_out
                  << " frames=" << dec_stats.frames_decoded
                  << " keyframes=" << callback_keyframes.load()
                  << " fps=" << dec_stats.decode_fps_ema
                  << " size=" << callback_width.load() << "x" << callback_height.load()
                  << " dec_errors=" << dec_stats.decode_errors
                  << std::endl;

        if (metrics_enabled) {
            const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - started_at).count();
            metrics_csv
                << test_id_csv << ","
                << scenario_csv << ","
                << notes_csv << ","
                << run_mode_csv << ","
                << elapsed_ms << ","
                << rx_stats.packets_total << ","
                << parser_stats.nal_units_out << ","
                << dec_stats.frames_decoded << ","
                << callback_keyframes.load() << ","
                << dec_stats.decode_fps_ema << ","
                << dec_stats.decode_errors << ","
                << callback_width.load() << ","
                << callback_height.load() << ","
                << paused_int << ","
                << overlay_int
                << std::endl;
        }
    }

#ifdef TELLO_HAS_OPENCV
    cv::destroyWindow("tello_viewer");
#endif

    receiver.stop();
    const tello::ResponseCode stream_off_rc = client.streamOff();
    std::cout << "[tello_viewer] streamOff: " << static_cast<int>(stream_off_rc) << std::endl;
    client.shutdown();
    return 0;
#endif
}
