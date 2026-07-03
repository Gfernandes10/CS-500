# Development and Evaluation of a Modular C++ Communication, Control, and Visualization System for the DJI Tello Drone

**Course:** CS 500 Project  
**Student:** Gabriel Fernandes  
**Supervisor:** Prof. Stefan Bruda  
**Program Context:** Course-based M.Sc. project  
**Status:** Draft report before ROS2 integration  

## Abstract

This project develops and evaluates a modular C++ software stack for communicating with, monitoring, and controlling a DJI Tello drone. The system is built around a reusable core library that implements UDP-based SDK command execution, telemetry parsing, FFmpeg-based video streaming, runtime metrics, recovery behavior, and safety-oriented RC control. On top of the core library, the project provides command-line tools and a Qt-based Control Panel for live video, telemetry visualization, CSV logging, manual commands, and configurable keyboard-based flight control.

The implementation was evaluated using real-drone experiments covering command latency, telemetry freshness, FFmpeg video performance, GUI responsiveness, keyboard RC safety, power-cycle recovery, and Wi-Fi reconnect behavior. The final experiments show stable baseline command behavior, usable telemetry and video freshness, approximately 31-32 FPS FFmpeg video decoding, and a median estimated vertical RC reaction time of 814 ms during keyboard-controlled flight. The current report documents the system before the planned ROS2 integration stage, which will expose the core library to robotics workflows through topics, services, and command inputs.

## 1. Introduction

Small aerial robots are useful platforms for studying robotics software because they combine networking, telemetry, video streaming, control, safety, and user interaction in a compact system. The DJI Tello is especially suitable for a course project because it exposes a simple SDK over Wi-Fi while still presenting realistic engineering challenges: unreliable wireless links, UDP packet loss, command timeouts, asynchronous telemetry, video decoding, and safety-critical motion commands.

The goal of this CS 500 project is to define and initially develop a graduate-level applied robotics software problem: a modular C++ communication and control stack for the DJI Tello drone. The project focuses on a reusable core library rather than a one-off script. The software is designed to support multiple clients, including command-line tools, a desktop Control Panel, and a future ROS2 bridge.

The main objectives are:

1. implement a reliable command channel over the Tello SDK;
2. parse and expose telemetry from the drone's state stream;
3. receive, decode, and display the video stream using FFmpeg;
4. collect runtime metrics suitable for experimental evaluation;
5. build a desktop Control Panel for operation and data collection;
6. implement configurable keyboard RC control with safety-oriented neutral behavior;
7. evaluate the system using repeatable real-drone experiments;
8. prepare the architecture for ROS2 integration.

## 2. DJI Tello SDK Background

The DJI Tello SDK communicates over Wi-Fi using UDP. The host computer connects to the drone's Wi-Fi network and communicates with the drone at `192.168.10.1`. Three channels are central to this project:

| Channel | UDP Port | Direction | Purpose |
|---|---:|---|---|
| Command | `8889` | host to drone, drone to host | SDK commands and command responses |
| Telemetry | `8890` | drone to host | periodic state packets |
| Video | `11111` | drone to host | H264 video stream |

Before most SDK functionality is available, the host sends `command` to enter SDK mode. After that, command strings such as `battery?`, `takeoff`, `land`, `streamon`, and `streamoff` can be sent to the command port. Many SDK commands return a response such as `ok`, `error`, a numeric value, or no response before the client timeout expires. Because the protocol is UDP-based, software must be prepared for delayed, missing, or ambiguous responses.

The telemetry stream is different from the command channel. Once the drone is in SDK mode, it periodically sends state packets to the host. These packets include fields such as attitude, velocity, height, time-of-flight distance, battery, barometer, and temperature. Telemetry is not requested one packet at a time; it is received asynchronously by a background receiver.

The video stream is enabled with `streamon` and disabled with `streamoff`. The drone sends H264 video over UDP port `11111`. In this project, FFmpeg opens the UDP stream directly, handles H264 parsing and decoding, and returns decoded RGB frames to the application.

