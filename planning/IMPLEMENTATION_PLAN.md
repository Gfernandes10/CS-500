# Didactic Implementation Plan - C/C++ Library for DJI Tello (CS500)

## Objective of this plan
Guide the construction of the project step by step, with **you writing all code** while I provide clear, didactic instructions. We will first prepare the foundation: folder structure, main files with empty function stubs, and documentation of technical decisions at each phase. **I will never provide complete implementations—only tell you exactly what to write.**

## Tello 2.0 SDK Assumptions (architectural base)
- Command/response channel: UDP `192.168.10.1:8889` (send `command` before other commands).
- Telemetry (state) channel: UDP local `0.0.0.0:8890`.
- Video channel: UDP local `0.0.0.0:11111` (enabled by `streamon`).
- Safety: if no command for 15 seconds, Tello auto-lands.

## General execution strategy
1. Define minimal, testable architecture.
2. Create public API first (clear headers), keeping implementations empty.
3. Implement incrementally: command → telemetry → video → metrics → ROS → GUI.
4. Document each decision and trade-off as we progress.

## Suggested initial structure
```text
.
├── CMakeLists.txt
├── cmake/
│   └── CompilerOptions.cmake
├── docs/
│   ├── README.md
│   ├── 00_architecture.md
│   ├── 01_setup_and_build.md
│   ├── 02_command_channel.md
│   ├── 03_telemetry.md
│   ├── 04_video.md
│   ├── 05_metrics.md
│   ├── 06_ros_integration.md
│   ├── 07_gui.md
│   ├── 08_experiments.md
│   └── decision-log/
│       └── ADR-000-template.md
├── include/tello/
│   ├── udp_socket.hpp
│   ├── tello_client.hpp
│   ├── command_executor.hpp
│   ├── state_parser.hpp
│   ├── state_receiver.hpp
│   ├── video_receiver.hpp
│   ├── metrics.hpp
│   ├── logger.hpp
│   └── types.hpp
├── src/
│   ├── udp_socket.cpp
│   ├── tello_client.cpp
│   ├── command_executor.cpp
│   ├── state_parser.cpp
│   ├── state_receiver.cpp
│   ├── video_receiver.cpp
│   ├── metrics.cpp
│   └── logger.cpp
├── apps/
│   ├── cli/
│   │   └── main.cpp
│   └── viewer/
│       └── main.cpp
├── ros/
│   └── tello_ros2_bridge/ (future phase)
└── tests/
    ├── test_state_parser.cpp
    ├── test_command_executor.cpp
    └── test_integration_sockets.cpp
```

## Plan by phases (didactic)

### Phase A - Foundation (Week 1)
**Goal:** get the project compiling with stubs and clear architecture.
- Create main CMake + `tello_core` library.
- Create headers with contracts (signatures) and intention comments.
- Create `.cpp` with empty implementations returning controlled error (`TODO`/`not implemented`).
- Create `apps/cli/main.cpp` that initializes objects without executing flight.
- Create `docs/00_architecture.md` and register 1st version of architecture.

**Expected output:** clean build with minimal functionality and structure ready to evolve.

### Phase B - Command channel (Weeks 2-3)
**Goal:** send commands and receive `ok/error` reliably.
- Implement `UdpSocket` (open/bind/send/recv/close).
- Implement `CommandExecutor` with configurable timeout/retry.
- Implement `TelloClient::enterSdkMode()` (sends `command`).
- Add logs of command sent/response received.
- Write basic tests for timeout and response parsing.

**Expected output:** CLI can send `command`, `battery?`, `takeoff/land` (only when appropriate in real test).

### Phase C - Telemetry (Weeks 4-5)
**Goal:** receive and parse `state` into typed thread-safe structure.
- Create `TelloState` in `types.hpp`.
- Implement robust parser of string `k:v;` with validation.
- Implement receiver in dedicated thread (`state_receiver`).
- Expose safe read API (`getLatestState`).
- Extensive unit tests for parser.

**Expected output:** live state updates with safe access for other layers.

### Phase D - Video (Weeks 6-7)
**Goal:** receive stream and make frames available for consumption.
- Implement `VideoReceiver` (UDP socket 11111).
- Integrate decoder (OpenCV/FFmpeg) in simple pipeline.
- Expose callback/polling for frame.
- Minimal viewer app with optional FPS counter.

**Expected output:** video visualization in simple window.

