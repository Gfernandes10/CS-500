# Final Repeated Experiment Protocol

## Purpose

This protocol defines the final compact experiment set for the CS 500 report. It is based on the development procedures in `08_experiments.md`, but it focuses only on experiments that provide distinct evidence and adds independent repetitions where statistical comparison is useful.

The protocol has four goals:

1. measure command, telemetry, video, GUI, and RC behavior under controlled conditions;
2. separate core transport behavior from GUI and flight load;
3. quantify run-to-run variation instead of relying on one representative run;
4. validate recovery and ROS2 integration as functional system behaviors.

## Consolidation Decisions

The final set makes the following changes to the earlier development matrix:

1. `E1-SMOKE-CMD` remains a session gate, not a statistical result. Run it once after every drone power-on or Wi-Fi reconnection.
2. The former standalone keyboard-profile experiment is merged into `E6-RC-IDLE`. Profile persistence is a precondition, while the experiment measures the dedicated RC worker safely on the ground.
3. The development-only video-reset experiment is removed. Power-cycle recovery in `E8-PWR-CYCLE` exercises the more complete SDK and video recovery path.
4. `E9-WIFI-LOSS` remains separate from power-cycle recovery because it isolates command-channel reconnect without intentionally resetting the drone.
5. `E10-ROS-END-TO-END` is added because ROS2 is a planned project deliverable and should have direct runtime evidence.
6. Thermal endurance, maximum-distance, and subjective image-quality experiments are excluded. They depend strongly on environment or hardware condition and do not directly evaluate the software architecture.
7. Packet capture is an optional diagnostic extension for experiments that exercise command, telemetry, video, or RC transport. Captured diagnostic runs are labelled separately and are not silently mixed with the primary statistical repetitions.

## Final Matrix

| ID | Evidence | Repetitions | Flight | Optional PCAP |
|---|---|---:|---|---|
| `E1-SMOKE-CMD` | SDK connectivity gate | once per test session | No | No |
| `E2-CMD-BASE` | command latency and transient recovery | 3 | No | Yes |
| `E3-STATE-CLI` | telemetry-only baseline | 3 | No | Yes |
| `E4-VIDEO-CLI` | FFmpeg video plus telemetry without Qt | 3 | No | Yes |
| `E5-GUI-VID-IDLE` | GUI, video, telemetry, and recording while idle | 3 | No | Yes |
| `E6-RC-IDLE` | profile persistence, RC cadence, and neutral safety | 3 | No | Yes |
| `E7-KBD-RESPONSE` | RC response, GUI, telemetry, and video in flight | 3 | Yes | Yes |
| `E8-PWR-CYCLE` | complete SDK/video recovery after restart | 3 recovery events | No | Yes |
| `E9-WIFI-LOSS` | command reconnect after host Wi-Fi interruption | 3 recovery events | No | Yes |
| `E10-ROS-END-TO-END` | ROS topics, services, video, and ownership model | 1 acceptance run | Optional | On anomaly |

## Recorded Execution Status - 2026-07-10

The current campaign uses Ubuntu 20.04.6 native Linux on Wi-Fi interface `wlx503eaa237a02`. The native standalone binary is under `build/tello_core_native/`; output names include `NATIVE` so they cannot be confused with the earlier WSL2 data.

| Experiment | Recorded evidence | Decision |
|---|---|---|
| `E2-CMD-BASE` | 3 native primary runs and 3 native physical-interface PCAP diagnostics; no native timeout, retry, recovery, or blackout | Native Linux removes the recurring WSL2 command-channel blackout under the tested conditions |
| `E3-STATE-CLI` | 3 earlier WSL2 primary runs, 3 earlier WSL2 PCAP diagnostics, and 3 new native primary runs | Native telemetry is equivalent or slightly better; no gap >=300 ms in any group, so no native E3 PCAP was triggered |
| `E4-VIDEO-CLI` | 3 native primary runs without PCAP | 960x720 video remained near 30 FPS and telemetry remained continuous; no E4 PCAP was triggered |
| `E5-GUI-VID-IDLE` | Pending manual GUI execution | Execute manually on native Linux; do not introduce a WSL2 comparison |

