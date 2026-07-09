# Phase D - Video Channel

## Objective

Implement and validate a robust DJI Tello video channel using a single FFmpeg Stream pipeline. The goal is to provide smooth live viewing, frame-level metrics, and recovery behavior through one maintained video runtime path.

## Scope Implemented

1. SDK video activation through `streamon` and shutdown through `streamoff`.
2. FFmpeg-based UDP stream reader on local port `11111`.
3. H264 demuxing, parsing, decoding, and frame timing handled by FFmpeg.
4. RGB frame delivery to the Qt Control Panel.
5. Periodic video metrics export through the central metrics collector.
6. Stream stall detection and recovery using the reusable `TelloClient` recovery flow.
7. Power-cycle recovery support through SDK re-entry followed by `streamon`.
8. UI controls for start, stop, pause, overlay, and snapshot.

## Active Video Architecture

The active runtime path is:

1. The host enters SDK mode with `command`.
2. The host sends `streamon`.
3. The drone sends H264 video over UDP port `11111`.
4. `VideoStreamReaderFfmpeg` opens:

```text
udp://@0.0.0.0:11111?overrun_nonfatal=1&fifo_size=5000000
```

5. FFmpeg reads the UDP stream with `libavformat`.
6. FFmpeg decodes H264 frames with `libavcodec`.
7. Frames are converted to RGB with `libswscale`.
8. The Control Panel copies the latest frame into a thread-safe image buffer.
9. The Qt UI timer renders the most recent frame and records display metrics.

This design keeps the fragile video timing and H264 parsing work inside FFmpeg, which is better suited to handle packet timing variation and decoder synchronization than a custom application-level parser.

## Why FFmpeg Stream

Flight testing showed that smooth live video is more important than exposing every low-level video packet boundary in the main runtime. FFmpeg Stream produced substantially better visual behavior in the Control Panel, especially when the drone was airborne and the GUI was also drawing telemetry and accepting keyboard control.

The project therefore treats FFmpeg Stream as the only supported video implementation. This keeps the CLI, Control Panel, experiments, and report aligned: when video is enabled, the measured behavior is always the FFmpeg Stream path.

## Metrics

The video pipeline records:

1. packets read;
2. bytes read;
3. latest frame age;
4. decoded frame count;
5. decode FPS exponential moving average;
6. decode error count;
7. frame width and height;
8. keyframe count;
9. UI frame conversion, drop, and display counters;
10. stream recovery stage and result.

The metrics collector preserves the existing CSV column names for compatibility, but the values are now populated from FFmpeg Stream statistics. In this context, video quality means stream freshness and continuity, not subjective image sharpness.

## Dependencies

Video support requires FFmpeg development packages:

1. `pkg-config`
2. `libavformat`
3. `libavcodec`
4. `libavutil`
5. `libswscale`

On Ubuntu/Debian:

```bash
sudo apt update
sudo apt install -y pkg-config libavformat-dev libavcodec-dev libavutil-dev libswscale-dev
```

The CMake configuration requires FFmpeg for video builds. If FFmpeg is missing, configuration fails instead of silently falling back to another video path.

## Validation Procedure

1. Build the project.
2. Connect to the Tello Wi-Fi network.
3. Run a bounded CLI video watch:

```bash
./build/tello_cli --video-watch --interval-ms 1000 --duration-s 30
```

4. Confirm the CLI reports `video_backend: FFmpeg Stream`.
5. Confirm decoded frames, frame size, decode FPS, and keyframes increase.
6. Open the Control Panel, connect to SDK mode, and press `Start Vision` if video is not already running.
7. Confirm the video view updates smoothly and the Vision section reports FFmpeg stream statistics.
8. For recovery validation, interrupt the stream or power-cycle the drone and confirm recovery stages are logged.

## Current Results Summary

1. FFmpeg Stream is the sole active video runtime path.
2. CLI and Control Panel video experiments use the same backend.
3. Real-drone experiments showed decode FPS around 31-34 FPS in successful runs.
4. The Control Panel can display live video while logging telemetry, GUI timing, and keyboard RC commands.
5. Power-cycle recovery can re-enter SDK mode and restart the stream when the drone becomes reachable again.

## Residual Risks

1. UDP video remains sensitive to Wi-Fi quality, laptop wireless drivers, and environmental interference.
2. FFmpeg can recover from many timing irregularities, but severe packet loss may still produce decode errors or stale frames.
3. Live viewing quality is affected by both transport freshness and GUI rendering load.
4. The current system is validated for manual operation and experiment logging; future closed-loop control should treat stale video as unavailable rather than trying to control from delayed imagery.
