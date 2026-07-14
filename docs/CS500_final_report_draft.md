# Development and Evaluation of a Modular C++ Communication, Control, Visualization, and ROS Integration System for the DJI Tello Drone

**Course:** CS 500 Project  
**Student:** Gabriel Fernandes  
**Supervisor:** Prof. Stefan Bruda  
**Program Context:** Course-based M.Sc. project  
**Status:** Final report

## Abstract

This project developed and evaluated a modular C++ software stack for communicating with, monitoring, and controlling a DJI Tello drone. A reusable core library owns UDP command execution, asynchronous telemetry, FFmpeg video decoding, runtime metrics, recovery, and safety-oriented RC control. Command-line tools and a Qt Control Panel reuse that core for diagnosis, live operation, visualization, CSV recording, and configurable keyboard flight. A ROS2 bridge was also implemented around the released core artifact.

Evaluation used repeated real-drone experiments spanning command latency, telemetry continuity, video decoding, GUI scheduling, grounded RC safety, airborne response, drone power-cycle recovery, and host Wi-Fi reconnection. An initially recurring 2.4-3.2 s command blackout under WSL2 motivated a controlled native-Linux reproduction. Every WSL2 command run contained a recovered interruption, whereas 708 native command samples completed with zero retry, timeout, or recovery and physical-interface captures contained a response for every request. Native telemetry remained near 9.88 Hz without gaps of 300 ms or more; native 960×720 video remained near 30 fps; GUI and flight runs retained fresh telemetry/video; grounded RC returned to neutral without unsafe nonzero output; and all recorded power-cycle and Wi-Fi interruption trials recovered. The results support native Linux as the deployment environment and demonstrate a reusable experimental basis for future closed-loop work.

## 1. Introduction

Small aerial robots combine networking, asynchronous sensing, video, control, safety, and user interaction in a compact platform. The DJI Tello is attractive for applied robotics because its SDK is accessible over Wi-Fi, but its UDP transport also exposes realistic engineering problems: delayed or missing command responses, independently arriving telemetry, compressed video, shared-resource contention, and the safety consequences of stale motion commands.

The goal of this CS 500 project was therefore not to build a one-off flight script, but a reusable C++ communication and control stack shared by command-line tools, a desktop Control Panel, and a ROS2 integration layer. The main objectives were to:

1. implement a reliable and diagnosable SDK command channel;
2. parse and expose asynchronous drone telemetry;
3. receive, decode, and display H264 video through FFmpeg;
4. collect metrics detailed enough to distinguish application, GUI, and transport behavior;
5. provide a Qt Control Panel for operation and repeatable data collection;
6. implement configurable keyboard RC with independent cadence and neutral safety;
7. evaluate ordinary operation and recovery through repeated real-drone experiments;
8. expose the same runtime through ROS2 without duplicating drone ownership.

A major experimental issue shaped the final evaluation. Early repeated command tests under WSL2 showed an interruption at approximately 60-66 seconds in every run. Public API calls eventually returned successfully, but only after multiple UDP receive timeouts and SDK recovery, producing individual latencies around 2.45-3.19 seconds. Telemetry-only tests did not show corresponding continuity gaps. This raised a causal question that could not be answered from application logs alone: was the delay caused by the C++ executor, the drone, the radio, or the additional WSL2/Hyper-V/Windows networking path?

The investigation therefore became part of the project rather than an incidental debugging note. Command runs were repeated with matched packet captures, then reproduced on Ubuntu native Linux using the same C++ implementation and drone. Telemetry was also repeated across WSL2 and native Linux to determine whether the observed effect was command-specific or a broader stream-continuity problem. Once the native evidence removed the periodic blackout, later video, GUI, RC, flight, and recovery experiments were executed only on native Linux.

## 2. DJI Tello SDK Background

![DJI Tello drone used in this project](images/drone-tello.jpg)

The DJI Tello SDK communicates over Wi-Fi using UDP. The host computer connects to the drone's Wi-Fi network and communicates with the drone at `192.168.10.1`. Three channels are central to this project:

| Channel | UDP Port | Direction | Purpose |
|---|---:|---|---|
| Command | `8889` | host to drone, drone to host | SDK commands and command responses |
| Telemetry | `8890` | drone to host | periodic state packets |
| Video | `11111` | drone to host | H264 video stream |

Before most SDK functionality is available, the host sends `command` to enter SDK mode. After that, command strings such as `battery?`, `takeoff`, `land`, `streamon`, and `streamoff` can be sent to the command port. Many SDK commands return a response such as `ok`, `error`, a numeric value, or no response before the client timeout expires. Because the protocol is UDP-based, software must be prepared for delayed, missing, or ambiguous responses.

The telemetry stream is different from the command channel. Once the drone is in SDK mode, it periodically sends state packets to the host. These packets include fields such as attitude, velocity, height, time-of-flight distance, battery, barometer, and temperature. Telemetry is not requested one packet at a time; it is received asynchronously by a background receiver.

The main telemetry fields used by this project are:

| Field | Meaning | Unit |
|---|---|---|
| `pitch` | pitch attitude | degrees |
| `roll` | roll attitude | degrees |
| `yaw` | yaw attitude | degrees |
| `vgx` | velocity along the SDK x-axis | cm/s |
| `vgy` | velocity along the SDK y-axis | cm/s |
| `vgz` | velocity along the SDK z-axis | cm/s |
| `templ` | lowest measured temperature | degrees Celsius |
| `temph` | highest measured temperature | degrees Celsius |
| `tof` | downward time-of-flight distance | cm |
| `h` | SDK-reported height | cm |
| `bat` | battery percentage | percent |
| `baro` | barometer-derived altitude measurement | cm |
| `time` | motor running time | seconds |
| `agx` | acceleration along the SDK x-axis | `0.001g` |
| `agy` | acceleration along the SDK y-axis | `0.001g` |
| `agz` | acceleration along the SDK z-axis | `0.001g` |

Three altitude-related fields are especially relevant for the flight analysis. `tof` is the time-of-flight distance in centimeters, measured by a downward distance sensor that estimates range from the time required for an emitted signal to reflect back to the drone. `h` is the SDK-reported height in centimeters. `baro` is a barometer-derived altitude measurement in centimeters, based on air pressure. In this project, `h` and `tof` are used to detect short vertical RC responses because they are easier to interpret over low-altitude indoor motion. `baro` is useful as a complementary trend signal but is not used alone for fine reaction-time estimates.

The video stream is enabled with `streamon` and disabled with `streamoff`. The drone sends H264 video over UDP port `11111`. In this project, FFmpeg opens the UDP stream directly, handles H264 parsing and decoding, and returns decoded RGB frames to the application.

The `rc a b c d` command is the SDK's continuous motion command. The four values represent:

1. `a`: left/right velocity command;
2. `b`: forward/back velocity command;
3. `c`: up/down velocity command;
4. `d`: yaw command.