E2 and E3 together provide the evidence for continuing subsequent experiments only on native Linux. For this campaign, the optional PCAP rule is anomaly-driven after E2: capture an additional diagnostic run only if a CSV reports a new transport gap, stale interval, timeout, or recovery. The already-recorded WSL2 E3 PCAP runs remain valid historical evidence and are not repeated merely for symmetry.

The three native E4 runs used the same charged battery, with recorded run conditions of 90->85%, 77->73%, and 82->80%. The drone remained stationary with motors off. All three runs are retained because video and telemetry remained continuous and decode-error counters did not grow after stream acquisition.

## Common Preparation

1. Build and test the current source:

```bash
cmake -S tello_core -B build/tello_core_standalone
cmake --build build/tello_core_standalone
cmake -E chdir build/tello_core_standalone \
  ctest -L offline --output-on-failure
```

2. Create a separate output directory for the repeated experiment set:

```bash
mkdir -p results/final_repeated/pcap
```

3. Record the following once for the experiment session:

   - date and location;
   - drone model and firmware, if available;
   - computer, operating system, and Wi-Fi interface;
   - approximate drone-to-computer distance;
   - current Git commit;
   - battery percentage before each run;
   - whether the drone was allowed to cool before the run.

4. Keep the drone stationary on the same flat surface for all non-flight baselines.
5. Keep the camera pointed at the same well-lit scene for video comparisons.
6. Use a different file suffix for every repetition: `R01`, `R02`, and `R03`.
7. Do not overwrite a failed run. Preserve it and repeat with the next run number, documenting why it was excluded or retained.
8. Allow the drone to cool and replace or recharge the battery when temperature or battery level would make runs incomparable.

## Packet-Capture Run Rule

Packet capture is intended to answer whether a blackout is visible on the host network interface or occurs only inside the application. Because `tcpdump` adds processing and storage work, do not enable it silently during one of the three primary repetitions.

For an experiment marked **Yes** in the Optional PCAP column:

1. complete `R01`, `R02`, and `R03` without packet capture;
2. repeat the same procedure as a diagnostic run named `D01-PCAP`;
3. start `tcpdump` in a separate terminal before starting the application;
4. stop it with `Ctrl+C` immediately after the application run;
5. preserve the matching CSV and PCAP with the same experiment/run identifier;
6. if the expected blackout does not occur, repeat as `D02-PCAP` rather than overwriting `D01-PCAP`;
7. if capture itself appears to change behavior, perform an adjacent `D01-NOCAP` control run under the same conditions.

Use `-i any` because the active Linux interface name can differ across native Linux, WSL, virtualized, and USB Wi-Fi configurations. The capture filter retains all Tello UDP channels, while `-nn` avoids name-resolution work and `-s 0` stores complete packets.

## Statistical Reporting Rule

For `E2` through `E7`, calculate a summary independently for each run. Then report the mean and sample standard deviation of those per-run summaries.

Do not combine every packet from all runs into one large sample and treat the packets as independent repetitions. The independent experimental unit is the run.

Recommended across-run values are:

1. command median and p95 latency;
2. telemetry median/p95 age and maximum interarrival;
3. video median/p95 age and median decode FPS;
4. GUI timer p95 and maximum delay;
5. RC median/p95/maximum send gap;
6. RC response-latency median by axis;
7. recovery success rate and time to restore service.

## E1-SMOKE-CMD - Session Connectivity Gate

### Objective

Confirm that Wi-Fi, the command socket, SDK mode, and a basic query work before a longer run. E1 is a validity gate and is not included in statistical comparisons.

### Procedure

1. Power on the drone and connect the computer to the Tello Wi-Fi network.
2. Wait 10-15 seconds after power-on.
3. Run:

```bash
./build/tello_core_standalone/tello_cli --once \
  --metrics-csv results/final_repeated/E1-SMOKE-CMD.csv \
  --test-id E1-SMOKE-CMD \
  --scenario command-smoke \
  --notes "session connectivity gate"
```