The `rc a b c d` command is the SDK's continuous motion command. The four values represent:

1. `a`: left/right velocity command;
2. `b`: forward/back velocity command;
3. `c`: up/down velocity command;
4. `d`: yaw command.

Each value is typically in the range `-100` to `100`. A neutral command, `rc 0 0 0 0`, means no commanded motion. For safe manual control, software must return to neutral when no key or joystick input is active.

## 3. System Architecture

The project is centered on a reusable C++ core library that contains the drone-specific communication and processing logic. Applications such as the CLI tools and Qt Control Panel depend on this library instead of duplicating SDK, telemetry, video, and recovery behavior.

The main components are:

- `UdpSocket`: low-level UDP send/receive abstraction.
- `CommandExecutor`: command send/receive policy with timeout and retry behavior.
- `TelloClient`: high-level drone API, SDK session state, keepalive, and recovery behavior.
- `StateParser`: parser for raw Tello state strings.
- `StateReceiver`: background telemetry receiver and thread-safe latest-state cache.
- `VideoStreamReaderFfmpeg`: FFmpeg Stream reader for H264 video over UDP.
- `MetricsCollector`: central runtime metrics aggregator and CSV export schema.
- CLI applications: smoke commands, command watch, state watch, and video watch.
- Qt Control Panel: graphical operation, visualization, keyboard control, and CSV export.

The architecture separates transport, parsing, client state, metrics, and user interface. This keeps the codebase suitable for later ROS2 integration, where the ROS layer should wrap the core library rather than reimplementing the command, telemetry, and video logic.

## 4. Implementation

### 4.1 Command Channel

The command channel uses UDP to send SDK commands to the drone and wait for responses. Reliability is handled in layers. `UdpSocket` provides transport operations and timeout handling. `CommandExecutor` handles command execution policy, including retry and timeout behavior. `TelloClient` owns higher-level SDK state and exposes methods such as SDK entry, takeoff, land, stream control, keepalive, and recovery.

The client tracks connection state through connected, recovering, and disconnected states. It also exposes connection events such as lost and restored, which are useful for experiments and logging. A keepalive loop can periodically send `battery?` queries to keep the SDK session active when no RC stream is running.

Several command-channel details were refined during real-drone testing. Critical commands such as `takeoff` and `land` are protected by preflight neutral RC behavior. RC commands use a non-blocking path where appropriate so that they do not wait behind slow query commands. In the Qt Control Panel, blocking critical commands and stream-control commands are dispatched through a background command worker; the GUI receives the final result through a queued callback and remains responsive while the command is waiting for an SDK response or timeout. Metrics include command latency, failures, command mutex wait time, command source, raw attempt logs, and connection state.

### 4.2 Telemetry

Telemetry is received from the Tello state channel on UDP port `8890`. `StateReceiver` runs a background receive loop, parses each packet with `StateParser`, and stores the latest valid state. Consumers access telemetry through a thread-safe latest-state cache.

The project records both wall-clock timestamps and monotonic elapsed timestamps. For analysis, monotonic elapsed time is preferred because it avoids false gaps caused by wall-clock adjustments. Telemetry metrics include receive rate, packet age, interarrival gaps, quality labels, and selected state fields such as height, time-of-flight, battery, temperature, yaw, and vertical velocity.

### 4.3 Video Pipeline

The video pipeline uses FFmpeg Stream as the only supported runtime path. The drone sends H264 video over UDP after `streamon`. `VideoStreamReaderFfmpeg` opens:

```text
udp://@0.0.0.0:11111?overrun_nonfatal=1&fifo_size=5000000
```

FFmpeg handles UDP stream reading, H264 parsing, decoding, and frame timing internally. The application receives decoded RGB frames and displays them in the Qt Control Panel. This design was selected because it produced smoother live video during real flight operation and avoids maintaining a second custom H264 packet assembly path.

