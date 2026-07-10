# DJI Tello C++ Runtime And Control Panel

This repository contains the CS 500 DJI Tello project: a reusable C++17 runtime for the Tello SDK, command-line diagnostics, a Qt Control Panel, FFmpeg video decoding, telemetry/state recording, structured metrics, recovery behavior, and experiment/report material.

The core runtime can be used directly from this repository, installed from a versioned release, or consumed by the separate [ROS2 integration repository](https://github.com/Gfernandes10/CS-500---ROS).

## Main Components

- `tello_core`: reusable UDP command, telemetry, FFmpeg video, recovery, and metrics library.
- `tello_cli`: command, telemetry, video, keepalive, and recovery diagnostics.
- `tello_control_panel`: Qt application for live operation, video, telemetry plots, keyboard RC, logging, and CSV export.
- `MetricsCollector`: central structured metrics and timestamped event-log schema used by CLI and GUI exports.
- `docs`: architecture, channel documentation, experiment procedures, and final report draft.
- `notebooks`: reproducible experiment analysis and report figures.

## Requirements

On Ubuntu or Debian:

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

FFmpeg development libraries are required. FFmpeg Stream is the only supported video runtime path. OpenCV is optional and is not used as a second video decoder.

## Build

From the repository root:

```bash
cmake -S tello_core -B build/tello_core_standalone
cmake --build build/tello_core_standalone
```

The main executables are:

```text
build/tello_core_standalone/tello_cli
build/tello_core_standalone/tello_control_panel
```

## Offline Tests

The offline suite does not require a drone:

```bash
ctest --test-dir build/tello_core_standalone \
  -L offline \
  --output-on-failure
```

The current tests cover telemetry parsing and central metrics/CSV behavior. Hardware-dependent command tests are disabled by default.

## Connect To The Drone

1. Power on the DJI Tello.
2. Connect the computer to the Tello Wi-Fi network.
3. Wait approximately 10-15 seconds after power-on.
4. Run the smoke check:

```bash
./build/tello_core_standalone/tello_cli --once
```

The CLI enters SDK mode and performs a basic `battery?` query.

## Command-Line Diagnostics

Show all supported arguments:

```bash
./build/tello_core_standalone/tello_cli --help
```

Common modes are:

```bash
./build/tello_core_standalone/tello_cli --watch
./build/tello_core_standalone/tello_cli --state-watch
./build/tello_core_standalone/tello_cli --video-watch
```

Use `--metrics-csv`, `--test-id`, `--scenario`, and `--notes` to produce structured experiment output. CLI status messages are printed to the terminal; detailed command, telemetry, video, recovery, and event data are exported through `MetricsCollector`.

## Qt Control Panel

Start the standalone panel:

```bash
./build/tello_core_standalone/tello_control_panel
```

The normal workflow is:

1. click `Connect + SDK`;
2. confirm telemetry, battery, temperature, link quality, and FFmpeg video;
3. configure GUI metrics and state CSV paths;
4. start recording before an experiment;
5. use the Config tab for profiles and SDK commands;
6. use the Operation tab for telemetry, video, manual RC, and keyboard control;
7. stop recording and export both CSV files.

The panel records user-visible messages with timestamps in the GUI metrics CSV. Blocking command/recovery work runs outside the Qt event loop, and keyboard RC uses a dedicated worker with neutral fallback.

## Experiments And Analysis

- [Development experiment procedures](docs/08_experiments.md)
- [Final repeated experiment protocol](docs/09_final_repeated_experiment_protocol.md)
- [Final analysis notebook](notebooks/final_experiment_analysis.ipynb)
- [CS 500 final report draft](docs/CS500_final_report_draft.md)

The final repeated protocol defines three independent runs for the main command, telemetry, video, GUI, and RC experiments, plus recovery and ROS2 acceptance procedures.

## Versioned Releases

GitHub Releases provide a runtime-only `tello_core.tar.gz` containing the public headers, static library, CMake package metadata, CLI, Qt Control Panel, and runtime resources.

Downstream CMake projects can use the installed/extracted release with:

```cmake
find_package(tello_core REQUIRED)
target_link_libraries(my_app PRIVATE tello_core::tello_core)
```

See [Tello Core Release Install](docs/TELLO_CORE_RELEASE_INSTALL.md) for the release creation and consumption workflow.

## ROS2

ROS2 Jazzy integration is maintained in [CS-500---ROS](https://github.com/Gfernandes10/CS-500---ROS). The ROS workspace consumes the versioned runtime artifact rather than cloning the academic documentation and experiment data.

In ROS mode, `tello_driver_node` is the sole owner of the Tello UDP command channel. It publishes telemetry, video, diagnostics, and aggregate link quality; accepts autonomous and manual velocity inputs; and exposes connection, flight, stream, and autonomy services.

After building and sourcing the ROS workspace:

```bash
ros2 launch tello_bringup control_panel.launch.py
```

## Safety

- Test flight commands only in a clear area.
- Begin with low RC aggression and short movement pulses.
- Keep `land` and `emergency` accessible.
- Do not allow multiple processes to own UDP command port `8889`.
- Treat `STALE`, `BLACKOUT`, or `safe_for_nonzero_rc=false` as unsafe for sustained nonzero RC control.
- Allow the drone to cool between long back-to-back experiments.

## Demonstration

The continuous project evidence video is available on [YouTube](https://www.youtube.com/watch?v=wz3gtlSBJCw).
