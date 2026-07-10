# Native Linux Command-Channel Blackout Investigation

## Purpose

This document provides the context and procedure needed to continue the DJI Tello command-channel investigation on native Linux. It is intended both for the project author and for an AI assistant opening this workspace without access to the earlier conversation.

The central question is:

> Are the periodic command-channel interruptions caused by the C++ API, WSL2/Windows networking, the Wi-Fi adapter or driver, the radio environment, or the DJI Tello itself?

The next experiment should reproduce the existing E2 command baseline on native Ubuntu while changing as little else as possible.

## Existing WSL2 Evidence

The original repeated E2 experiments were executed inside WSL2. The observed kernel identified the environment as:

```text
6.18.33.2-microsoft-standard-WSL2
```

The available dataset contains:

- three primary command-baseline runs without packet capture: `R01`, `R02`, and `R03`;
- three diagnostic command-baseline runs with packet capture: `D01-PCAP`, `D02-PCAP`, and `D03-PCAP`;
- approximately 120 seconds per run;
- one `battery?` operation approximately every second;
- matching CSV and PCAP files under `results/final_repeated/`.

The current analysis notebook is:

```text
results/final_repeated_experiment_analysis.ipynb
```

### Current Findings

Across the six WSL2 runs:

1. routine command latency had a median of approximately 32-34 ms;
2. the per-run steady-state p95 was approximately 36-38.5 ms;
3. all commands ultimately completed successfully, with zero final failures;
4. every run contained a recovered multi-attempt interruption around the same part of the run, generally completing around 64-66 seconds;
5. the diagnostic PCAPs showed interruption windows of approximately 2.401-3.180 seconds;
6. during each captured interruption, outgoing UDP requests remained visible while incoming command responses were absent;
7. after recovery entered SDK mode again, command traffic returned to normal;
8. ordinary latency was not worse in the captured runs, so the current sample does not show that `tcpdump` caused the routine latency behavior.

One uncaptured run also contained additional delayed responses away from the main periodic interruption. Because that run had no PCAP, those individual delays cannot be localized to a transport layer using CSV evidence alone.

## What The Existing PCAP Proves

The WSL2 capture proves that the application called the UDP send path and that outgoing requests reached the WSL network capture point. It also proves that no corresponding incoming response reached that same capture point during the recorded blackout window.

It does **not** prove that the outgoing packet reached the physical Wi-Fi adapter or was transmitted over the air. The path in WSL2 may include:

```text
tello_cli
  -> Linux UDP socket
  -> WSL virtual network interface
  -> WSL/Hyper-V networking or NAT
  -> Windows network stack and firewall
  -> Windows Wi-Fi driver
  -> physical Wi-Fi radio
  -> DJI Tello
```

Therefore, the missing response could still originate from:

- WSL2 virtual networking or NAT;
- Hyper-V or Windows firewall processing;
- Windows network management or periodic Wi-Fi scanning;
- the Wi-Fi adapter or driver;
- radio interference;
- the drone temporarily not receiving or answering commands.

The repeated timing makes a periodic host/network mechanism plausible, but the current evidence is not sufficient to identify WSL2 as the cause.

## Native Linux Test Strategy

Native Linux removes the WSL2 virtual network, Hyper-V, and Windows networking layers from the path:

```text
tello_cli
  -> Linux UDP socket
  -> native Linux Wi-Fi stack and driver
  -> physical Wi-Fi radio
  -> DJI Tello
```

For a meaningful comparison, keep the following conditions as similar as possible:

- same laptop and Wi-Fi adapter;
- same drone and firmware;
- same room and approximate drone position;
- same camera/drone orientation;
- same one-second command interval;
- same 120-second run duration;
- similar battery level and drone temperature;
- no other Tello application running;
- drone stationary and motors off;
- three uncaptured runs and three captured diagnostic runs.

The operating-system environment should be recorded as an intentional changed variable.

## Ubuntu Compatibility

The standalone API can be built on another Ubuntu version because it uses:

- C++17;
- CMake 3.16 or newer;
- POSIX threads and UDP sockets;
- FFmpeg development libraries;
- Qt5 or Qt6 for the Control Panel.

Ubuntu 22.04 and Ubuntu 24.04 are appropriate targets for the standalone API. Ubuntu 24.04 is preferred if the ROS2 Jazzy workspace will also be tested, but ROS is not required for the E2 command baseline.