Each value is typically in the range `-100` to `100`. A neutral command, `rc 0 0 0 0`, means no commanded motion. For safe manual control, software must return to neutral when no key or joystick input is active.

## 3. System Architecture

The project is centered on a reusable C++ core library that contains the drone-specific communication and processing logic. Applications such as the CLI tools and Qt Control Panel depend on this library instead of duplicating SDK, telemetry, video, and recovery behavior.

![System architecture overview](images/system_architecture.svg)

The diagram shows the main project boundary: the drone communicates over the SDK UDP channels, `tello_core` owns the reusable communication and processing logic, and each client uses that same core layer instead of reimplementing drone-specific behavior.

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

The architecture separates transport, parsing, client state, metrics, and user interface. This also supports ROS2 integration, where the ROS layer wraps the core library instead of reimplementing the command, telemetry, video, and safety logic.

## 4. Implementation

### 4.1 Command Channel

The command channel uses UDP to send SDK commands to the drone and wait for responses for synchronous SDK operations. Reliability is handled in layers. `UdpSocket` provides transport operations and timeout handling. `CommandExecutor` handles command execution policy, including retry and timeout behavior. `TelloClient` owns higher-level SDK state and exposes methods such as SDK entry, takeoff, land, stream control, keepalive, and recovery. 

The client tracks connection state through connected, recovering, and disconnected states. It also exposes connection events such as lost, restored, and transient loss recovered, which are useful for experiments and logging. A keepalive loop can periodically send `battery?` queries to keep the SDK session active when no RC stream is running. This is necessary because the Tello SDK states that if the drone receives no command for 15 seconds, it automatically lands. During idle experiments, periodic lightweight commands prevent the drone from treating the session as inactive while avoiding unnecessary motion commands.

Several command-channel details were refined during real-drone testing. Critical commands such as `takeoff` and `land` are protected by preflight neutral RC behavior. RC commands use a no-wait path so continuous control does not wait behind slow query commands. In the Qt Control Panel, blocking critical commands and stream-control commands are dispatched through a background command worker; the GUI receives the final result through a queued callback and remains responsive while the command is waiting for an SDK response or timeout. Metrics include command latency, failures, command mutex wait time, command source, raw attempt logs, internal attempt logs, recovery timing, and connection state.

An important refinement was the distinction between a real connection loss and a transient command outage that recovers inside the same high-level API call. Earlier logs could report `LOST` as soon as one internal SDK command attempt timed out, even if retry or recovery succeeded and the public API call returned `OK`. The client now records the full `command_internal_attempt_log`, including all executor calls and recovery calls made inside one high-level command. If at least one internal attempt failed but the final command result is `OK`, the event is classified as `TRANSIENT_LOSS_RECOVERED` instead of a persistent loss. This produces a more accurate operator and experiment log.

### 4.2 Telemetry

Telemetry is received from the Tello state channel on UDP port `8890`. `StateReceiver` runs a background receive loop, parses each packet with `StateParser`, and stores the latest valid state. Consumers access telemetry through a thread-safe latest-state cache.

The project records wall-clock timestamps for correlation with external logs and packet captures, and monotonic elapsed timestamps for duration and ordering within a run. Telemetry age, receive rate, and packet-interarrival metrics are computed with a monotonic clock, preventing system-clock adjustments from creating artificial gaps.

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

The Qt Control Panel is the main human-facing application. It has a global control area above the tabs, followed by Config and Operation tabs. The annotated screenshots below show the corresponding interface areas; the numbered descriptions are intended to match those figures.

#### Global Controls Above The Tabs

![Global Control Panel Area](images/global_gui.png)

1. **Top Status Row:** summarizes connection state, SDK readiness, RC stream state, keyboard-control state, speed setpoint, aggregate link quality, Wi-Fi status, battery, temperature, and state-recording status.
2. **Connection Controls:** provide `Connect + SDK` and `Disconnect` actions. `Connect + SDK` initializes the command channel, enters SDK mode, starts telemetry reception, and starts SDK keepalive when appropriate.
3. **Basic Flight Controls:** expose high-priority commands such as `takeoff`, `land`, `emergency`, `streamon`, `streamoff`, and hover/stop. `takeoff` requires confirmation, `emergency` is visually emphasized, and blocking critical commands are executed by a background command worker so the GUI event loop does not wait for SDK timeouts.
4. **Log Panel:** records user-visible events, command responses, recovery events, and state transitions. The same log messages can also be stored in the GUI metrics CSV for experiment correlation.

#### Config Tab Features

![Control Panel Config Tab](images/config_gui.png)

1. **Logging Export Configuration:** selects GUI metrics and state CSV paths, starts/stops recording, clears buffered recordings, and exports both CSV files.
2. **Control Profiles:** creates, edits, saves, and deletes keyboard control profiles while preserving at least one default profile.
3. **RC Aggression:** defines the absolute RC magnitude used by mapped keyboard movements.
4. **Input Mapping:** maps keys to left, right, forward, back, up, down, yaw left, and yaw right actions.
5. **SDK Read Commands:** provides quick buttons for query commands such as `battery?`, `speed?`, `time?`, `wifi?`, `sdk?`, and `sn?`.
6. **Set Speed:** sends `speed x`, with the default setpoint initialized to `100`.
7. **Movement, Rotation, and Flip Commands:** exposes parameterized commands such as `up x`, `down x`, `left x`, `right x`, `forward x`, `back x`, `cw x`, `ccw x`, and `flip x`.
8. **Raw Command:** allows manually sending an SDK command string for testing or diagnostics.

#### Operation Tab Features

![Control Panel Operation Tab](images/operation_gui.png)

1. **Keyboard Control:** enables/disables keyboard RC control and shows the active profile and current RC vector.
2. **Dedicated RC Worker:** sends the desired RC command independently from the GUI event loop and falls back to neutral if input becomes stale.
3. **Manual RC Operation:** provides sliders for left/right, forward/back, up/down, and yaw, plus one-shot RC, zero-and-send, and continuous RC stream controls.
4. **State History:** displays telemetry history, selected state plots, buffer size, recording status, and a red/green REC indicator.
5. **Vision:** starts/stops FFmpeg Stream viewing, pauses live display, captures snapshots, and toggles the overlay.

The Control Panel also records user-visible logs into the metrics CSV, allowing post-run analysis to correlate GUI events with telemetry, video, and command behavior. On application close or Ctrl+C, the panel sends `emergency` if the drone is connected.

### 4.6 Keyboard RC Control

Keyboard RC control was implemented using configurable control profiles. A profile maps keyboard inputs to RC axes and defines a single aggression value used for all mapped movement commands. Profiles are stored persistently and can be edited in the Control Panel.

The initial concern during flight testing was that GUI stalls could cause a movement command to remain active after the user released the key. To mitigate this, RC sending was moved into a dedicated worker. The GUI updates a desired RC state, while the worker sends RC commands at a fixed cadence. If input becomes stale, the worker sends neutral RC. This design reduces coupling between GUI event-loop responsiveness and drone motion safety.

### 4.7 ROS2 Integration