The video metrics include packets read, bytes read, latest frame age, decoded frames, decode FPS, decode errors, frame size, keyframes, and UI frame conversion/display counters. The same video path is used by both the CLI video experiment and the Control Panel.

### 4.4 Metrics and Experiment Logging

`MetricsCollector` centralizes runtime metrics across command, telemetry, video, GUI, recovery, and control behavior. CSV rows include experiment metadata followed by metrics groups for:

1. command latency and failures;
2. telemetry counters and freshness;
3. video packet/decode behavior;
4. frame size and decoder FPS;
5. recovery status;
6. GUI timing;
7. RC stream state and values;
8. keepalive state;
9. event/log messages.

The word "quality" in the experiment results means transport freshness and continuity. It does not mean subjective image sharpness, scene visibility, or flight-control accuracy. The labels are computed from packet age and interarrival timing:

| Label | Meaning |
|---|---|
| `NO_DATA` | no packet has been received |
| `OK` | latest packet age is at most 200 ms and interarrival is at most 300 ms |
| `DEGRADED` | latest packet age is at most 500 ms and interarrival is at most 500 ms |
| `STALE` | data is too old for safe control assumptions |

These labels are intended to support future closed-loop control decisions. `OK` means the stream is fresh enough for normal monitoring. `DEGRADED` suggests caution. `STALE` and `NO_DATA` should be treated as unsafe for feedback control.

Packet age and packet interarrival measure different aspects of the stream. Packet age is how old the most recent packet is at the moment a metrics row is written. If no new packet arrives, age increases. Packet interarrival is the elapsed time between two consecutive received packets. A stream can have regular interarrival while packets are flowing, but high age after packets stop arriving.

### 4.5 Qt Control Panel

The Qt Control Panel is the main human-facing application. It is organized into Config and Operation tabs. Two annotated screenshots will be added later; the numbered descriptions below are intended to match those figures.

#### Config Tab Features

1. **Logging Export Configuration:** selects GUI metrics and state CSV paths, starts/stops recording, clears buffered recordings, and exports both CSV files.
2. **Control Profiles:** creates, edits, saves, and deletes keyboard control profiles while preserving at least one default profile.
3. **RC Aggression:** defines the absolute RC magnitude used by mapped keyboard movements.
4. **Input Mapping:** maps keys to left, right, forward, back, up, down, yaw left, and yaw right actions.
5. **SDK Read Commands:** provides quick buttons for query commands such as `battery?`, `speed?`, `time?`, `wifi?`, `sdk?`, and `sn?`.
6. **Set Speed:** sends `speed x`, with the default setpoint initialized to `100`.
7. **Control Commands:** exposes common movement, rotation, flip, takeoff, land, hover, and emergency commands. Blocking critical commands are executed by a background command worker so the GUI event loop does not wait for SDK timeouts.
8. **Raw Command:** allows manually sending an SDK command string for testing or diagnostics.

#### Operation Tab Features

1. **Keyboard Control:** enables/disables keyboard RC control and shows the active profile and current RC vector.
2. **Dedicated RC Worker:** sends the desired RC command independently from the GUI event loop and falls back to neutral if input becomes stale.
3. **Manual RC Operation:** provides sliders for left/right, forward/back, up/down, and yaw, plus one-shot RC, zero-and-send, and continuous RC stream controls.
4. **State History:** displays telemetry history, selected state plots, buffer size, recording status, and a red/green REC indicator.
5. **Vision:** starts/stops FFmpeg Stream viewing, pauses live display, captures snapshots, and toggles the overlay.
6. **Top Status Row:** shows connection status, Wi-Fi status, battery, temperature, and recording state.
7. **Log Panel:** records user-visible events, command responses, recovery events, and state transitions.

The Control Panel also records user-visible logs into the metrics CSV, allowing post-run analysis to correlate GUI events with telemetry, video, and command behavior. On application close or Ctrl+C, the panel sends `emergency` if the drone is connected.