### Phase E - Metrics and evaluation (Weeks 8-9)
**Goal:** meet CS500 academic requirement for quantitative measurement.
- Measure command-response latency.
- Measure telemetry update rate (Hz).
- Measure video frame rate (FPS).
- Export structured logs (CSV/JSON).
- Document experimental protocol in `docs/08_experiments.md`.

**Expected output:** repeatable dataset for analysis in final report.

### Phase E Progress (In Progress)

**Date:** 2026-06-18

1. Initial metrics export implemented in `tello_viewer` with CSV output support via `--metrics-csv PATH`.
2. Export schema includes runtime observability fields: elapsed time, packets, NAL units, decoded frames, keyframes, decode FPS EMA, decode errors, frame size, paused state, and overlay state.
3. Bounded runtime validation passed and generated CSV rows successfully.
4. E2 implemented in `tello_cli --video-watch`: CSV export now includes video pipeline metrics plus recovery observability fields (`recovery_attempted`, `recovery_result`, `recovery_hard`, `event`, `cmd_state`).
5. Bounded runtime validation passed for CLI CSV export with sample rows generated.
6. E3 implemented in `tello_cli --once/--watch`: command latency CSV export added (`command`, `latency_ms`, result/response/state/event fields).
7. Bounded runtime validation passed for command-latency CSV with multi-row output.
8. E4 implemented: unified experiment metadata fields (`test_id`, `scenario`, `notes`, `run_mode`) added to viewer, CLI video-watch, and CLI command CSV exports.
9. E4 validation passed with bounded hardware runs and populated metadata rows in all current CSV modes.

### Phase F - ROS2 (Weeks 10-11)
**Goal:** integrate with robotics ecosystem.
- Create ROS2 package bridge to `tello_core`.
- Publish state/video topics.
- Subscribe to control commands and offer services (takeoff/land).
- Test with `ros2 topic echo` and visual tools.

**Expected output:** functional ROS2 flow demo without duplicating core logic.

### Phase G - Desktop GUI (Week 12)
**Goal:** demonstrator app for control and observation.
- Basic layout with video panel, telemetry display, and buttons.
- Integrate with existing core API.
- Avoid business logic in GUI.

**Expected output:** functional desktop client for final demo.

### Phase G Progress (Kickoff)

**Date:** 2026-06-18

1. Added new GUI app target `tello_control_panel` (OpenCV-based) for quick command testing.
2. MVP A interface implemented with clickable controls for:
- `Connect + SDK` (initialize + `command`)
- Read commands: `battery?`, `speed?`, `time?`, `wifi?`, `sdk?`, `sn?`
- Set command: `speed x` with bounded setpoint control (`10..100`).
3. Event log panel added in UI for command/response visibility with timestamps and connection state.
4. Build target validated successfully: `cmake --build build --target tello_control_panel`.
5. Control panel transitioned to Qt Widgets as primary GUI backend (Qt5/Qt6 auto-detect in CMake, OpenCV fallback retained).
6. Delivery B completed in Qt panel: all read commands (`battery?`, `speed?`, `time?`, `wifi?`, `sdk?`, `sn?`), `speed x` set command control, and raw command input for iterative testing.

### Phase H - Academic closure (Week 13)
**Goal:** consolidate documentation, results, and presentation.
- Finalize technical report.
- Consolidate graphs/tables.
- Prepare demo (live or recorded).

**Expected output:** final CS500 delivery package.

## Main files and roles (first set of stubs)
- `udp_socket.*`: encapsulates POSIX UDP operations.
- `command_executor.*`: sends commands, waits for response, applies retry/timeout.
- `tello_client.*`: high-level facade (enter sdk, takeoff, land, queries).
- `state_parser.*`: converts raw state string to struct.
- `state_receiver.*`: reception loop and thread-safe storage.
- `video_receiver.*`: stream reception and frame distribution.
- `metrics.*`: latency, Hz, FPS, and export.
- `logger.*`: logs with levels and timestamps.
- `types.hpp`: common structs and enums.

## Documentation pattern we will follow
For each completed phase, create/update:
1. Objective of the phase.
2. Technical decisions made.
3. What was implemented.
4. What was tested and result.
5. Known limitations.
6. Next steps.

Suggestion: record important decisions in short ADRs inside `docs/decision-log/`.

## Ready criteria per phase
- Build without errors.
- Phase tests passing.
- Phase document updated in `docs/`.
- Phase checklist completed.

## Foreseeable technical risks (and mitigation)
- UDP/Wi-Fi instability → retries, timeout, detailed logs.
- Fragile telemetry parser → tests with edge cases.
- Complexity of video too early → minimal pipeline first.
- Coupling with ROS/GUI → keep `tello_core` independent.