ROS, the Robot Operating System, is a common middleware framework used in robotics to connect sensors, controllers, planning algorithms, visualization tools, and hardware drivers. Despite its name, ROS is not an operating system in the traditional kernel sense. It provides conventions and libraries for building distributed robot software. In ROS2, independent processes called nodes communicate through typed topics, request/response services, actions, parameters, launch files, and a DDS-based discovery and transport layer. A typical robotics system uses a hardware driver node to publish sensor data and accept commands, while other nodes perform mapping, planning, control, visualization, or logging. Tools such as `ros2 topic echo`, `ros2 service call`, `rqt`, and RViz are then used to inspect and interact with the running graph.

The project includes a separate ROS2 Foxy integration workspace in its own GitHub repository. The ROS-independent runtime remains in `CS-500`, while the ROS repository contains only interfaces, the broker driver, the ROS GUI backend, and bringup files. A `tello_ros2.repos` manifest imports the `dev` branch of `CS-500`. `colcon` discovers `tello_core/package.xml` as a plain CMake package and compiles the core against the same Ubuntu 20.04, Qt5, FFmpeg, compiler, and glibc environment as the ROS packages. This avoids both ROS dependencies in the core and binary compatibility assumptions between operating-system versions.

The resulting workspace is organized around five build units:

1. source-built `tello_core`, which exports the runtime and shared Qt UI without depending on ROS;
2. `tello_interfaces`, which defines Tello-specific messages and services;
3. `tello_driver`, which implements the broker node that owns all drone UDP communication;
4. `tello_control_panel_ros`, which implements `RosBackend` and reuses the exported Qt UI;
5. `tello_bringup`, which starts the driver and ROS GUI together.

The source dependency is declared explicitly in the workspace manifest:

```yaml
repositories:
  CS-500:
    type: git
    url: https://github.com/Gfernandes10/CS-500.git
    version: dev
```

Starting from a machine with Ubuntu 20.04 and ROS2 Foxy installed, the workspace can be obtained and built from scratch as follows. The first command clones the ROS repository; `vcs import` then places the `CS-500` source tree declared by the manifest under the workspace's existing `src/` directory.

```bash
sudo apt update
sudo apt install -y \
  python3-vcstool \
  python3-rosdep \
  build-essential \
  cmake \
  pkg-config \
  libavformat-dev \
  libavcodec-dev \
  libavutil-dev \
  libswscale-dev \
  qtbase5-dev

git clone https://github.com/Gfernandes10/CS-500---ROS.git
cd CS-500---ROS

vcs import src < tello_ros2.repos
source /opt/ros/foxy/setup.bash
rosdep install --from-paths src --ignore-src -r -y
colcon build
```

If `rosdep` has not previously been initialized on the machine, `sudo rosdep init` and `rosdep update` must be run once before `rosdep install`. The `dev` branch selection for `CS-500` is stored in `tello_ros2.repos`, so no separate manual clone of the core repository is required.

After the build, the operator powers on the Tello, connects the computer to the drone's Wi-Fi network, sources Foxy and the workspace overlay, and starts the complete application:

```bash
source /opt/ros/foxy/setup.bash
source install/setup.bash
ros2 launch tello_bringup control_panel.launch.py
```

This starts `/tello/tello_driver_node` and the separate `tello_control_panel_ros` executable. With the default `auto_connect:=false`, the operator completes SDK initialization by pressing `Connect + SDK` in the Control Panel. The graph can be inspected from another terminal after sourcing the same environment:

```bash
source /opt/ros/foxy/setup.bash
source install/setup.bash
ros2 node list
ros2 topic list
ros2 service list
ros2 topic echo /tello/link_quality
```

The central ROS node is `tello_driver_node`. It is the only ROS-mode process that owns the `TelloClient` and therefore the only process that talks directly to the drone command UDP socket. This ownership model avoids conflicting command senders. The node subscribes to `/tello/cmd_vel` using `geometry_msgs/Twist`, clamps normalized command values to `[-1.0, 1.0]`, scales them to SDK RC values in `[-100, 100]`, and maps them to the Tello command `rc a b c d` as follows: `linear.y` to left/right, `linear.x` to forward/back, `linear.z` to up/down, and `angular.z` to yaw.

The ROS interface is summarized below so that the delivered bridge can be inspected with standard ROS tools.

| Interface | Direction | Type | Purpose |
|---|---|---|---|
| `/tello/state` | published by driver | `tello_interfaces/msg/TelloState` | structured telemetry from the SDK state channel |
| `/tello/battery` | published by driver | `std_msgs/msg/Int32` | convenience battery percentage topic |
| `/tello/connection_state` | published by driver | `std_msgs/msg/String` | SDK connection state for operators and tools |
| `/tello/link_quality` | published by driver | `tello_interfaces/msg/LinkQuality` | aggregate communication-quality and RC-safety state |
| `/tello/runtime_metrics` | published by driver | `tello_interfaces/msg/RuntimeMetrics` | telemetry, decoder, video, RC, and keepalive counters used by the shared GUI |
| `/tello/video/image_raw` | published by driver | `sensor_msgs/msg/Image` | decoded FFmpeg video frames |
| `/tello/diagnostics` | published by driver | `diagnostic_msgs/msg/DiagnosticArray` | diagnostic status for ROS tooling |
| `/tello/cmd_vel` | subscribed by driver | `geometry_msgs/msg/Twist` | normalized autonomous velocity command input |
| `/tello/manual_cmd_vel` | published by Control Panel in ROS mode | `geometry_msgs/msg/Twist` | manual RC input forwarded through the ROS command owner |

The main services exposed by the driver are:

| Service | Type | Purpose |
|---|---|---|
| `/tello/connect` | `std_srvs/srv/Trigger` | initialize the core client and enter SDK mode |
| `/tello/disconnect` | `std_srvs/srv/Trigger` | stop operation and release the SDK session |
| `/tello/takeoff` | `std_srvs/srv/Trigger` | request takeoff through the ROS command owner |
| `/tello/land` | `std_srvs/srv/Trigger` | request landing through the ROS command owner |
| `/tello/emergency` | `std_srvs/srv/Trigger` | request emergency motor stop |
| `/tello/streamon` | `std_srvs/srv/Trigger` | enable video streaming |
| `/tello/streamoff` | `std_srvs/srv/Trigger` | disable video streaming |
| `/tello/enable_autonomy` | `std_srvs/srv/Trigger` | allow `/tello/cmd_vel` to control the drone |
| `/tello/disable_autonomy` | `std_srvs/srv/Trigger` | block autonomous command input |
| `/tello/gui_command` | `tello_interfaces/srv/Command` | forward a GUI-requested SDK command through the broker |

The link-quality topic exposes the same aggregate safety-oriented state introduced in the core metrics layer. It includes the overall label, numeric score, `safe_for_nonzero_rc`, reason text, telemetry/video/command/RC sublabels, and RC cadence diagnostics. This makes communication health visible to future ROS controllers rather than only to the Qt Control Panel or CSV logs.