### 4.6 Keyboard RC Control

Keyboard RC control was implemented using configurable control profiles. A profile maps keyboard inputs to RC axes and defines a single aggression value used for all mapped movement commands. Profiles are stored persistently and can be edited in the Control Panel.

The initial concern during flight testing was that GUI stalls could cause a movement command to remain active after the user released the key. To mitigate this, RC sending was moved into a dedicated worker. The GUI updates a desired RC state, while the worker sends RC commands at a fixed cadence. If input becomes stale, the worker sends neutral RC. This design reduces coupling between GUI event-loop responsiveness and drone motion safety.

## 5. Milestone Plan Traceability

The original milestone plan divided the project into eight phases. This section summarizes each planned phase, the delivered work, and any remaining gap. It is intentionally explicit because the project is evaluated against the planned scope.

### 5.1 Phase 0: Project Definition and Architecture

The planned goal for Phase 0 was to define the project scope, study the DJI Tello SDK, choose technologies, create the source structure, define the core modules, and establish the CMake build system.

This phase was delivered. The project scope was defined around a modular C++ Tello communication stack. The architecture separates command, telemetry, video, metrics, GUI, and future ROS integration. The source tree was organized around a reusable core library, CLI applications, tests, documentation, and a placeholder ROS area. CMake and C++17 were adopted.

The architecture is documented textually in this report. A polished architecture diagram can still be added later to improve presentation quality, but the architectural foundation itself was completed.

### 5.2 Phase 1: Core Networking and Command Layer

The planned goal for Phase 1 was to implement UDP communication, command execution, timeout and retry behavior, logging, and a CLI tool for command testing.

This phase was delivered. `UdpSocket`, `CommandExecutor`, and `TelloClient` implement the command channel. The CLI supports one-shot commands, command watching, SDK initialization, keepalive, command metrics, and recovery state reporting. Logging is integrated through a console logger and GUI log export.

The command experiments validate this phase with real-drone measurements of command latency and failure behavior.

### 5.3 Phase 2: Telemetry System

The planned goal for Phase 2 was to implement continuous telemetry reception, parse raw state messages into structured data, provide thread-safe access, and record telemetry logs.

This phase was delivered. `StateParser` converts raw SDK state strings into structured telemetry. `StateReceiver` runs a background receiver, stores the latest state, buffers recent history, records state CSV data, and exposes thread-safe access to consumers.

Telemetry experiments validate packet freshness, packet age, interarrival gaps, and state recording. Unit tests also validate the parser behavior.

### 5.4 Phase 3: Video Stream Integration

The planned goal for Phase 3 was to receive, decode, and expose the drone video stream, including a minimal viewer or frame access path.

This phase was delivered with a design refinement. The final video implementation uses `VideoStreamReaderFfmpeg`, which opens the Tello H264 UDP stream directly through FFmpeg, decodes frames, converts them to RGB, and feeds both CLI metrics and the Qt Control Panel. This replaced the initial idea of maintaining a custom packet/NAL pipeline because FFmpeg Stream produced smoother real-flight video and reduced implementation risk.

The final project intentionally supports only FFmpeg Stream as the video runtime path. Video experiments validate decoded frame rate, frame freshness, and GUI display behavior.

### 5.5 Phase 4: Evaluation and Metrics

The planned goal for Phase 4 was to add command latency, telemetry rate, video FPS, timestamp logging, metadata-ready CSV exports, experiments, plots, and performance analysis.

This phase was delivered and became one of the central contributions of the project. `MetricsCollector` centralizes command, telemetry, video, GUI, keepalive, recovery, and RC metrics. Experiments record metadata such as test ID, scenario, notes, and run mode.

The final experiment set covers command behavior, telemetry continuity, FFmpeg video, GUI operation, keyboard RC flight, power-cycle recovery, and Wi-Fi reconnect behavior. The Results section includes plots and quantitative summaries generated from these experiments.