## Immediate practical sequence (next step)
1. Create folder skeleton and CMake.
2. Create core headers with contracts.
3. Create `.cpp` files with empty stubs.
4. Create `docs/` folder with registration templates.
5. Confirm Phase A checklist.

## How we will work together
- I guide you step by step with clear instructions.
- In each step: objective → what to create → why → how to validate.
- No heavy implementation before we lock down the interface.
- Whenever you want, we refine architecture before coding.

## Testing Strategy for Each Phase

### Phase A - Foundation (Mandatory checks)
1. **Build Compilation Test**
   - Root CMakeLists.txt compiles without errors.
   - All headers include correctly without circular dependencies.
   - All stub implementations compile successfully.

2. **Linking Test**
   - Library `tello_core` links without unresolved symbols.
   - CLI app links successfully with the library.

3. **Execution Test (cli app)**
   - CLI starts without crashing.
   - Prints initialization message.
   - Initializes stub objects without runtime errors.

4. **Documentation Test**
   - `docs/00_architecture.md` exists and describes all components.
   - Decision log template exists in `docs/decision-log/`.

### Before Each Subsequent Phase (B, C, D, E, F, G, H)
- **Unit tests** compile and pass.
- **Integration test** (CLI app) executes without crash.
- **Log output** shows expected behavior.
- **Documentation** updated with phase summary.

### Validation Workflow for Each Component
1. **You write** the code (header + stub implementation).
2. **Build check:** Compile with `cmake && make`.
3. **Runtime check:** Execute and verify output.
4. **Document:** Log results in phase document.

---

## Next: Confirmation and adjustments

Before we begin Phase A, confirm if:
1. This structure and sequencing makes sense to you?
2. Any technology choices you'd like to adjust (e.g., logging framework, C++ standard)?
3. Shall we start with **Phase A - Foundation** right away?

---

## Phase B Sign-off (Completed)

**Date:** 2026-06-16

### Status
- Phase B command channel implementation is functionally complete and validated in manual runtime scenarios.

### Completed Deliverables
1. UDP transport implementation (`open`, `bind`, `send`, `recv`, timeout mapping).
2. Command executor with retry, timeout, and response handling.
3. Tello client session recovery (SDK mode re-entry + channel recovery fallback).
4. Connection state/event model exposed by client API.
5. CLI runtime validation in two modes (`--once`, `--watch`).
6. Reliability policy exposed by client config API (`TelloClient::ReliabilityConfig`).

### Validation Summary
1. Baseline connected startup: passed.
2. Wi-Fi loss while running + reconnection without process restart: passed.
3. Startup without Wi-Fi + later reconnection: passed after recovery flow fixes.
4. Repeated execution stability: passed.

### Residual Risks
1. Signal stop responsiveness still depends on active network call windows.
2. Automated failure-injection tests are not yet implemented.

### Phase C Entry Gate
- ✅ Command channel considered ready for telemetry phase work.

---

## Phase C Progress (In Progress)

**Date:** 2026-06-18

### Implemented So Far
1. Telemetry parser implementation (`state_parser`) with malformed-frame handling.
2. State receiver thread implementation (`state_receiver`) with thread-safe latest-state snapshot.
3. Telemetry smoke apps for parser and receiver runtime validation.
4. Telemetry observability API (`rx_hz`, `age_ms`, packet counters/timeouts/errors).
5. Main CLI telemetry mode (`--state-watch`) integrated.

### Current Checklist
- [x] Parse valid telemetry frame into `TelloState`
- [x] Reject malformed telemetry frame with parse error
- [x] Receive state packets on UDP 8890 in background thread
- [x] Expose latest state safely to consumers
- [x] Expose telemetry receive statistics in API
- [x] Validate telemetry flow with smoke runtime
- [x] Add automated tests in project test pipeline (offline default + optional hardware)

### Remaining to Close Phase C
1. Expand telemetry-focused tests (beyond parser baseline).

---

## Phase C Sign-off (Completed)

**Date:** 2026-06-18

### Status
- Phase C telemetry ingestion is complete for current milestone scope.

### Completed Deliverables
1. Parser and receiver implementation for telemetry state channel.
2. Thread-safe latest-state access API.
3. Telemetry observability metrics (rate, age, packet counters).
4. CLI telemetry mode (`--state-watch`).
5. Default offline tests plus optional hardware test gating.

### Validation Summary
1. Build and offline CTest pipeline passed.
2. Parser smoke tests passed.
3. Runtime telemetry reception smoke passed with real stream.