### Acceptance

1. SDK entry succeeds.
2. `battery?` returns a valid value.
3. The CSV contains one data row with `last_command_result=OK`.

If E1 fails, do not start the remaining experiments until connectivity is restored.

## E2-CMD-BASE - Command Baseline

### Objective

Measure normal query latency, timeout behavior, and transient command recovery without telemetry display, video decoding, GUI work, or flight.

### Procedure

Perform three independent 120-second runs. Replace `R01` with `R02` and `R03` for the later runs.

```bash
./build/tello_core_standalone/tello_cli --watch \
  --interval-ms 1000 \
  --duration-s 120 \
  --metrics-csv results/final_repeated/E2-CMD-BASE-R01.csv \
  --test-id E2-CMD-BASE-R01 \
  --scenario command-baseline \
  --notes "stationary drone; independent repetition 1 of 3"
```

Keep the drone stationary and do not start another Tello application during the run.

### Commands

1. `command`
2. repeated `battery?`
3. internal recovery commands only if a transient outage occurs

### Primary Metrics

1. per-command latency, median, p95, and maximum;
2. final command failures;
3. internal timeout/retry counts;
4. `TRANSIENT_LOSS_RECOVERED` count;
5. UDP receive-wait and recovery timing.

### Optional Packet-Capture Diagnosis

Repeat the procedure with CSV/test ID `E2-CMD-BASE-D01-PCAP`. Start this capture first:

```bash
sudo tcpdump -i any -nn -s 0 \
  'host 192.168.10.1 and udp' \
  -w results/final_repeated/pcap/E2-CMD-BASE-D01-PCAP.pcap
```

Use the matched CSV and PCAP to determine whether latency spikes and retries correspond to missing command requests or responses on UDP port 8889.

## E3-STATE-CLI - Telemetry-Only Baseline

### Objective

Measure telemetry freshness and continuity without FFmpeg decoding or Qt rendering. This is the reference used to decide whether later gaps originate below the GUI/video layer.

### Procedure

Perform three independent 150-second runs:

```bash
./build/tello_core_standalone/tello_cli --state-watch \
  --interval-ms 1000 \
  --duration-s 150 \
  --metrics-csv results/final_repeated/E3-STATE-CLI-R01.csv \
  --test-id E3-STATE-CLI-R01 \
  --scenario telemetry-cli-baseline \
  --notes "stationary telemetry-only repetition 1 of 3"
```

### Primary Metrics

1. telemetry receive-rate EMA;
2. median and p95 packet age;
3. median, p95, and maximum packet interarrival;
4. gap events above 300, 500, and 1000 ms;
5. percentage of rows classified `OK`, `DEGRADED`, and `STALE`.

### Optional Packet-Capture Diagnosis

Repeat the procedure with CSV/test ID `E3-STATE-CLI-D01-PCAP`. Start this capture first:

```bash
sudo tcpdump -i any -nn -s 0 \
  'host 192.168.10.1 and udp' \
  -w results/final_repeated/pcap/E3-STATE-CLI-D01-PCAP.pcap
```

Compare CSV telemetry gaps with packets received on UDP port 8890. Packets present in the PCAP but absent from application metrics point toward a receiver, parsing, scheduling, or synchronization issue above the network interface.

## E4-VIDEO-CLI - FFmpeg Video Baseline

### Objective

Measure FFmpeg transport/decode behavior and telemetry while excluding Qt display and plotting work. Comparing E4 with E3 isolates the additional video workload.

### Procedure

Perform three independent 150-second runs:

```bash
./build/tello_core_standalone/tello_cli --video-watch \
  --interval-ms 1000 \
  --duration-s 150 \
  --metrics-csv results/final_repeated/E4-VIDEO-CLI-R01.csv \
  --test-id E4-VIDEO-CLI-R01 \
  --scenario video-cli-baseline \
  --notes "stationary FFmpeg repetition 1 of 3"
```

Use the same scene and approximate distance in all repetitions.

### Primary Metrics