### 5.6 Phase 5: ROS Integration

The planned goal for Phase 5 was to create a ROS2 package, wrap the core library in a ROS node, publish telemetry and camera data, subscribe to velocity commands, expose takeoff and landing services, and test the bridge with ROS tools.

This phase is not delivered yet. The current contribution toward this milestone is architectural readiness: the core library was designed so a ROS2 bridge can reuse command, telemetry, video, safety, and recovery logic without duplicating it. A placeholder ROS area exists, and the intended bridge design is described in the Future Work section.

This is the main remaining planned technical milestone.

### 5.7 Phase 6: Desktop GUI

The planned goal for Phase 6 was to build a simple Qt GUI with video, telemetry display, control buttons, and integration with the core library.

This phase was delivered with expanded scope. The Qt Control Panel includes SDK connection, FFmpeg video, telemetry plotting, state history, CSV export, logging, takeoff confirmation, emergency behavior on exit, keyboard profiles, RC sliders, a dedicated keyboard RC worker, an asynchronous command worker for blocking critical commands, and runtime diagnostics.

The GUI experiments validate that the Control Panel can display video and telemetry while logging metrics, and the keyboard flight experiment validates its use during real drone operation.

### 5.8 Phase 7: Final Report and Presentation

The planned goal for Phase 7 was to produce the final technical report, presentation slides, and a live or recorded demo.

This phase is partially delivered. This draft report documents the architecture, SDK background, implementation, experiments, results, limitations, and milestone traceability. Figures and experiment summaries are included for the final PDF.

The remaining work for this phase is to finalize the written report, prepare presentation slides, and prepare the final demo material.

The only major planned technical component not yet implemented is the ROS2 bridge from Phase 5. The final report and presentation work from Phase 7 is also still in progress. All other implementation phases have been delivered, and several were expanded based on real-drone testing, especially metrics, recovery, GUI diagnostics, and keyboard RC safety.

## 6. Experimental Methodology

The evaluation focuses on the system behaviors that matter for manual operation and future robotics integration:

| ID | Purpose |
|---|---|
| `E1-SMOKE-CMD` | Confirm basic SDK connectivity before the run set |
| `E2-CMD-BASE` | Measure command latency and reliability under stable conditions |
| `E3-STATE-CLI` | Measure telemetry continuity without GUI or video rendering load |
| `E4-VIDEO-CLI` | Measure FFmpeg Stream video behavior without Qt rendering |
| `E5-GUI-VID-IDLE` | Measure GUI telemetry and video behavior while the drone is stationary |
| `E6-KBD-PROFILE` | Validate keyboard profile persistence and mapping behavior |
| `E7-KBD-RESPONSE` | Evaluate keyboard RC safety, flight response, telemetry, and video while airborne |
| `E8-PWR-CYCLE` | Evaluate recovery after a drone restart |
| `E9-WIFI-LOSS` | Evaluate command reconnect after Wi-Fi loss |

Each experiment records metrics with a consistent schema so command, telemetry, video, GUI, recovery, and RC behavior can be compared across runs.

## 7. Results

The final experiments produced a complete set of command, telemetry, video, GUI, keyboard RC, and recovery logs. The most important quantitative findings are summarized below.

| Experiment | Key Result |
|---|---|
| `E2-CMD-BASE` | 59.0 s command baseline, 0 final command failures, 34.6 ms final average latency |
| `E3-STATE-CLI` | telemetry-only baseline ended with telemetry quality `OK` |
| `E4-VIDEO-CLI` | FFmpeg Stream video ended with video quality `OK`, decoder FPS about 31.3 |
| `E5-GUI-VID-IDLE` | valid idle GUI run; no RC commands; telemetry/video quality `OK/OK` |
| `E7-KBD-RESPONSE` | 1106 RC rows, 291 non-zero RC rows, telemetry/video quality `OK/OK`, decoder FPS about 32.1 |
| `E8-PWR-CYCLE` | 8 recovery rows, successful `power_streamon` recovery |
| `E9-WIFI-LOSS` | 38 rows, 4 command failures, final command result `OK` |