### Residual Risks
1. Telemetry API remains pull-based only.
2. No in-memory history/ring buffer yet.

### Phase D Entry Gate
- ✅ Ready to start video channel implementation.

---

## Phase D - Step-by-Step Execution Guide (Didactic)

**Date:** 2026-06-18

### Step 1 - Validate current baseline (build + existing tests)
**What is being done:** confirm project health before expanding video scope.

**Why:** avoids mixing old regressions with new Phase D issues.

**Actions:**
1. Build the project.
2. Run offline test label to ensure baseline is stable.

**Validation commands:**
```bash
cmake --build build
ctest --test-dir build -L offline --output-on-failure
```

**Expected result:** build passes and offline tests stay green.

### Step 2 - D1 raw video transport (already started)
**What is being done:** receive UDP packets from local port `11111` in background thread.

**Why:** transport layer must be stable before any decode/render logic.

**Actions:**
1. Keep `VideoReceiver::start()` opening/binding socket.
2. Keep receive loop reading packets and counting traffic.
3. Keep `stop()` shutting down thread/socket cleanly.

**Validation target:**
1. No crash on start/stop cycles.
2. Packet counter grows while stream is active.

### Step 3 - Command orchestration for video (`command` + `streamon`)
**What is being done:** use CLI mode that enters SDK mode and enables stream automatically.

**Why:** Tello does not transmit video unless SDK mode is active and `streamon` is sent.

**Actions:**
1. Start CLI with `--video-watch`.
2. CLI sends `command`, then `streamon`, then starts video receiver.

**Validation command:**
```bash
./build/tello_cli --video-watch --interval-ms 1000 --duration-s 3
```

**Expected result:** periodic logs with non-zero `packets_total`, `delta`, and `pps`, followed by auto-stop at duration limit.

### Step 4 - Video observability metrics
**What is being done:** extend video stats beyond packet count.

**Why:** diagnostics are necessary to debug Wi-Fi instability and stream behavior.

**Planned metrics:**
1. `bytes_total`
2. `timeouts_total`
3. `errors_total`
4. `last_packet_age_ms`
5. `rx_pps_ema`

**Acceptance check:** metrics update in real time and reset correctly when requested.

### Phase D Progress Update (Current)
1. Step 1 completed: baseline build + offline tests passed.
2. Step 2 completed: raw UDP video receiver is running with clean start/stop lifecycle.
3. Step 3 completed: `--video-watch` now orchestrates `command` + `streamon` + receiver startup.
4. Step 3 safety improvement completed: `--duration-s` added to prevent indefinite monitoring loops.
5. Step 4 completed: metrics now include `packets_total`, `bytes_total`, `recv_timeouts`, `recv_errors`, `last_packet_age_ms`, and `rx_pps_ema`.
6. Step 5 completed: robustness scenarios validated, including successful recovery after power-cycle.
7. Step 6 started: D2 boundary module created (`VideoStreamAssembler`) to isolate H264 Annex-B NAL assembly from UDP transport.

### Step 5 - Robustness scenarios (manual runtime checks)
**What is being done:** stress stream continuity in realistic conditions.

**Why:** this project is network-sensitive; robustness matters more than happy-path only.

**Scenarios:**
1. Run 60+ seconds with stable Wi-Fi.
2. Send `streamoff`, wait, send `streamon` again.
3. Temporary Wi-Fi loss and reconnection.
4. Power-cycle Tello during `--video-watch` and verify stream recovery behavior.

**Acceptance check:** process remains alive and resumes counting packets when stream returns.

**Current finding:**
1. In a power-cycle test, packet flow did not resume automatically before mitigation (`packets_total` stopped, `age_ms` and `timeouts` kept increasing).
2. Mitigation implemented in `TelloClient`: detect sustained stall via caller signal and attempt `streamOn` recovery with cooldown.
3. Hard fallback added in `TelloClient` for power-cycle cases: bounded full command-session reinitialize (`initialize -> enterSdkMode -> streamOn` with backoff).
4. CLI keeps only orchestration/logging and now reports whether hard recovery path was used.
5. Post-mitigation validation passed: stream resumed after hard recovery path (`hard_recovery=yes`) and packet flow recovered (`packets_total` increased again and `age_ms` returned to low values).

### Step 6 - D2 boundary (prepare parser/decode split)
**What is being done:** define clear interface between UDP transport and future decode pipeline.

**Why:** keeps `tello_core` modular and prevents decode logic from polluting transport layer.

**Planned outcome:** separate module for packet-to-NAL/frame assembly; decoder integration stays isolated.