Do not reuse the WSL2 `build/` directory or WSL2 binaries. Rebuild from source on native Linux so the executable links against the native system's compiler, glibc, FFmpeg, and Qt libraries.

## Native Linux Setup

Clone the repository or open the existing source tree from native Linux:

```bash
git clone https://github.com/Gfernandes10/CS-500.git
cd CS-500
```

Install the standalone build dependencies:

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  pkg-config \
  libavformat-dev \
  libavcodec-dev \
  libavutil-dev \
  libswscale-dev
```

For the Qt Control Panel, install either Qt5:

```bash
sudo apt install -y qtbase5-dev
```

or Qt6:

```bash
sudo apt install -y qt6-base-dev
```

Qt is not required to execute the CLI-only E2 experiment, but the project CMake configuration supports both versions.

## Clean Native Build

Configure and build in a new native-only directory:

```bash
cmake -S tello_core -B build/tello_core_native
cmake --build build/tello_core_native
```

Run the offline test suite before connecting to the drone:

```bash
ctest --test-dir build/tello_core_native \
  -L offline \
  --output-on-failure
```

Record the native environment:

```bash
uname -a
cat /etc/os-release
cmake --version
g++ --version
ip link
iw dev
```

## Drone Preparation

1. Power on the Tello.
2. Connect native Ubuntu directly to the Tello Wi-Fi network.
3. Wait approximately 10-15 seconds after power-on.
4. Confirm that the drone is reachable:

```bash
ping -c 3 192.168.10.1
```

5. Run the smoke test:

```bash
mkdir -p results/final_repeated/pcap

./build/tello_core_native/tello_cli --once \
  --metrics-csv results/final_repeated/E1-SMOKE-CMD-NATIVE.csv \
  --test-id E1-SMOKE-CMD-NATIVE \
  --scenario command-smoke-native-linux \
  --notes "native Linux session connectivity gate"
```

Do not continue if the smoke test cannot enter SDK mode or query the battery.

## Native Runs Without Packet Capture

Perform three independent 120-second runs. Do not overwrite any output.

### R01

```bash
./build/tello_core_native/tello_cli --watch \
  --interval-ms 1000 \
  --duration-s 120 \
  --metrics-csv results/final_repeated/E2-CMD-BASE-NATIVE-R01.csv \
  --test-id E2-CMD-BASE-NATIVE-R01 \
  --scenario command-baseline-native-linux \
  --notes "native Linux; no pcap; independent repetition 1 of 3"
```

Repeat using `R02` and `R03`, updating both the filename, test ID, and repetition note.

Allow the drone to cool if its temperature is significantly different between repetitions.

## Native Runs With Packet Capture

Identify the physical Wi-Fi interface:

```bash
iw dev
```

Prefer capturing directly on that interface, for example `wlp2s0`, rather than using `-i any`. Replace `<wifi-interface>` below with the actual interface name.

### Terminal 1: Start PCAP

```bash
sudo tcpdump -i <wifi-interface> -nn -s 0 \
  'host 192.168.10.1 and udp' \
  -w results/final_repeated/pcap/E2-CMD-BASE-NATIVE-D01-PCAP.pcap
```

If direct interface capture is unavailable, use `-i any` and document that choice.

### Terminal 2: Run E2

```bash
./build/tello_core_native/tello_cli --watch \
  --interval-ms 1000 \
  --duration-s 120 \
  --metrics-csv results/final_repeated/E2-CMD-BASE-NATIVE-D01-PCAP.csv \
  --test-id E2-CMD-BASE-NATIVE-D01-PCAP \
  --scenario command-baseline-native-linux \
  --notes "native Linux; with pcap; diagnostic repetition 1 of 3"
```

After the CLI finishes, stop `tcpdump` with `Ctrl+C` in Terminal 1.

Repeat the same procedure as `D02-PCAP` and `D03-PCAP`.

## Optional Native Network Diagnostics

If practical, collect operating-system evidence during at least one diagnostic run.

Record interface counters once per second:

```bash
mkdir -p results/final_repeated/native_link_diag
while true; do
  date --iso-8601=ns
  ip -s link show <wifi-interface>
  sleep 1