### 7.1 Command Channel

The command baseline measured SDK command behavior in a stable connection scenario. The final average command latency was approximately 34.6 ms, with no final command failures during the baseline run. This indicates that the command path is reliable for normal query-style operations when the Wi-Fi link is stable.

The plotted command latency is a cumulative running average. The first point is high because it is based on only one `battery?` response, around 117 ms. Later `battery?` responses were mostly around 48-53 ms, so the running average falls as more samples are added. This should be interpreted as first-command or warm-up overhead plus cumulative-average convergence, not as evidence that every command continuously became faster.

![Command latency](images/e2_command_latency.png)

### 7.2 Telemetry

Telemetry quality remained generally healthy across the core experiments. In this report, quality means transport freshness and continuity, not subjective signal quality. `OK` means the latest telemetry packet age is at most 200 ms and the packet interarrival time is at most 300 ms. `DEGRADED` means the latest packet age is at most 500 ms and interarrival is at most 500 ms. `STALE` means data is older than those limits, and `NO_DATA` means no packet was received.

Telemetry age is important because stale telemetry would be unsafe for future feedback control. Packet age is the age of the latest known telemetry sample at the time of measurement. Packet interarrival is the spacing between consecutive telemetry packets. The analysis therefore plots packet age directly, and the timeline view shows how packet age and packet interarrival evolve during each run relative to the `OK`, `DEGRADED`, and `STALE` thresholds. The power-cycle run is shown separately in the timeline because intentionally powering off the drone creates a large age spike that would otherwise compress the other traces.

![Telemetry age](images/telemetry_age_comparison.png)

![Telemetry freshness timeline](images/telemetry_freshness_timeline.png)

### 7.3 Video Performance

The FFmpeg Stream path achieved usable real-time video performance. The CLI video baseline ended with video quality `OK` and decoder FPS around 31.3. The GUI and keyboard-flight runs also maintained video quality `OK` with decoder FPS around 31-32. As with telemetry, video quality here means stream freshness and continuity: recent decoded frames with acceptable interarrival are `OK`; delayed frames are `DEGRADED`; stale or missing frames are represented as `STALE` or `NO_DATA`.

![Video FPS](images/video_decode_fps_comparison.png)

![Video quality](images/video_quality_comparison.png)

These results support the decision to use FFmpeg Stream as the project's only video runtime path.

### 7.4 GUI Responsiveness

The Control Panel was tested in idle video operation and during keyboard flight. GUI timing metrics distinguish event-loop scheduling delay from the actual cost of refreshing video and drawing plots. The reference periods are approximately 120 ms for the vision refresh timer and 250 ms for the state/plot refresh timer.

The keyboard-flight run showed the largest GUI delay spikes near 36.2 s and 135.7 s. These timestamps align with the delayed timeout logging for `takeoff` and `land`, while telemetry indicates physical takeoff started earlier, around 30.6 s, and physical landing started earlier, around 130.0 s. This suggests the spikes were associated with the blocking critical-command path returning and logging its result, not with the physical motion itself. This finding motivated the final change that moves blocking critical commands and stream/recovery commands into a background command worker.

![GUI tick delay](images/gui_tick_delay_idle_vs_flight.png)

The idle GUI run maintained video and telemetry freshness, but timing metrics remain important because manual control safety depends on avoiding long UI stalls.

### 7.5 Keyboard RC Flight

The keyboard flight experiment is the main real-operation test. During this run, the drone was airborne, video was active, telemetry was logged, and keyboard-generated RC commands were sent by the dedicated RC worker.

The height and vertical RC plot separates the physical height readings from the operator's vertical RC command. The takeoff and landing markers are estimated from telemetry rather than from delayed command log rows. The physical takeoff marker is the first sustained increase in height or time-of-flight, and the landing marker is the beginning of the final sustained descent toward the ground.