**Current implementation status:**
1. `VideoStreamAssembler` API created in `include/tello/video_stream_assembler.hpp`.
2. Minimal Annex-B start-code parser implemented in `src/video_stream_assembler.cpp`.
3. Module integrated in `tello_core` build target.
4. Step 6.1 completed: `VideoReceiver` packets are now wired to `VideoStreamAssembler` in CLI `--video-watch`.
5. Runtime validation passed: `nal_units` increased continuously during bounded run (`--duration-s 5`) with stable low `buffered_bytes` and no parser resync bursts.
6. Step 6.2 completed: NAL classification callback added in CLI (`SPS/PPS/IDR/non-IDR/other`) with live counters in `--video-watch` output.
7. Runtime validation passed: bounded run showed consistent GOP-like pattern (`nal_sps`, `nal_pps`, `nal_idr`, `nal_nonidr` increasing; `nal_other` remained 0 in observed run).
8. Decoder plug step completed: optional FFmpeg H264 decoder backend integrated (CMake auto-detect + build guard).
9. Runtime decode validation passed: `--video-watch` now reports `dec_frames`, `dec_fps`, and `dec_errors` from live NAL feed.
10. External dependency policy added: development-flexible and CI/release-strict modes for FFmpeg build requirements.
11. Strict dependency mode validated with CMake (`TELLO_REQUIRE_FFMPEG=ON`) and successful build.
12. Phase D resumed with decoder robustness refinement: NAL decode sync-gating added (wait for SPS/PPS/IDR path before unrestricted slice decode).
13. Minimal viewer step completed: new `tello_viewer` app consumes decoded frames via FFmpeg callback and reports frame size/FPS/keyframe counters.
14. D3 viewer rendering step completed: OpenCV window rendering integrated in `tello_viewer` with bounded execution and keypress exit (`q`/`ESC`).
15. D3 color-rendering refinement completed: decoder now provides BGR frame payload via swscale and viewer prefers color rendering with grayscale fallback.
16. Viewer usability refinement completed: on-screen overlay with live metrics plus keyboard controls (`Q/ESC` quit, `P` pause, `S` snapshot).

### Step 7 - Documentation close for D1
**What is being done:** write a dedicated video phase document.

**Why:** preserve reproducibility and sign-off evidence.

**Deliverable:** `docs/04_video_channel.md` with:
1. objectives,
2. implementation notes,
3. test procedure,
4. results,
5. residual risks,
6. next step to D2.

**Current implementation status:**
1. `docs/04_video_channel.md` created and filled.
2. FFmpeg dependency requirements and build policy documented (`TELLO_ENABLE_FFMPEG`, `TELLO_REQUIRE_FFMPEG`).
3. Decoder sync-gating and bounded runtime validation recorded as ongoing D2 hardening.
4. Viewer bootstrap validated in runtime (`960x720`, decode FPS around 31, keyframe counter increasing).
5. Viewer real-time window rendering validated (OpenCV enabled build path).
6. Viewer color pipeline validated with FFmpeg+swscale conversion (stream still observable under bounded execution).
7. Viewer control layer validated with bounded runtime and interactive key handling.

---

## Phase D Sign-off (Completed)

**Date:** 2026-06-18

### Status
- Phase D video channel is complete for current milestone scope, including transport, parser boundary, decode, viewer rendering, and resilience behavior.

### Completed Deliverables
1. Raw video UDP receiver with observability metrics and bounded runtime support.
2. Stream assembly layer (`VideoStreamAssembler`) for Annex-B NAL extraction.
3. Command/session recovery for video continuity after stall and power-cycle, centered in `TelloClient`.
4. FFmpeg decoder integration with optional/strict dependency policy and runtime decode metrics.
5. Minimal viewer (`tello_viewer`) with OpenCV rendering, color conversion path, overlay, pause, snapshot, and overlay toggle controls.
6. Dedicated video documentation with dependency policy and validation procedures.

### Validation Summary
1. Build and repeated runtime runs passed with real Tello stream.
2. Power-cycle recovery scenario passed after hard fallback recovery path.
3. Viewer pipeline validated at `960x720` with stable decode FPS in observed runs.
4. Interactive controls validated (`Q/ESC`, `P`, `S`, `O`).

### Residual Risks
1. Decode error counter may still rise under severe packet loss conditions.
2. Viewer remains a technical demonstrator (no advanced UX controls or recording pipeline yet).

### Phase E Entry Gate
- ✅ Ready to start Phase E (metrics and evaluation).