1. decoder FPS median and variation;
2. video frame age and continuity labels;
3. decoded frame count and decode errors;
4. frame dimensions and keyframe count;
5. telemetry age/interarrival under video load;
6. recovery events, if any.

### Optional Packet-Capture Diagnosis

Repeat the procedure with CSV/test ID `E4-VIDEO-CLI-D01-PCAP`. Start this capture first:

```bash
sudo tcpdump -i any -nn -s 0 \
  'host 192.168.10.1 and udp' \
  -w results/final_repeated/pcap/E4-VIDEO-CLI-D01-PCAP.pcap
```

Correlate FFmpeg frame-age/decode gaps with UDP port 11111 and telemetry port 8890. Incoming video packets during a decoded-frame freeze indicate a decode/application problem; a matching absence of video packets indicates a transport or drone-side interruption.

## E5-GUI-VID-IDLE - Stationary Control Panel Baseline

### Objective

Measure the complete standalone Control Panel while the drone is stationary. Comparing E5 with E4 shows the incremental effect of Qt display, plotting, and recording.

### Procedure

Perform three independent 150-second runs.

1. Start the panel:

```bash
./build/tello_core_standalone/tello_control_panel
```

2. Connect with `Connect + SDK` and confirm that FFmpeg video is active.
3. For the first repetition, set:

```text
GUI metrics csv: results/final_repeated/E5-GUI-VID-IDLE-R01.csv
state csv:       results/final_repeated/E5-GUI-VID-IDLE-R01-state.csv
```

4. Start recording and leave the drone untouched for 150 seconds.
5. Stop recording and export both CSV files.
6. Disconnect and repeat with `R02` and `R03`.

### Primary Metrics

1. telemetry and video age/quality;
2. decoder FPS and errors;
3. GUI vision/state timer p95 and maximum delay;
4. refresh, frame conversion/scaling, and plot paint time;
5. command mutex wait;
6. state-recording inter-sample gaps.

### Optional Packet-Capture Diagnosis

Repeat the GUI procedure with output names `E5-GUI-VID-IDLE-D01-PCAP.csv` and `E5-GUI-VID-IDLE-D01-PCAP-state.csv`. Start this capture before opening the panel:

```bash
sudo tcpdump -i any -nn -s 0 \
  'host 192.168.10.1 and udp' \
  -w results/final_repeated/pcap/E5-GUI-VID-IDLE-D01-PCAP.pcap
```

Use this run to distinguish a Qt display/event-loop freeze from a network blackout. If telemetry/video packets continue in the PCAP while GUI metrics pause, the interruption is above the host network interface.

## E6-RC-IDLE - Ground RC Worker And Profile Safety

### Objective

Validate profile persistence, key mapping, continuous RC cadence, neutral return, and stale-input safety without flight. Comparing E6 with E7 separates RC-worker behavior from airborne/network/video effects.

### Procedure

Perform three 120-second ground runs.

1. Confirm that the `KeyboardTest` profile persists after restarting the panel.
2. Use the same mappings and aggression `25` in every run.
3. Connect SDK mode but do not take off.
4. Set the `R01` GUI/state paths:

```text
GUI metrics csv: results/final_repeated/E6-RC-IDLE-R01.csv
state csv:       results/final_repeated/E6-RC-IDLE-R01-state.csv
```

5. Start recording and enable keyboard control.
6. Leave all keys released for 30 seconds so the worker sends neutral RC.
7. Press each mapped direction for approximately 500 ms, release it, and wait 3 seconds before the next direction.
8. Leave all keys released for another 30 seconds.
9. Disable keyboard control, stop recording, and export both files.
10. Repeat with `R02` and `R03`.

### Acceptance And Metrics

1. profile name, aggression, and mappings persist;
2. RC channels correspond to each key;
3. RC returns to `0 0 0 0` after release;
4. RC median, p95, and maximum packet gaps remain close to the configured cadence;
5. no unexplained nonzero RC remains active;
6. aggregate link quality and `safe_for_nonzero_rc` follow the RC cadence state.