![E7 height and vertical RC](images/e7_height_and_vertical_rc.png)

The state log confirms a real flight and shows the evolution of attitude, velocity, height, barometer, acceleration, battery, and temperature. The subplot matrix focuses on the flight window, from approximately 7 seconds before the telemetry-estimated takeoff start to approximately 7 seconds after the telemetry-estimated landing start. Acceleration fields are reported in thousandths of gravity (`0.001g`), as exposed by the Tello SDK.

![E7 drone state subplots](images/e7_drone_state_subplots.png)

The RC channel plot shows the keyboard-generated control commands over time.

![E7 RC channels](images/e7_rc_channels.png)

During the same flight, telemetry and video freshness stayed `OK`, and decode FPS remained around 32 FPS. This indicates that the system could maintain video, telemetry, and RC operation simultaneously in this test.

![E7 operation quality and video](images/e7_operation_quality_video.png)

The estimated vertical RC reaction latency was computed from the keyboard-flight state data. Consecutive non-zero RC samples were grouped into command pulses. For vertical pulses (`rc_c != 0`), the command timestamp was taken from the monotonic RC command timestamp. The response timestamp was the first later telemetry sample where height or time-of-flight changed by at least 5 cm in the expected direction.

Each point in the reaction plot represents one valid vertical RC pulse. Blue points are upward commands (`rc_c > 0`). Orange points are downward commands (`rc_c < 0`). The dashed horizontal line marks the median reaction time, and the dotted horizontal line marks the mean reaction time.

The median estimated reaction time was **814 ms** across 17 valid vertical RC pulses. The mean was 835 ms, with a range from 342 ms to 1320 ms.

![E7 RC reaction latency](images/e7_rc_reaction_latency.png)

### 7.6 Recovery

The power-cycle experiment validated recovery after the drone restarted while the process remained running. The run captured recovery rows including a successful `power_streamon` stage.

![E8 power-cycle quality](images/e8_power_cycle_quality.png)

The data-age plot shows how video and telemetry became stale during the outage and then returned to healthy values after recovery.

![E8 power-cycle age](images/e8_power_cycle_age.png)

The Wi-Fi loss experiment validated command-channel reconnect behavior. The run produced command failures during the outage but ended with command result `OK`, indicating that reconnect behavior restored the SDK command path.

![E9 Wi-Fi reconnect](images/e9_wifi_loss_reconnect.png)

## 8. Discussion

The results show that the core command, telemetry, video, and GUI components are functional under real-drone conditions. The baseline command experiment suggests that normal SDK queries are reliable under stable Wi-Fi. Telemetry remains fresh enough for monitoring, and the FFmpeg Stream backend provides usable video performance for live operation.

The most important implementation decision in the video subsystem was standardizing on FFmpeg Stream. By allowing FFmpeg to own UDP stream reading, H264 parsing, decoding, and frame timing, the application avoids maintaining a fragile custom video parser and obtains smoother live video during flight.

Keyboard RC control required special attention because control commands can be safety-critical. The dedicated RC worker reduces the risk that GUI event-loop stalls keep a movement command active longer than intended. The flight experiment showed that RC commands were recorded, non-zero commands were sent, the drone responded physically, and telemetry/video remained `OK`.

The GUI timing analysis also showed that blocking critical command calls can affect responsiveness when their timeout result is logged. To address this, the Control Panel was updated so critical commands and stream/recovery commands run in a background command worker and report results back to the GUI through queued callbacks. This preserves the command behavior while reducing coupling between SDK timeouts and GUI responsiveness.

Recovery behavior is also important because real Wi-Fi and drone power states are not perfectly stable. The power-cycle experiment showed that the system can detect a stalled video/session state and recover after SDK re-entry and stream restart.

## 9. Limitations

The project has several limitations:

