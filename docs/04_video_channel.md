# Phase D - Video Channel

## Objective
Implement and validate a robust video pipeline for DJI Tello, from UDP packet reception to H264 decode observability.

## Scope Implemented
1. UDP video receiver on local port 11111.
2. Stream assembly layer for H264 Annex-B NAL extraction.
3. NAL classification observability (SPS, PPS, IDR, non-IDR, other).
4. Optional FFmpeg decode backend with runtime decode metrics.
5. Stream recovery flow after stall and power-cycle scenarios.
6. Minimal viewer app consuming decoded frame callbacks (`tello_viewer`).
7. Real-time viewer window rendering path using OpenCV (grayscale luma visualization).
8. Color rendering path using swscale conversion to BGR24 before OpenCV display (with grayscale fallback if color buffer is unavailable).
9. Viewer interaction layer: on-screen metrics overlay and keyboard controls (`Q/ESC`, `P`, `S`).

## External Dependencies

### Runtime and build dependencies for video decode
1. pkg-config
2. libavcodec (development package)
3. libavutil (development package)
4. libswscale (development package)

On Ubuntu/Debian:
1. sudo apt update
2. sudo apt install -y pkg-config libavcodec-dev libavutil-dev libswscale-dev

## Dependency Management Policy

### Professional policy adopted in this project
1. FFmpeg is treated as a build environment dependency, not vendored into this repository.
2. Development mode allows building without FFmpeg to keep onboarding simple.
3. CI/release mode can require FFmpeg and fail configure immediately if missing.

### CMake switches
1. TELLO_ENABLE_FFMPEG
- Default: ON
- Meaning: tries to enable FFmpeg decode backend.

2. TELLO_REQUIRE_FFMPEG
- Default: OFF
- Meaning: if ON, CMake fails when FFmpeg is unavailable.

### Recommended usage
1. Developer local build (flexible):
- cmake -S . -B build -DTELLO_ENABLE_FFMPEG=ON -DTELLO_REQUIRE_FFMPEG=OFF

2. CI/release build (strict):
- cmake -S . -B build -DTELLO_ENABLE_FFMPEG=ON -DTELLO_REQUIRE_FFMPEG=ON

3. Build without decoder (transport/parser only):
- cmake -S . -B build -DTELLO_ENABLE_FFMPEG=OFF

## Validation Procedure
1. Build and run bounded video watch to avoid infinite monitoring:
- ./build/tello_cli --video-watch --interval-ms 1000 --duration-s 5
2. Confirm packet/NAL/decode counters are updating.
3. Run robustness checks (streamoff/streamon, Wi-Fi reconnect, power-cycle).
4. Run bounded minimal viewer:
- ./build/tello_viewer --interval-ms 1000 --duration-s 3

Expected viewer signals:
1. frame size reported (for Tello default stream expected around 960x720)
2. decode FPS stabilizing around stream FPS
3. keyframe counter increasing over time
4. OpenCV window shows decoded frames and accepts `q`/`ESC` for early exit
5. `P` toggles pause/live state and `S` saves a PNG snapshot from the current displayed frame

## Current Results Summary
1. Packet reception and NAL assembly are stable.
2. Recovery after power-cycle is working with hard fallback path in TelloClient.
3. FFmpeg decode backend is integrated and reports decode frame counters and decode FPS.
4. Strict build mode validated with FFmpeg required (`TELLO_REQUIRE_FFMPEG=ON`).
5. Decoder sync-gating added (SPS/PPS/IDR-aware gate before unrestricted slice decode), reducing startup/resume decode error bursts.
6. Minimal viewer runtime validated with decoded frames (`960x720`), keyframe progression, and stable decode FPS (~31 in observed run).
7. Viewer rendering path validated with OpenCV-enabled build configuration.
8. Viewer color path validated (BGR conversion in decoder + OpenCV display in viewer).
9. Viewer controls and overlay validated during bounded runs.

## Current Residual Risks
1. Decode error counter can still increase under severe packet loss, even with sync-gating.
2. Viewer is currently focused on technical validation, not end-user UX polish.

## Next Step
1. Start Phase E metrics export for experiments (CSV/JSON for decode FPS, keyframe cadence, and recovery events).
2. Define repeatable experimental protocol (latency, packet continuity, decode stability across scenarios).