### Optional Packet-Capture Diagnosis

Repeat the GUI procedure with output names `E6-RC-IDLE-D01-PCAP.csv` and `E6-RC-IDLE-D01-PCAP-state.csv`. Start this capture before opening the panel:

```bash
sudo tcpdump -i any -nn -s 0 \
  'host 192.168.10.1 and udp' \
  -w results/final_repeated/pcap/E6-RC-IDLE-D01-PCAP.pcap
```

Inspect outgoing UDP port 8889 traffic to verify the RC cadence seen by the network interface. An RC gap already present in the PCAP points toward the host process, scheduling, or socket-send path; regular outgoing RC in the PCAP with stale incoming channels points toward the Wi-Fi link or drone.

## E7-KBD-RESPONSE - Flight Response And Combined Load

### Objective

Measure command-to-motion response while RC, telemetry, FFmpeg video, GUI rendering, and recording operate simultaneously. E7 is the main full-system experiment. An RC aggression of `35` is used to make the commanded steps more distinguishable from normal hover variation, supporting command-aligned detection of response onset in the recorded state channels.

### Safety

1. Use a clear indoor area and a charged battery.
2. Keep aggression fixed at `35` for all repetitions.
3. Use short pulses and wait for hover stabilization between pulses.
4. Keep `land` and `emergency` immediately accessible.
5. Stop the experiment if link quality becomes `STALE` or `BLACKOUT`.

### Procedure

Perform three independent flight runs, allowing the drone to cool between runs.

1. Start the panel and set the `R01` paths:

```text
GUI metrics csv: results/final_repeated/E7-KBD-RESPONSE-R01.csv
state csv:       results/final_repeated/E7-KBD-RESPONSE-R01-state.csv
```

2. Connect SDK mode, confirm FFmpeg video and telemetry, and start recording.
3. Select the tested keyboard profile, set aggression to exactly `35`, and confirm the value before takeoff.
4. Take off and hover for 10 seconds.
5. Enable keyboard control.
6. Execute two pulses per direction. Begin each pulse only after the drone has returned to a visually stable hover, hold the key for approximately 500 ms, release it completely, and wait at least 3 seconds before the next pulse:

   - up, then down;
   - yaw left, then yaw right;
   - forward, then backward if space permits;
   - left, then right if space permits.

7. Leave all keys released for 10 seconds.
8. Disable keyboard control and land.
9. Wait until telemetry confirms touchdown.
10. Stop recording and export both CSV files.
11. Repeat with `R02` and `R03`.

### Primary Metrics

1. RC command cadence and neutral-return time;
2. command-to-response onset latency for each detectable pulse, aligned from the first nonzero RC sample to the first sustained state change above the pre-pulse hover variation;
3. vertical response from `h`/`tof` and `vgz`;
4. yaw response from `yaw`;
5. lateral/forward response using attitude and available velocity fields as proxies when direct position is unavailable;
6. telemetry and video freshness during flight;
7. decoder FPS and errors;
8. GUI timer delay and command mutex wait;
9. takeoff/land SDK result and telemetry confirmation;
10. link-quality transitions and RC safety override.

### Optional Packet-Capture Diagnosis

Perform a separate flight run using output names `E7-KBD-RESPONSE-D01-PCAP.csv` and `E7-KBD-RESPONSE-D01-PCAP-state.csv`. Start this capture before opening the panel:

```bash
sudo tcpdump -i any -nn -s 0 \
  'host 192.168.10.1 and udp' \
  -w results/final_repeated/pcap/E7-KBD-RESPONSE-D01-PCAP.pcap
```

This is the most useful capture for blackout diagnosis because command/RC, telemetry, and video are active together. A simultaneous gap on ports 8889, 8890, and 11111 is evidence of a link-wide interruption rather than an isolated decoder, telemetry parser, or GUI problem. Treat this as an additional diagnostic flight, not as a replacement for the three primary runs.

## E8-PWR-CYCLE - Full Session Recovery

### Objective

Measure whether the same running process can recover SDK mode, telemetry, and FFmpeg video after the drone is powered off and restarted.