1. Experiments were performed with a single DJI Tello drone and one test environment.
2. Wi-Fi conditions are environment-dependent and may vary across rooms, laptops, and drivers.
3. The RC reaction latency estimate is based on onboard telemetry, not external motion capture.
4. The current system does not implement a closed-loop controller.
5. The ROS2 bridge is not implemented yet.
6. Some command responses, especially around `takeoff` and `land`, can be delayed or time out even when the drone physically executes the action. The GUI no longer waits for these responses on the main event loop, but the underlying SDK ambiguity remains.
7. Thermal behavior can affect long back-to-back experiments.

## 10. Future Work: ROS2 Integration Placeholder

The next planned stage is ROS2 integration. The intended design is to create a thin ROS2 bridge that reuses the core C++ library rather than duplicating command, telemetry, video, or recovery logic.

The planned ROS2 package should:

1. wrap the existing drone client, telemetry receiver, and video access;
2. publish telemetry topics;
3. publish camera frames if feasible;
4. subscribe to velocity or RC command inputs;
5. expose takeoff, land, and emergency services;
6. preserve the existing safety behavior around neutral RC and critical commands.

This future work will make the project usable from standard robotics tools and provide a bridge toward future closed-loop control experiments.

## 11. Conclusion

This project produced a modular C++ communication, telemetry, video, metrics, and control system for the DJI Tello drone. The system includes a reusable core library, command-line tools, a Qt Control Panel, FFmpeg-based video display, CSV experiment logging, and configurable keyboard RC control.

Real-drone experiments validated the main engineering claims. The command channel was reliable in the baseline run, telemetry and video freshness remained healthy in core scenarios, FFmpeg Stream provided practical live video performance, keyboard RC control worked during flight, and recovery was demonstrated after a drone power-cycle. The project is now ready for the next phase: ROS2 integration and final report refinement.

## Appendix A: DJI Tello SDK Command Summary

| Command | Type | Purpose |
|---|---|---|
| `command` | setup | enters SDK mode |
| `battery?` | query | returns battery percentage |
| `wifi?` | query | returns Wi-Fi signal information |
| `speed?` | query | returns the current speed setting |
| `time?` | query | returns motor time |
| `sdk?` | query | returns SDK version |
| `sn?` | query | returns serial number |
| `streamon` | video | enables H264 video streaming on UDP port `11111` |
| `streamoff` | video | disables video streaming |
| `takeoff` | flight | starts takeoff |
| `land` | flight | starts landing |
| `emergency` | safety | immediately stops motors |
| `speed x` | configuration | sets movement speed, where `x` is usually `10` to `100` |
| `rc a b c d` | flight control | sends continuous motion commands for left/right, forward/back, up/down, and yaw |
| `rc 0 0 0 0` | flight control | neutral RC command used to stop commanded motion |

## Appendix B: Experiment Summary

| ID | Primary Evidence |
|---|---|
| `E1-SMOKE-CMD` | SDK entry works before the full experiment sequence |
| `E2-CMD-BASE` | command latency and failure rate under stable conditions |
| `E3-STATE-CLI` | telemetry freshness without GUI/video load |
| `E4-VIDEO-CLI` | FFmpeg Stream decode behavior without Qt rendering |
| `E5-GUI-VID-IDLE` | Control Panel telemetry/video behavior while stationary |
| `E6-KBD-PROFILE` | keyboard profile persistence and mapping correctness |
| `E7-KBD-RESPONSE` | airborne keyboard RC response, video, and telemetry behavior |
| `E8-PWR-CYCLE` | recovery after drone restart |
| `E9-WIFI-LOSS` | command-channel recovery after Wi-Fi loss |

## Appendix C: Build Environment Notes

The project uses CMake and C++17. Video support requires FFmpeg development packages for `libavformat`, `libavcodec`, `libavutil`, and `libswscale`. FFmpeg is a required dependency for the video-enabled build because the project supports only the FFmpeg Stream video path.