done | tee results/final_repeated/native_link_diag/ip_link_E2.log
```

Record kernel and NetworkManager events in separate terminals:

```bash
sudo dmesg -w \
  | tee results/final_repeated/native_link_diag/dmesg_E2.log
```

```bash
journalctl -fu NetworkManager \
  | tee results/final_repeated/native_link_diag/networkmanager_E2.log
```

Stop these commands with `Ctrl+C` after the experiment. Record whether they were active, because diagnostics can add a small amount of host workload.

## Files Expected After Native Testing

```text
results/final_repeated/E2-CMD-BASE-NATIVE-R01.csv
results/final_repeated/E2-CMD-BASE-NATIVE-R02.csv
results/final_repeated/E2-CMD-BASE-NATIVE-R03.csv

results/final_repeated/E2-CMD-BASE-NATIVE-D01-PCAP.csv
results/final_repeated/E2-CMD-BASE-NATIVE-D02-PCAP.csv
results/final_repeated/E2-CMD-BASE-NATIVE-D03-PCAP.csv

results/final_repeated/pcap/E2-CMD-BASE-NATIVE-D01-PCAP.pcap
results/final_repeated/pcap/E2-CMD-BASE-NATIVE-D02-PCAP.pcap
results/final_repeated/pcap/E2-CMD-BASE-NATIVE-D03-PCAP.pcap
```

Optional diagnostics should be stored under:

```text
results/final_repeated/native_link_diag/
```

## Analysis Questions

The native results should be compared with the existing WSL2 runs using the following questions:

1. Does a recovered multi-attempt event still occur in every native run?
2. Does it still occur around 60-66 seconds?
3. Are routine median and p95 command latency similar to the WSL2 values?
4. During a native captured interruption, are outgoing `battery?` requests visible on the physical Wi-Fi interface?
5. Are incoming responses absent from the same physical-interface capture?
6. Do interface counters report dropped packets or errors at the same time?
7. Do `dmesg` or NetworkManager report scanning, reconnect, power-management, firmware, or driver events?
8. Does packet capture alter routine latency on native Linux?

## Interpretation Guide

### Blackout Disappears On Native Linux

If the repeated interruption disappears across the native runs, the result is strong evidence that the WSL2/Windows path contributed to the original behavior. It would not identify the exact WSL2 or Windows component, but it would substantially reduce the likelihood of a deterministic defect in the C++ command implementation or the drone.

### Blackout Remains At Approximately The Same Time

If the interruption remains periodic and similar on native Linux, WSL2 becomes much less likely as the root cause. Investigate:

- Wi-Fi adapter firmware and driver behavior;
- periodic Wi-Fi scanning or power management;
- radio interference;
- drone command-channel behavior;
- a periodic mechanism inside the API, using the existing internal timing metrics.

### Outgoing Requests Are Missing From Native PCAP

If the API CSV says a command was attempted but the physical-interface PCAP does not show it, investigate the application send path, process scheduling, socket behavior, or the native network stack.

### Outgoing Requests Are Present But Responses Are Missing

If requests are present on the physical Wi-Fi capture but responses are absent, the evidence moves below or beyond the API send path. The next candidates are the radio link, Wi-Fi driver receive path, or the drone not answering.

### Responses Are Present But The API Times Out

If a timely response is visible on the native physical-interface PCAP but the API reports a timeout, investigate the C++ receive socket, port ownership, parsing, synchronization, and command executor. This would be direct evidence of an API-side receive problem.

## Instructions For The Next AI Session

When this workspace is opened on native Linux, the assisting AI should:

1. read this document first;
2. inspect `results/final_repeated_experiment_analysis.ipynb` for the existing WSL2 analysis method;
3. inventory all `E2-CMD-BASE-NATIVE-*` CSV and PCAP files;
4. verify file durations, row counts, final failures, retries, timeouts, and recoveries;
5. parse UDP port 8889 from each native PCAP;
6. compare WSL2 and native Linux at the per-run level;
7. keep captured and uncaptured runs separate;
8. update the notebook with a dedicated WSL2-versus-native comparison section;
9. clearly separate direct PCAP evidence from root-cause inference;
10. avoid changing the API before the native evidence has been evaluated.

The immediate goal is diagnosis, not implementation. A correction should only be selected after determining which layer fails during the periodic blackout.