### Procedure

Perform three independent recovery events. Use `R01`, `R02`, and `R03` filenames.

```bash
./build/tello_core_standalone/tello_cli --video-watch \
  --interval-ms 1000 \
  --duration-s 180 \
  --metrics-csv results/final_repeated/E8-PWR-CYCLE-R01.csv \
  --test-id E8-PWR-CYCLE-R01 \
  --scenario power-cycle \
  --notes "power-cycle recovery repetition 1 of 3"
```

1. Wait 30 seconds after video becomes healthy.
2. Power off the drone for 5-10 seconds.
3. Power it on and reconnect the computer to Tello Wi-Fi when necessary.
4. Do not restart the CLI.
5. Let the run finish after video and telemetry recover or the timeout expires.

### Primary Metrics

1. recovery success rate out of three;
2. time from outage to command-channel restoration;
3. time from outage to fresh telemetry;
4. time from outage to decoded video;
5. recovery stage/result and hard-recovery flag;
6. final connection and quality state.

### Optional Packet-Capture Diagnosis

Repeat the procedure with CSV/test ID `E8-PWR-CYCLE-D01-PCAP`. Start this capture first and leave it running across the power cycle and Wi-Fi reconnection:

```bash
sudo tcpdump -i any -nn -s 0 \
  'host 192.168.10.1 and udp' \
  -w results/final_repeated/pcap/E8-PWR-CYCLE-D01-PCAP.pcap
```

The expected outage itself is not a defect. Use the capture to verify the sequence after restart: renewed command traffic on port 8889, telemetry on port 8890, and video on port 11111. Correlate these events with the core recovery stages in the CSV.

## E9-WIFI-LOSS - Command Reconnect

### Objective

Measure command-channel recovery after the host temporarily leaves the Tello Wi-Fi network without intentionally power-cycling the drone.

### Procedure

Perform three independent 120-second runs:

```bash
./build/tello_core_standalone/tello_cli --watch \
  --interval-ms 1000 \
  --duration-s 120 \
  --metrics-csv results/final_repeated/E9-WIFI-LOSS-R01.csv \
  --test-id E9-WIFI-LOSS-R01 \
  --scenario wifi-reconnect \
  --notes "host Wi-Fi reconnect repetition 1 of 3"
```

1. After 30 seconds, disconnect the host from Tello Wi-Fi for 5-10 seconds.
2. Reconnect to Tello Wi-Fi without restarting the CLI.
3. Let the run finish.

### Primary Metrics

1. reconnect success rate;
2. failed commands during the outage;
3. time to the first successful command after reconnect;
4. connection-state sequence;
5. final command result and outage count.

### Optional Packet-Capture Diagnosis

Repeat the procedure with CSV/test ID `E9-WIFI-LOSS-D01-PCAP`. Start this capture first and leave it running while the host disconnects and reconnects:

```bash
sudo tcpdump -i any -nn -s 0 \
  'host 192.168.10.1 and udp' \
  -w results/final_repeated/pcap/E9-WIFI-LOSS-D01-PCAP.pcap
```

Use the capture to establish when command requests leave the host and when responses return after reconnection. Because `-i any` observes all Linux interfaces, the capture remains useful if the active Wi-Fi interface is recreated or renamed.

## E10-ROS-END-TO-END - ROS2 Acceptance

### Objective

Confirm that the released core artifact and ROS2 workspace form one working runtime: the driver owns UDP communication, publishes telemetry/video/quality, accepts commands through services/topics, and supplies the ROS-mode Control Panel.

### Procedure

1. Build and test the ROS workspace:

```bash
cd "/home/gabriel_fernandes/CS 500 - ROS"
source /opt/ros/foxy/setup.bash
colcon build
colcon test
colcon test-result --verbose
source install/setup.bash
```

2. Launch the driver and ROS-mode panel:

```bash
ros2 launch tello_bringup control_panel.launch.py
```

3. Connect through the panel and confirm that only the driver owns the drone connection.
4. In separate terminals, inspect:

