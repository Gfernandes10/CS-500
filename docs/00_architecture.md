# Architecture Overview

## Goal

Describe the current architecture of the DJI Tello C++ stack. The project is no longer a stub-only skeleton; it is a reusable core library with CLI tools, a Qt Control Panel, metrics/export support, FFmpeg video, and ROS2 integration through a separate runtime workspace.

## System Boundary

The DJI Tello communicates over three UDP channels:

1. command and response on `8889`;
2. telemetry state packets on `8890`;
3. H264 video stream on `11111`.

The `tello_core` library owns the drone-specific SDK behavior. Applications should depend on `tello_core` rather than reimplementing command, telemetry, video, metrics, keepalive, or recovery logic.

## High-Level Modules

### Core Command Path

- `UdpSocket`: low-level UDP send/receive abstraction.
- `CommandExecutor`: per-command send/receive policy, timeout handling, retries, and attempt logs.
- `TelloClient`: high-level SDK lifecycle, command facade, keepalive, critical-command timeout policy, video recovery, and connection state.

### Telemetry

- `StateParser`: parses raw SDK state strings into `TelloState`.
- `StateReceiver`: receives telemetry on UDP `8890`, stores latest state, maintains a temporary history buffer, records state/RC samples, and exports state CSV files.

### Video

- `VideoStreamReaderFfmpeg`: the only active video runtime path.
- FFmpeg opens the Tello UDP stream, parses H264, decodes frames, and provides RGB frames plus stream/decode statistics.
- Video metrics, CLI diagnostics, and Control Panel viewing all use this same FFmpeg Stream path.

### Metrics and Logging

- `MetricsCollector`: central CSV schema for command, telemetry, video, GUI, recovery, keepalive, RC, and aggregate link-quality metrics.
- `Logger` / `ConsoleLogger`: pluggable logging abstraction.
- The Qt Control Panel records user-visible log rows into the metrics CSV so command, GUI, telemetry, and recovery events can be correlated after a run.

### Applications

- `tello_cli`: smoke, command-watch, telemetry-watch, and video-watch diagnostics.
- `tello_control_panel`: Qt graphical application for connection, command execution, live FFmpeg video, telemetry plotting, CSV export, keyboard RC profiles, and operation diagnostics.

### ROS2 Integration

ROS2 support is delivered through a separate workspace. The ROS packages consume an installed or extracted `tello_core` runtime artifact instead of copying the academic workspace. In ROS mode, the ROS driver owns the command path and the Control Panel communicates through ROS topics/services.

## Folder-Level Architecture

- `tello_core/include/tello/`
  - Public headers and downstream API contracts.
- `tello_core/src/`
  - Core library implementation.
- `tello_core/apps/cli/`
  - CLI diagnostic tool.
- `tello_core/apps/control_panel/`
  - Qt/OpenCV Control Panel implementation.
- `tello_core/tests/`
  - Offline unit tests and optional hardware tests.
- `docs/`
  - Technical documentation and final report material.
- `notebooks/`
  - Experiment analysis notebooks and generated figures.
- `results/`
  - Real-drone experiment CSV files and diagnostic captures.

## Dependency Direction

- CLI and Control Panel depend on `tello_core`.
- ROS2 driver depends on the packaged/exported `tello_core` artifact.
- `tello_core` depends on the C++ standard library, threads, and FFmpeg for video.
- Qt is required for the primary graphical Control Panel.
- OpenCV is only a fallback display dependency when Qt Widgets are unavailable; it is not the primary video runtime path.

Inside `tello_core`:

- `TelloClient` depends on `UdpSocket` and `CommandExecutor`.
- `StateReceiver` depends on `UdpSocket` and `StateParser`.
- `VideoStreamReaderFfmpeg` depends on FFmpeg libraries.
- `MetricsCollector` consumes snapshots from command, telemetry, video, GUI, recovery, keepalive, and RC paths.

## Current Status

Implemented:

1. UDP command channel with retry/timeout handling.
2. SDK mode entry, read commands, flight commands, stream commands, keepalive, and recovery.
3. Telemetry parser, receiver, latest-state cache, history buffer, state recording, RC recording, and CSV export.
4. FFmpeg Stream video path for CLI and Control Panel.
5. Central metrics schema with experiment metadata and aggregate link quality.
6. Qt Control Panel with Config/Operation tabs, logging/export, live video, telemetry plot, keyboard RC profiles, RC worker, and asynchronous critical command worker.
7. ROS2 integration through a separate workspace and release-artifact consumption model.
8. Offline tests for state parsing and metrics collector behavior.

Remaining future work:

1. Additional offline tests for command retry/failure-injection paths.
2. Closed-loop controller validation using `/tello/link_quality`.
3. Extended ROS hardware validation beyond the current build/interface/launch evidence.