The driver also exposes services for connect, disconnect, takeoff, land, emergency, stream on, stream off, enabling autonomy, disabling autonomy, and forwarding GUI commands. Command arbitration is deliberately conservative. `/tello/cmd_vel` is accepted only when autonomy is explicitly enabled. Commands originating from the GUI or manual services switch the driver into manual override, block `/tello/cmd_vel`, and execute the requested GUI or service command through the broker. Discrete critical commands such as takeoff, land, emergency, stream on, and stream off can preempt autonomous command flow. Manual RC commands from the GUI are forwarded directly as RC commands and continue to block autonomous `/cmd_vel` until autonomy is explicitly re-enabled. The emergency service has highest priority.

The Qt Control Panel is shared through the ROS-neutral `ControlBackend` contract. The standalone executable `tello_control_panel` injects `StandaloneBackend`, which owns `TelloClient`, `StateReceiver`, FFmpeg, keepalive, and recovery. The ROS executable `tello_control_panel_ros` injects `RosBackend`, which owns only an `rclcpp` node, service clients, publishers, subscribers, and an executor thread. It consumes state, connection, link-quality, runtime-metrics, and video topics, publishes manual RC on `/tello/manual_cmd_vel`, and invokes driver services. Consequently, there is no runtime flag that can accidentally make the standalone executable compete for the ROS driver's UDP sockets.

The ROS launch path was also validated in basic real-drone operation. In this validation, the bringup launch started the driver and Control Panel, the GUI connected to the drone through `/tello/connect`, telemetry and video were visible through the ROS-mode GUI, and the ROS topics and services were available for external inspection and tooling.

### 4.8 Standalone Build And Operation

The core API can also be used without ROS. In standalone mode, the CLI tools and Qt Control Panel link directly against `tello_core` and communicate with the drone through the SDK UDP channels. This is the mode used for the real-drone experiments in this report.

The supported distribution path is a source build. This ensures that FFmpeg, Qt5, glibc, and compiler-runtime dependencies match the target Ubuntu system. A downstream CMake project can install the source-built package locally and consume its exported target:

```cmake
find_package(tello_core REQUIRED CONFIG)
target_link_libraries(my_app PRIVATE tello_core::tello_core)
```

Clone the `dev` branch for development, auditing, project evaluation, or rebuilding on another machine:

```bash
git clone --branch dev https://github.com/Gfernandes10/CS-500.git
cd CS-500
```

On Ubuntu or Debian, install the required development packages:

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  pkg-config \
  libavformat-dev \
  libavcodec-dev \
  libavutil-dev \
  libswscale-dev \
  qtbase5-dev
```

FFmpeg is required because the project supports only the FFmpeg Stream video path. Qt is required for the graphical Control Panel. OpenCV is optional and is not the primary video runtime path:

```bash
sudo apt install -y libopencv-dev
```

Build the standalone core, CLI, and Control Panel:

```bash
cmake -S tello_core -B build/tello_core_standalone

cmake --build build/tello_core_standalone
```


The offline tests can be run without a drone:

```bash
cmake -E chdir build/tello_core_standalone \
  ctest -L offline --output-on-failure