```bash
ros2 topic echo /tello/connection_state
ros2 topic echo /tello/link_quality
ros2 topic hz /tello/state
ros2 topic hz /tello/video/image_raw
```

5. Verify the service surface:

```bash
ros2 service list | grep '^/tello/'
```

6. Invoke safe stationary services such as stream off/on and confirm that the GUI follows the ROS-published state.
7. If flight validation is approved and the area is safe, invoke takeoff and land through `/tello/takeoff` and `/tello/land`; otherwise use the existing recorded demonstration as flight-service evidence.

### Acceptance

1. all ROS packages build and tests pass;
2. telemetry, link quality, diagnostics, and video topics publish;
3. the ROS-mode panel receives state and video through ROS;
4. services execute through the driver;
5. `/tello/cmd_vel` is ignored until autonomy is explicitly enabled;
6. manual GUI control disables autonomy until it is explicitly re-enabled.

### Optional Packet-Capture Diagnosis

Packet capture is not required for ROS acceptance. If ROS topics become stale or a command service times out, repeat the failing operation with a diagnostic identifier and run:

```bash
sudo tcpdump -i any -nn -s 0 \
  'host 192.168.10.1 and udp' \
  -w results/final_repeated/pcap/E10-ROS-END-TO-END-D01-PCAP.pcap
```

This verifies traffic owned by the ROS driver at the network interface; it does not imply that other ROS nodes should open their own Tello UDP sockets.

## Packet-Capture Interpretation

The relevant Tello UDP channels are:

| Channel | UDP port | Direction |
|---|---:|---|
| SDK commands, responses, and RC | 8889 | bidirectional |
| telemetry state packets | 8890 | drone to host |
| H264 video stream | 11111 | drone to host |

Read a captured channel without modifying the PCAP:

```bash
tcpdump -nn -tttt -r results/final_repeated/pcap/E7-KBD-RESPONSE-D01-PCAP.pcap 'udp port 8889'
tcpdump -nn -tttt -r results/final_repeated/pcap/E7-KBD-RESPONSE-D01-PCAP.pcap 'udp port 8890'
tcpdump -nn -tttt -r results/final_repeated/pcap/E7-KBD-RESPONSE-D01-PCAP.pcap 'udp port 11111'
```

Interpret matched CSV and PCAP evidence as follows:

| Observation | Most likely layer to investigate next |
|---|---|
| CSV reports stale data, but matching packets remain present in the PCAP | application receive, parsing, decode, synchronization, or scheduling |
| outgoing RC disappears from the PCAP while the application should be sending it | RC worker, process scheduling, socket-send path, or host overload |
| outgoing RC remains regular, but telemetry and video disappear | Wi-Fi downlink, drone transmission, radio environment, or driver receive path |
| command requests leave on port 8889, but responses do not return | drone command processing or command-channel transport |
| all three channels show a simultaneous gap | link-wide Wi-Fi, interface/driver, radio, or drone-side blackout |
| packets resume in the PCAP before application quality recovers | application recovery policy or stale-state handling |

Packet capture proves what reached or left the host capture interface. By itself, it cannot distinguish radio interference from the drone choosing not to transmit. If the PCAP confirms a link-wide blackout, collect interface counters and operating-system Wi-Fi events during a later diagnostic run to separate interface/driver resets from radio or drone-side silence.

Always record whether capture was active. Compare diagnostic PCAP runs with the uncaptured primary repetitions before attributing a timing change to the drone or API.

## Completion Checklist

The final dataset is complete when:

1. E1 passes for every test session;
2. E2-E7 each have three valid independent runs;
3. E8 and E9 each contain three recovery attempts with documented outcomes;
4. E10 demonstrates the ROS2 build, interfaces, driver ownership, and live message flow;
5. every filename has a unique run suffix and is never overwritten;
6. excluded runs and their reasons are preserved in the experiment notes;
7. every diagnostic PCAP is non-empty and has a matching CSV identifier;
8. captured diagnostic runs are labelled separately from primary statistical repetitions;
9. the report presents across-run variation, not only one selected trace.