```

For a basic hardware check, power on the Tello, connect the computer to the Tello Wi-Fi network, wait about 10-15 seconds after power-on, and run:

```bash
./build/tello_core_standalone/tello_cli --once
```

For the standalone graphical application, run:

```bash
./build/tello_core_standalone/tello_control_panel
```

The normal standalone Control Panel workflow is:

1. power on the drone;
2. connect the computer to the Tello Wi-Fi network;
3. start `tello_control_panel`;
4. press `Connect + SDK`;
5. confirm that telemetry, battery, temperature, link quality, and video status update;
6. configure CSV export paths if experiment logging is needed;
7. use the Config tab for SDK commands, speed setting, and keyboard profile setup;
8. use the Operation tab for live video, state history, manual RC sliders, and keyboard RC control.

In standalone mode `tello_control_panel` owns the command channel directly. ROS operation uses the distinct `tello_control_panel_ros` executable through bringup, keeping the driver node as the only command owner.

## 5. Milestone Plan Traceability

The original milestone plan divided the project into eight phases. This section maps each planned phase to the delivered implementation.

An accompanying continuous evidence video is available here: [CS 500 evidence video](https://www.youtube.com/watch?v=wz3gtlSBJCw). The video demonstrates the delivered artifacts for Phases 0, 1, 2, 3, 5, and 6. Phase 4 is evidenced primarily by the experimental methodology, CSV-based measurements, and quantitative results in this report. Phase 7 is evidenced by this final report.

### 5.1 Phase 0: Project Definition and Architecture

**Planned goal.** Phase 0 was intended to define the project scope, study the DJI Tello SDK, choose technologies, create the source structure, define the core modules, and establish the CMake build system.

**Delivered work.** This phase was delivered. The project scope was defined around a modular C++ Tello communication stack. The architecture separates command, telemetry, video, metrics, GUI, and ROS integration. The implementation uses C++17, CMake, FFmpeg, Qt5, and ROS2 Foxy. The reusable `tello_core` is a ROS-independent CMake package, while a separate ROS2 workspace imports its source and provides the driver and ROS backend. This preserves the modular boundary without relying on precompiled runtime artifacts.

**Evidence included.** The SDK background, system architecture, component descriptions, and implementation sections document the final structure and design decisions. Section 3 includes the final system architecture diagram, showing the DJI Tello drone, UDP command/telemetry/video channels, the reusable `tello_core` layer, CLI tools, Qt Control Panel, metrics layer, and ROS2 bridge. The accompanying evidence video shows the SDK background, architecture diagram, and project structure.

### 5.2 Phase 1: Core Networking and Command Layer

**Planned goal.** Phase 1 was intended to implement UDP communication, command execution, timeout and retry behavior, logging, and a CLI tool for command testing.

**Delivered work.** This phase was delivered. `UdpSocket`, `CommandExecutor`, and `TelloClient` implement the command channel. The command socket was refined to use the SDK command port consistently and avoid ambiguous response ownership. The CLI supports one-shot commands, command watching, SDK initialization, keepalive, command metrics, and recovery state reporting. Operator-facing status and errors are printed by the CLI or displayed in the Control Panel, while `MetricsCollector` stores structured command results, attempt diagnostics, timestamped GUI messages, recovery events, and connection state in CSV exports.

**Evidence included.** The smoke test and command baseline in the experimental results section validate this phase with real-drone command latency, timeout, retry, and transient-recovery measurements. The packet-capture discussion provides additional evidence that periodic command delays were transport/drone-response events rather than local API parsing or GUI blocking. The accompanying evidence video shows CLI command execution and metrics CSV generation.

### 5.3 Phase 2: Telemetry System

**Planned goal.** Phase 2 was intended to implement continuous telemetry reception, parse raw state messages into structured data, provide thread-safe access, and record telemetry logs.

**Delivered work.** This phase was delivered. `StateParser` converts raw SDK state strings into structured telemetry. `StateReceiver` runs a background receiver, stores the latest state, buffers recent history, records state CSV data, and exposes thread-safe access to consumers.

**Evidence included.** The telemetry baseline, GUI baseline, and keyboard-flight runs validate packet freshness, packet age, interarrival gaps, state recording, and quality labels. Parser behavior is also covered by unit tests. The accompanying evidence video shows live telemetry reception, parsed state values, state plotting, and state CSV recording.


### 5.4 Phase 3: Video Stream Integration

**Planned goal.** Phase 3 was intended to receive, decode, and expose the drone video stream, including a minimal viewer or frame access path.

**Delivered work.** This phase was delivered with a design refinement. The final video implementation uses `VideoStreamReaderFfmpeg`, which opens the Tello H264 UDP stream directly through FFmpeg, decodes frames, converts them to RGB, and feeds both CLI metrics and the Qt Control Panel. FFmpeg Stream became the only supported video runtime path because it produced smoother real-flight video and reduced implementation risk.

**Evidence included.** The video baseline, GUI idle run, and keyboard-flight run validate decoded frame rate, video freshness, decoder stability, and GUI display behavior. The accompanying evidence video shows the FFmpeg Stream video path running in the Qt Control Panel.

### 5.5 Phase 4: Evaluation and Metrics

**Planned goal.** Phase 4 was intended to add command latency, telemetry rate, video FPS, timestamp logging, metadata-ready CSV exports, experiments, plots, and performance analysis.

**Delivered work.** This phase was delivered and became one of the central contributions of the project. `MetricsCollector` centralizes command, telemetry, video, GUI, keepalive, recovery, link-quality, and RC metrics. Experiments record metadata such as test ID, scenario, notes, and run mode.

**Evidence included.** The final experiment set covers command behavior, telemetry continuity, FFmpeg video, GUI operation, keyboard RC flight, power-cycle recovery, and Wi-Fi reconnect behavior. The Results section includes plots and quantitative summaries generated from these experiments. This phase is evidenced by the experimental methodology, recorded CSV files, generated figures, and analysis presented in the Results section rather than by a separate live demonstration.

### 5.6 Phase 5: ROS Integration

**Planned goal.** Phase 5 was intended to create a ROS2 package, wrap the core library in a ROS node, publish telemetry and camera data, subscribe to velocity commands, expose takeoff and landing services, and test the bridge with ROS tools. The plan named ROS2 Humble or a compatible ROS2 version; the delivered implementation uses ROS2 Foxy on Ubuntu 20.04.

**Delivered work.** This phase was delivered as a separate ROS2 Foxy workspace and prepared as an independent GitHub repository. The ROS implementation keeps the ROS layer thin: a `.repos` manifest imports the `dev` branch of the ROS-independent core, while the ROS repository supplies custom interfaces, the sole-owner driver, `RosBackend`, and bringup files. The shared Qt interface is compiled once from source for the target environment and reused by the standalone and ROS executables.

The delivered driver node wraps the existing core library and publishes telemetry, battery, connection state, video frames, diagnostics, and aggregate link quality. It subscribes to normalized velocity commands on `/tello/cmd_vel`, converts them to Tello RC commands, and exposes services for connection management, takeoff, landing, emergency stop, stream control, autonomy enable/disable, and GUI command forwarding. The Qt Control Panel can be launched in ROS mode so that the GUI sends commands through the ROS broker instead of talking directly to the drone. Offline build and interface tests were performed with `colcon build`, `colcon test`, and ROS command-line tools. The launch path was also validated with the real drone by connecting through the ROS service path and confirming ROS-mode telemetry and video in the GUI.

**Evidence included.** The ROS2 Integration section lists the delivered packages, topics, services, launch flow, command ownership model, command arbitration policy, and repository boundary. The accompanying evidence video shows the ROS2 workspace build, test run, custom packages, custom interfaces, and ROS-mode Control Panel launch. The final ROS build/test evidence showed four packages built successfully and four tests passing with zero errors, zero failures, and zero skipped tests.

### 5.7 Phase 6: Desktop GUI

**Planned goal.** Phase 6 was intended to build a simple Qt GUI with video, telemetry display, control buttons, and integration with the core library.

**Delivered work.** This phase was delivered with expanded scope. The Qt Control Panel includes SDK connection, FFmpeg video, telemetry plotting, state history, CSV export, logging, takeoff confirmation, emergency behavior on exit, keyboard profiles, RC sliders, a dedicated keyboard RC worker, an asynchronous command worker for blocking critical commands, runtime diagnostics, persistent CSV paths, aggregate link-quality status, battery, Wi-Fi, temperature, and recording indicators.

**Evidence included.** The GUI screenshots describe the delivered interface. The GUI idle and keyboard-flight experiments validate that the Control Panel can display video and telemetry while logging metrics, and that it can be used during real drone operation. The accompanying evidence video demonstrates the global status area, Config tab, Operation tab, logging/export workflow, live video, telemetry plotting, and keyboard-control workflow.

### 5.8 Phase 7: Final Report

**Planned goal.** Phase 7 was intended to produce the final technical report documenting the project scope, architecture, implementation, experiments, results, limitations, and future work.

**Delivered work.** This phase was delivered through this report. The report documents the architecture, SDK background, implementation, experiments, results, limitations, and milestone traceability. Figures and experiment summaries are included for the final PDF.

**Evidence included.** This report is the evidence for Phase 7. It includes the major sections required for a technical project submission: problem definition, implementation, experiment methodology, results, discussion, limitations, and future work.

All major planned technical components have been delivered at least to an initial functional level. Several phases were expanded based on real-drone testing, especially metrics, recovery, GUI diagnostics, keyboard RC safety, link-quality monitoring, and ROS command arbitration. 

## 6. Experimental Methodology

### 6.1 Motivation and Experimental Logic

The campaign was designed as a sequence of isolation experiments. Each stage adds one source of workload or one failure mode only after the preceding layer is understood:

| ID | Rationale |
|---|---|
| E1 | Reject an invalid test session before collecting longer runs |
| E2 | Isolate synchronous command behavior and investigate the WSL2 delay |
| E3 | Determine whether telemetry continuity also changes between WSL2 and native Linux |
| E4 | Add FFmpeg video while excluding Qt |
| E5 | Add GUI rendering, plotting, and recording |
| E6 | Validate RC cadence and neutral safety on the ground |
| E7 | Add flight dynamics under combined command, RC, telemetry, video, and GUI load |
| E8 | Test complete session recovery after restarting the drone |
| E9 | Isolate host Wi-Fi loss without intentionally restarting the drone |
| E10 | Define end-to-end ROS2 acceptance for the released core and bridge |

E2 and E3 were the decision point for the operating environment. The WSL2 command path crossed the Linux UDP socket, WSL virtual networking, Hyper-V/Windows networking, the Windows Wi-Fi stack, and the physical radio. Native Linux removed the virtualized and Windows layers while preserving the C++ client, command interval, drone, and physical adapter. Later experiments were native-only because repeating flight and GUI tests on a platform already associated with periodic command interruption would add risk without answering a new project question.

Each primary repeated experiment used three independent runs. Packet capture was treated as a separate diagnostic factor and was not silently pooled with primary runs. After E2, an additional PCAP was required only if the CSV exposed a new timeout, stale interval, recovery, or unexplained gap.

### 6.2 E1: Session Gate

E1 entered SDK mode and issued a simple query before longer work. Its purpose was operational rather than statistical: association and ping do not prove that the drone is accepting SDK commands. A passing gate required an OK command result with no retry, timeout, or final failure.

### 6.3 E2: Command Baseline and WSL2 Investigation

E2 sent one battery query per second for approximately 120 seconds while the drone remained stationary. Three primary and three diagnostic PCAP runs were performed in WSL2, followed by the same six-run structure on native Linux. Primary metrics were per-command median, p95, maximum and steady-state latency, final failures, retries, timeouts, recovered transient events, UDP receive-wait time, and recovery timing.

The diagnostic logic was causal. A CSV send attempt without an outgoing packet at the capture point would implicate the application/socket path. An outgoing request with no response would place the loss below or beyond that capture point. A timely captured response accompanied by an API timeout would implicate socket ownership, receive synchronization, parsing, or executor logic. Because a WSL-interface capture is above the physical Windows Wi-Fi adapter, it cannot by itself prove over-the-air transmission; the native physical-interface captures remove that specific ambiguity.

### 6.4 E3: Telemetry Baseline

E3 recorded the asynchronous state stream for 150 seconds without FFmpeg or Qt. WSL2 primary and diagnostic runs were retained, and three native primary runs were added. Metrics included receive-rate EMA, packet age, interarrival median/p95/maximum, gaps above 300/500/1000 ms, invalid packets, receiver timeouts/errors, and OK/DEGRADED/STALE proportions. The purpose was to test whether the E2 problem represented a link-wide interruption or a command-path/environment effect.

### 6.5 E4: Native CLI Video Baseline

E4 added FFmpeg decoding while still excluding Qt. Three 150-second native runs recorded frame dimensions, decoded frames, keyframes, decoder rate, frame age, continuity labels, decoder errors, telemetry freshness under video load, and any recovery action. This separates decoder/transport behavior from GUI rendering overhead.

### 6.6 E5: Native GUI and Video at Idle

E5 ran the complete Qt Control Panel with telemetry, video, plot refresh, and state recording while the drone remained stationary. Three GUI metrics CSVs were paired with three state recordings. The analysis covered telemetry/video age and quality, decoder FPS/errors, vision and state timer delays, refresh/conversion/scaling/paint time, frame and command mutex wait, UI frame accounting, and state-recording inter-sample gaps.

### 6.7 E6: Grounded RC Safety

E6 enabled the keyboard RC worker without takeoff. Neutral output was recorded before and after short pulses on each mapped direction. The experiment measured deduplicated RC cadence, maximum packet gap, pulse duration, return-to-neutral delay, channel values, blackout count, link quality, safety overrides, and whether any nonzero command occurred while `safe_for_nonzero_rc` was false.

### 6.8 E7: Combined Flight and Dynamic Response

E7 was the full-system flight experiment. Each repetition recorded takeoff, hover, short pulses in vertical, yaw, forward/back, and lateral directions, neutral intervals, landing, telemetry-confirmed touchdown, video, GUI timing, and link/RC safety.

Dynamic response was estimated by grouping consecutive nonzero RC samples into pulses. Latency was measured from pulse onset to the first later telemetry sample crossing a conservative expected-direction threshold: 5 cm in `h` or `tof` for vertical motion, 3 degrees in yaw, and 2 degrees in roll/pitch for lateral or forward/back response. Direct `vgx`/`vgy` values remained near zero, so roll and pitch are attitude-response proxies rather than translational displacement. At approximately 10 Hz telemetry, these values are quantized upper-bound observations; they are not a fitted vehicle model or motion-capture ground truth.

### 6.9 E8: Drone Power-Cycle Recovery

E8 left the video-watch process running while the drone was powered off and restarted. Recovery success required restoration of the command channel, fresh telemetry, decoded video, and a final CONNECTED/OK/OK/OK state. Times were measured from the first non-OK link sample to each restored channel. Recovery stages, timeout rows, and the hard-recovery flag were retained rather than reporting only the successful endpoint.

### 6.10 E9: Host Wi-Fi Loss

E9 kept the drone powered while the host temporarily left and rejoined the Tello network. The command-only watch process was not restarted. The primary endpoint was the first successful command after the LOST event, together with failed commands, timeouts, connection-state transitions, outage count, and final command result.

### 6.11 E10: ROS2 Acceptance

E10 specifies an end-to-end acceptance run for the driver ownership model, telemetry/video/link-quality publishers, command services, RC input, and ROS-mode Control Panel. The software implementation is described in Section 4.7. However, no CSV, rosbag, launch transcript, or other structured E10 artifact was retained with the final experiment evidence, so the report does not assign quantitative ROS2 acceptance results.

## 7. Results

### 7.1 WSL2 Command Delay and Native-Linux Resolution

All WSL2 E2 runs completed without a final public-command failure, but every one contained a recovered multi-attempt interruption. Routine per-run medians were approximately 32-34 ms and steady p95 values 36-38.5 ms. Diagnostic captures showed blackout windows of 2.401-3.180 seconds: outgoing requests remained visible at the WSL capture interface while incoming command responses were absent, after which SDK recovery restored operation. This rules out failure to call the UDP send path, but the WSL capture position cannot distinguish Hyper-V/NAT, Windows firewall or Wi-Fi management, the Windows driver, radio loss, or temporary drone silence.

The native result changed both routine performance and failure behavior. Across three primary and three captured native runs, all 708 command samples succeeded with zero retry, timeout, recovery, or multi-attempt command. Every physical-interface PCAP contained 119 outgoing requests and 119 matching responses. Per-run medians were 19 ms and steady p95 values 21-22 ms. Captured and uncaptured native groups were nearly identical, so packet capture did not materially change routine latency.

![WSL2 and native E2 comparison](../results/final_repeated/analysis_images/e2_wsl_vs_native_comparison.png)

The disappearance of the periodic event in all six native runs strongly associates it with the removed WSL2/Windows path or another environment-linked condition. It substantially reduces the likelihood of a deterministic defect in the shared C++ executor or a deterministic approximately 60-second Tello behavior. The experiment does not name one exact Windows/WSL component because synchronized Windows WLAN, firewall, driver, and physical-adapter evidence was not collected.

### 7.2 Telemetry Continuity Across Environments

E3 showed that telemetry remained continuous in both environments. The native group collected 4,450 valid packets with no invalid packets, receiver timeouts/errors, gaps of 300/500/1000 ms, or non-OK rows. The mean of per-run median rates was 9.88 Hz; mean median age was 50.67 ms, mean p95 age 95.53 ms, mean p95 interarrival 103.73 ms, and mean maximum interarrival 154.67 ms.

The WSL2 primary group likewise had zero qualifying gaps and zero non-OK rows, with a 9.74 Hz mean median rate, 49.00 ms mean median age, 97.80 ms mean p95 age, 104.33 ms mean p95 interarrival, and 155.67 ms mean maximum interarrival. The small differences do not indicate a materially different telemetry process. E3 therefore narrows the reason for abandoning WSL2 to the command-channel evidence rather than a general telemetry regression.

![E3 WSL2 and native telemetry](../results/final_repeated/analysis_images/e3_wsl_vs_native_telemetry.png)

### 7.3 Native FFmpeg Video Baseline

All E4 runs decoded 960×720 video near 30 fps. R01-R03 recorded 4,477, 4,493, and 4,503 frames and 150, 150, and 151 keyframes. Across runs, frame count averaged 4,491.0 (SD 13.12) and per-run decoder-FPS median averaged 29.997 (SD 0.002). Frame-age median averaged 15.67 ms, p95 29.53 ms, and maximum 35.0 ms.

All classified video rows were OK. Decoder error counters were 0, 15, and 1, confined to stream acquisition and not growing afterward. Telemetry remained continuous under video load, with zero 300/500/1000 ms gaps and mean p95 age 93.27 ms. No recovery or E4 diagnostic PCAP was triggered.

![Native E4 video continuity](../results/final_repeated/analysis_images/e4_native_video_continuity.png)

### 7.4 GUI Workload

E5 maintained telemetry and video at OK in every sampled row. Decoder medians were 29.999-30.002 fps; telemetry-age p95 was 95.5-98.0 ms and video-age p95 31-32 ms. Vision and state timer p95 values were 125 and 250 ms, matching their configured periods. Vision refresh p95 was 2 ms, scaling p95 1 ms, and frame/command mutex-wait p95 values were 0 ms.

State recording remained near 9.95-9.96 Hz with no sequence gaps. Maximum inter-sample gaps were 155, 212, and 155 ms. UI accounting displayed 88.12-90.44% of converted frames and reported 3,635-4,234 dropped display opportunities; these drops did not correspond to stale transport or decoder interruption.

![E5 GUI and video](../results/final_repeated/analysis_images/e5_gui_idle_analysis.png)

### 7.5 Grounded RC Safety

Each E6 run contained eight nonzero pulses. Deduplicated RC cadence had a 100 ms median, 101 ms p95, and 151 ms maximum. All pulses returned to neutral within 101-151 ms. There were no RC blackouts, safety overrides, unsafe nonzero commands, or non-OK link rows.

Pulse medians were 125.5 ms in R01 and 401 ms in R02/R03. Thus R02/R03 approximated the intended 500 ms operator hold more closely, while R01 tested shorter pulses. The CSV identifies keyboard-neutral and keyboard-worker sources, but records the profile as Default rather than the protocol's KeyboardTest name; aggression and mappings were not exported and cannot be independently verified. Recorded durations were approximately 97-105 seconds rather than the nominal 120 seconds.

![E6 RC magnitude and neutral return](../results/final_repeated/analysis_images/e6_rc_idle_analysis.png)

### 7.6 Flight Operation and Dynamic Response

All E7 runs show telemetry-confirmed takeoff, an OK landing command, and terminal height/ToF consistent with touchdown. Telemetry and video were OK throughout all three runs. Decoder medians were approximately 30 fps, GUI vision timer p95 was 125 ms, command-mutex p95 was 0 ms, and no unsafe nonzero RC or safety override occurred. R01 alone contained two non-OK aggregate-link samples; neither coincided with unsafe nonzero output.

RC cadence remained centered at 100 ms. Per-run p95/maximum gaps were 130.4/200, 150/250, and 101/201 ms. Telemetry-derived takeoff occurred at 7.59, 6.07, and 6.78 seconds, and touchdown at 97.28, 102.44, and 122.76 seconds.

![E7 complete repeated-flight state matrix](../results/final_repeated/analysis_images/e7_all_state_subplots_repeated.png)

The state matrix overlays all three repetitions relative to takeoff and exposes the principal attitude, velocity, height/ToF, barometer, battery, temperature, and acceleration measurements in one figure. It also makes the limitation of the SDK horizontal velocity fields visible: `vgx` and `vgy` remained close to zero even when attitude changed.

The conservative pulse-response detector produced:

| Axis | Proxy | Detected/total pulses | Median detected latency | Range |
|---|---|---:|---:|---:|
| left/right | roll | 2/12 | 409.5 ms | 407-412 ms |
| forward/back | pitch | 4/12 | 470.0 ms | 341-614 ms |
| up/down | height/ToF | 6/13 | 713.0 ms | 24-1,044 ms |
| yaw | yaw | 12/12 | 371.5 ms | 317-447 ms |

![E7 estimated response latency](../results/final_repeated/analysis_images/e7_rc_response_latency_repeated.png)

![E7 command-aligned transient responses](../results/final_repeated/analysis_images/e7_command_aligned_dynamics.png)

Yaw provides the strongest repeated evidence because every pulse crossed the direct yaw threshold. Roll/pitch detection is sparse, so those results show first observable attitude response only and should not be generalized as translational latency. A pulse that did not cross a threshold is not proof that the drone did not move; it means the recorded proxy did not satisfy the conservative criterion within the pulse plus 750 ms window. Vertical results are direct but more variable because height and ToF are discrete, noisy, and sampled asynchronously.

### 7.7 Recovery

E8 recovered in 3/3 power-cycle trials. Command restoration occurred 18.236-18.263 seconds after outage onset, telemetry at 19.237-19.263 seconds, and video at 19.263-20.238 seconds. Every run recorded two TIMEOUT recovery rows before OK, traversed `power_command` then `power_streamon`, set the hard-recovery flag, and ended CONNECTED/OK/OK/OK.

![E8 recovery milestones](../results/final_repeated/analysis_images/e8_power_cycle_recovery.png)

E9 also recovered in 3/3 host-Wi-Fi trials. The first successful command followed the LOST event after 19.425, 19.455, and 10.276 seconds. R01/R02 each recorded four failed-command rows including two timeouts; R03 recorded two errors and no timeout. Every run recorded one LOST event, traversed CONNECTED → RECOVERING → CONNECTED, and ended with command result OK. Telemetry/video were NO_DATA by design because E9 used command-only watch mode.

![E9 command reconnection](../results/final_repeated/analysis_images/e9_wifi_reconnect.png)

E8 ran for approximately 84 seconds rather than the nominal 180 seconds and E9 for approximately 81-82 seconds rather than 120 seconds. The recovery endpoints were reached in every recorded window, but the runs do not establish long post-recovery endurance.

### 7.8 Result Summary

| Experiment | Final evidence |
|---|---|
| E1 | Both retained smoke gates passed without retry, timeout, or failure |
| E2 | WSL2 interruption in every run; 708/708 native commands without retry/timeout/recovery |
| E3 | No gap ≥300 ms or non-OK row in WSL2 or native groups |
| E4 | 960×720 at approximately 30 fps; video and telemetry continuous |
| E5 | Full GUI workload without stale transport or mutex contention |
| E6 | 100 ms median RC cadence, bounded neutral return, no unsafe nonzero RC |
| E7 | Three completed flights; fresh telemetry/video; direct yaw and vertical response evidence |
| E8 | 3/3 full command/telemetry/video recoveries |
| E9 | 3/3 command reconnects after host Wi-Fi loss |
| E10 | Implementation exists, but no structured acceptance artifact was retained |

## 8. Discussion

The experimental sequence supports the architectural decision to place drone communication in one reusable core. E2 is the strongest diagnostic result: identical application behavior was not observed across operating environments. Complete native physical-interface request/response pairs and the absence of all native retries substantially reduce the probability of a deterministic executor defect. At the same time, the experiment does not overclaim an exact WSL2 subcomponent because the original WSL capture was not at the Windows physical adapter.

E3 adds an important boundary. Telemetry remained healthy under WSL2 even while the command campaign repeatedly exposed synchronous response loss. Therefore, a generic claim that “the entire Wi-Fi link failed every minute” is not supported. The actionable engineering conclusion is narrower: native Linux removed the observed command-path risk and was the appropriate platform for subsequent real-flight evaluation.

E4 and E5 show that FFmpeg and Qt did not reintroduce the blackout. Video decoding remained near 30 fps, telemetry remained fresh, and GUI refresh costs were small relative to timer periods. UI display drops are therefore an application rendering/accounting behavior, not evidence of transport loss.

E6 and E7 connect communication quality to safety. The independent RC worker maintained approximately 100 ms cadence and returned to neutral without unsafe nonzero output. Since RC is a no-wait stream, repeated output and neutral fallback matter more than acknowledgment latency for each packet. A long network outage can still delay neutral delivery, so the aggregate link state and `safe_for_nonzero_rc` flag should gate future autonomous commands.

The dynamic-response analysis is deliberately conservative. The Tello telemetry rate, quantized height/ToF, and nearly uninformative horizontal velocity fields prevent high-fidelity system identification. Yaw response is repeatable, vertical response is observable but variable, and horizontal results are limited to attitude proxies. These data are sufficient to demonstrate command-to-motion correspondence, but not to identify a control-ready dynamic model.

Finally, E8 and E9 show two different recovery paths: rebuilding a complete command/telemetry/video session after drone restart, and restoring synchronous commands after host Wi-Fi loss. Both succeeded in every recorded trial. The shortened recorded durations limit endurance claims but do not erase the observed recovery milestones.

## 9. Limitations

1. The campaign used one DJI Tello, one native host, one adapter, and one physical test environment.
2. WSL2/native comparison had three primary runs per condition and was not randomized or simultaneous.
3. Native reproduction identifies an environment-linked cause but not the exact Hyper-V, Windows firewall, WLAN management, or driver mechanism.
4. E6, E8, and E9 recorded shorter windows than specified by the protocol; E6 profile/aggression/mapping metadata were incomplete.
5. E7 uses onboard telemetry rather than external motion capture. Horizontal response relies on roll/pitch proxies, and 10 Hz telemetry quantizes latency.
6. The campaign does not evaluate maximum distance, thermal endurance, subjective image quality, or closed-loop position control.
7. No structured E10 ROS2 acceptance artifact was retained, so ROS2 runtime performance is not quantified in the results.

## 10. Future Work

Future work should prioritize evidence and control readiness rather than adding parallel communication paths:

1. repeat E10 with a retained launch transcript or rosbag covering telemetry, video, link quality, services, RC input, and GUI/manual ownership;
2. record the active keyboard profile, aggression, and mappings directly in experiment metadata;
3. use external motion capture or vision tracking to estimate translational response and fit a dynamic model;
4. run randomized native/Windows-side diagnostics only if exact WSL2 root-cause localization remains necessary;
5. extend E8/E9 post-recovery windows to evaluate session endurance;
6. gate any future closed-loop controller with `safe_for_nonzero_rc`, freshness, and explicit neutral fallback;
7. add automated tests for command recovery, UDP loopback, link-quality classification, and CSV schema stability.

## 11. Conclusion

The project delivered a modular C++ Tello runtime with reusable command, telemetry, FFmpeg video, metrics, RC safety, Qt operation, recovery, and ROS2 integration components. The repeated campaign evaluated those components incrementally rather than treating a successful flight as sufficient evidence.

The central investigation resolved the practical deployment question. WSL2 produced a recovered 2.4-3.2 second command interruption in every run, while 708 native command samples completed without retry, timeout, or recovery and native physical-interface captures showed complete request/response pairs. Native telemetry, video, GUI, grounded RC, and flight tests then remained stable under their recorded workloads. Power-cycle and host-Wi-Fi recovery succeeded in all three trials each.

The evidence supports native Linux as the runtime environment for this system. It also supports the core engineering claims: shared communication ownership, diagnosable transport behavior, approximately 30 fps video, responsive GUI scheduling, bounded RC neutral return, observable flight response, and multi-channel recovery. The principal remaining gaps are precise dynamic identification and a retained end-to-end ROS2 acceptance record.

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
| `E6-RC-IDLE` | grounded RC cadence, channel mapping, neutral return, and safety |
| `E7-KBD-RESPONSE` | airborne keyboard RC response, video, and telemetry behavior |
| `E8-PWR-CYCLE` | recovery after drone restart |
| `E9-WIFI-LOSS` | command-channel recovery after Wi-Fi loss |
| `E10-ROS-END-TO-END` | ROS2 acceptance procedure; no structured run artifact retained |

## Appendix C: Build Environment Notes

The project uses CMake and C++17. Video support requires FFmpeg development packages for `libavformat`, `libavcodec`, `libavutil`, and `libswscale`. FFmpeg is a required dependency for the video-enabled build because the project supports only the FFmpeg Stream video path.

## Appendix D: Offline Tests

The current offline test suite can be executed without a connected drone:

```bash
cmake -E chdir build/tello_core_standalone \
  ctest -L offline --output-on-failure
```

The existing offline tests are:

| Test | Purpose |
|---|---|
| `unit_state_parser` | validates parsing of Tello SDK telemetry state packets into structured state fields |
| `unit_metrics_collector` | validates central metrics aggregation, CSV output behavior, and quality-related metrics used by the experiment logs |
| `unit_control_panel_offscreen` | starts the Qt Control Panel with the standalone backend using the offscreen platform and verifies clean initialization and shutdown |

These tests cover the telemetry parser promised in the milestone plan and part of the metrics infrastructure used for evaluation. Additional offline tests for command retry behavior, UDP loopback behavior, `StateReceiver` runtime behavior, link-quality classification, and CSV schema stability would further harden the system, but those were not explicitly required as automated tests in the original milestone plan.
