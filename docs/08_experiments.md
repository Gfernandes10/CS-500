# Phase E - Experiment Protocol

## Purpose

Define repeatable real-drone experiments for evaluating the DJI Tello C++ system with the central `MetricsCollector` CSV schema.

The experiments measure:
1. command-response latency and recovery behavior,
2. telemetry receive rate, packet age, and telemetry quality state,
3. video packet continuity, video quality state, NAL assembly, decode FPS, and decode errors,
4. GUI/control-panel behavior during command, telemetry, and video workflows.

## General Safety Rules

1. Run flight-related tests only in an open indoor area with enough clearance.
2. Keep the drone visible and reachable.
3. Keep one hand ready to send `land` or `emergency` if needed.
4. Avoid takeoff tests while debugging CSV export or basic connectivity.
5. For baseline metrics, keep the drone powered on and stationary on a flat surface.
6. Use `--duration-s` for CLI tests whenever possible to avoid unattended infinite runs.

## Setup Checklist

1. Build the project:
```bash
cd tello_core
cmake --build build
```

2. Create a results folder:
```bash
mkdir -p ../results
```

3. Power on the DJI Tello.
4. Connect the computer to the Tello Wi-Fi network.
5. Wait 10-15 seconds after the drone finishes powering on.
6. Run one quick command check:
```bash
./build/tello_cli --once --metrics-csv ../results/E-SMOKE-CMD-001.csv --test-id E-SMOKE-CMD-001 --scenario command-smoke --notes "pre-experiment connectivity check"
```

Expected result:
1. `initialize: OK`
2. `battery?: OK`
3. CSV file contains one header row and one data row.

## Experiment Matrix

| ID | Scenario | Tool | Duration | Priority | Main Metrics |
|---|---|---|---:|---|---|
| E-CMD-BASE | command-baseline | `tello_cli --watch` | 60s | Keep | command latency, success/failure count |
| E-TEL-BASE | telemetry-baseline | `tello_cli --state-watch` | 60s | Keep | telemetry Hz, packet age, telemetry quality |
| E-VID-BASE | video-baseline | `tello_cli --video-watch` | 60s | Keep | video packets, video quality, NAL units, decode FPS/errors |
| E-GUI-BASE | gui-baseline | `tello_control_panel` | manual | Keep | GUI command/video rows |
| E-GUI-TEL-IDLE | gui-telemetry-idle | `tello_control_panel` | 2-3 min | Optional | telemetry quality without video |
| E-GUI-VID-IDLE | gui-video-idle | `tello_control_panel` | 2-3 min | Keep | telemetry/video quality with video idle |
| E-GUI-MOVE | gui-motion-diagnostic | `tello_control_panel` | 2-3 min | Keep | telemetry/video quality while moving drone |
| E-GUI-PLOT | gui-plot-metric | `tello_control_panel` | manual | Optional | plot metric change vs telemetry gaps |
| E-STATE-CLI | telemetry-cli-diagnostic | `tello_cli --state-watch` | 180s | Keep | telemetry quality without GUI |
| E-VIDEO-CLI | video-cli-diagnostic | `tello_cli --video-watch` | 180s | Keep | video quality without GUI |
| E-LINK-DIST | link-distance-diagnostic | `tello_control_panel` or CLI | 2-3 min | New | quality near/far and orientation sensitivity |
| E-RC-QUALITY | rc-stream-quality | `tello_control_panel` | manual | New/Safety-gated | command/telemetry quality while RC stream is active |
| E-KBD-NEUTRAL | keyboard-neutral-safety | `tello_control_panel` | manual | New/Safety-gated | keyboard release sends neutral RC |
| E-KBD-RESPONSE | keyboard-response-latency | `tello_control_panel` | manual | New/Safety-gated | key press to telemetry motion response |
| E-KBD-PROFILE | keyboard-profile-validation | `tello_control_panel` | manual | New | profile save/load and key mapping correctness |
| E-WIFI-LOSS | wifi-reconnect | `tello_cli --watch` | manual | Keep | command recovery and outage failures |
| E-VID-RESET | stream-reset | `tello_cli --video-watch` | manual | Conditional | video stream recovery without power-cycle |
| E-PWR-CYCLE | power-cycle | `tello_cli --video-watch` | manual | Keep | hard recovery after drone restart |

## Recommended Next Run Set

Run this smaller set first when validating network/environment/driver effects:

1. `E-STATE-CLI` for 180 seconds with the drone stationary.
2. `E-VIDEO-CLI` for 180 seconds with the drone stationary and video enabled.
3. `E-GUI-VID-IDLE` for 180 seconds with the control panel open, video enabled, and no movement.
4. `E-GUI-MOVE` for 180 seconds, with movement starting near the 60-second mark.
5. `E-LINK-DIST` if quality drops appear linked to position, distance, or orientation.
6. `E-KBD-PROFILE` without takeoff to validate profile persistence and mapping.
7. `E-KBD-NEUTRAL` only after the no-flight diagnostics are stable and the area is safe.
8. `E-KBD-RESPONSE` only in a safe flight area, after neutral behavior has been confirmed.
9. `E-RC-QUALITY` if you want to compare keyboard RC against slider-based RC streaming.

`E-GUI-TEL-IDLE` is useful only when you need to separate state-only GUI behavior from video-enabled GUI behavior. `E-GUI-PLOT` is useful only if freezes appear immediately after changing the plot metric. `E-VID-RESET` is useful only when you can interrupt the video stream without restarting the drone; a drone restart belongs to `E-PWR-CYCLE`.

## Link Quality Fields

The central metrics CSV includes freshness-oriented quality fields:

1. `telemetry_quality`
2. `telemetry_quality_score`
3. `video_quality`
4. `video_quality_score`

Quality labels:

1. `NO_DATA`: no packet has been received yet.
2. `OK`: latest packet age is at most 200 ms and the latest interarrival gap is at most 300 ms.
3. `DEGRADED`: latest packet age is at most 500 ms and the latest interarrival gap is at most 500 ms.
4. `STALE`: data is older than the degraded threshold.

Use `OK` data normally. Treat `DEGRADED` data as usable for logging and cautious control only. Treat `STALE` or `NO_DATA` data as unsafe for closed-loop control; a controller should hold, reduce authority, send neutral RC, or enter a recovery state depending on the experiment.

## E-CMD-BASE - Command Baseline

### Purpose

Measure command-response latency while the drone is powered on, idle, and connected over stable Wi-Fi.

### Real Drone Steps

1. Place Tello on a flat surface.
2. Power on Tello and connect the computer to Tello Wi-Fi.
3. Wait 10-15 seconds after power-on.
4. Run the command below.
5. Do not move the drone or switch networks during the run.

### Command to Run

```bash
cd tello_core
./build/tello_cli --watch \
  --interval-ms 1000 \
  --duration-s 60 \
  --metrics-csv ../results/E-CMD-BASE-001.csv \
  --test-id E-CMD-BASE-001 \
  --scenario command-baseline \
  --notes "Tello idle on desk; stable Wi-Fi"
```

### Commands Sent to Drone

1. `command`
2. repeated `battery?`

### Success Criteria

1. Most rows report `last_command_result=OK`.
2. `command_latency_ms_avg` remains stable.
3. `command_failures` remains low or zero.

## E-TEL-BASE - Telemetry Baseline

### Purpose

Measure telemetry receive rate and packet freshness while the drone is idle.

### Real Drone Steps

1. Place Tello on a flat surface.
2. Power on Tello and connect the computer to Tello Wi-Fi.
3. Wait 10-15 seconds after power-on.
4. Run the command below.
5. Watch terminal output for `rx_hz` and `age_ms`.

### Command to Run

```bash
cd tello_core
./build/tello_cli --state-watch \
  --interval-ms 1000 \
  --duration-s 60 \
  --metrics-csv ../results/E-TEL-BASE-001.csv \
  --test-id E-TEL-BASE-001 \
  --scenario telemetry-baseline \
  --notes "Tello idle on desk; telemetry baseline"
```

### Commands Sent to Drone

1. `command`

Telemetry packets are then received passively on UDP port `8890`.

### Success Criteria

1. `telemetry_packets_total` increases over time.
2. `telemetry_hz_ema` is non-zero and stable.
3. `telemetry_age_ms` stays low during a healthy connection.
4. `telemetry_quality` remains mostly `OK`.

## E-VID-BASE - Video Baseline

### Purpose

Measure video packet continuity, NAL assembly, decode FPS, and decode errors under normal conditions.

### Real Drone Steps

1. Place Tello on a flat surface with the camera facing a well-lit scene.
2. Power on Tello and connect the computer to Tello Wi-Fi.
3. Wait 10-15 seconds after power-on.
4. Run the command below.
5. Do not intentionally interrupt Wi-Fi or power during the run.

### Command to Run

```bash
cd tello_core
./build/tello_cli --video-watch \
  --interval-ms 1000 \
  --duration-s 60 \
  --metrics-csv ../results/E-VID-BASE-001.csv \
  --test-id E-VID-BASE-001 \
  --scenario video-baseline \
  --notes "Tello idle on desk; video baseline"
```

### Commands Sent to Drone

1. `command`
2. `streamon`
3. `streamoff` at shutdown

### Success Criteria

1. `video_packets_total` increases continuously.
2. `nal_units` increases continuously.
3. `decoder_frames_decoded` increases when FFmpeg is enabled.
4. `decoder_fps_ema` is stable after startup.
5. `decoder_errors` does not grow rapidly under stable Wi-Fi.
6. `video_quality` remains mostly `OK` after startup.

## E-GUI-BASE - Control Panel Baseline

### Purpose

Validate that the Qt control panel can collect command and vision metrics through the central CSV schema.

### Real Drone Steps

1. Power on Tello and connect the computer to Tello Wi-Fi.
2. Start the control panel:
```bash
cd tello_core
./build/tello_control_panel
```
3. Click `Connect + SDK`.
4. Enable CSV export and choose a path such as:
```text
../results/E-GUI-BASE-001.csv
```
5. Send read commands: `battery?`, `wifi?`, `speed?`.
6. Click `Start View`.
7. Let the video run for at least 30 seconds.
8. Use `Pause`, `Overlay`, and `Snapshot` once each.
9. Click `Stop View`.
10. Disable CSV export or close the panel.

### Commands Sent to Drone

From the GUI:
1. `command`
2. `battery?`
3. `wifi?`
4. `speed?`
5. `streamon`
6. `streamoff`

Optional if safe and intended:
1. `takeoff`
2. `land`

### Success Criteria

1. CSV contains command rows and periodic vision rows.
2. Command rows include command latency and response fields.
3. Vision rows include video receiver, NAL, decoder, frame, and display-state fields.
4. GUI remains responsive during video display.

## GUI Telemetry/Video Stall Diagnostics

### Purpose

Separate four possible causes of visible freezes in the control panel:

1. telemetry UDP packet gaps,
2. general Wi-Fi/video packet gaps,
3. Qt GUI/rendering load,
4. plot metric changes or drone movement/orientation.

Use these experiments when the video view or telemetry plot appears to freeze even though the control panel remains open.

### Diagnostic CSV Fields

Check these fields in `control_panel_commands.csv`:

1. `telemetry_interarrival_ms`
2. `telemetry_max_interarrival_ms`
3. `telemetry_last_gap_ms`
4. `telemetry_last_gap_sequence`
5. `telemetry_gap_events_300ms`
6. `telemetry_gap_events_500ms`
7. `telemetry_gap_events_1000ms`
8. `telemetry_quality`
9. `telemetry_quality_score`
10. `video_interarrival_ms`
11. `video_max_interarrival_ms`
12. `video_last_gap_ms`
13. `video_gap_events_300ms`
14. `video_gap_events_500ms`
15. `video_gap_events_1000ms`
16. `video_quality`
17. `video_quality_score`
18. `plot_metric`
19. `state_sequence_delta`
20. `state_receiver_running`
21. `gui_vision_tick_delay_ms`
22. `gui_state_tick_delay_ms`
23. `vision_refresh_duration_ms`
24. `state_refresh_duration_ms`
25. `plot_paint_ms`
26. `frame_convert_ms`
27. `frame_scale_ms`

### Interpretation Rules

1. If `telemetry_gap_events_500ms` increases but `video_gap_events_500ms` does not, the issue is likely specific to telemetry/state UDP on port `8890`.
2. If both telemetry and video gap counters increase together, the issue is likely general Wi-Fi/link quality.
3. If gap counters do not increase but `gui_*_tick_delay_ms`, `plot_paint_ms`, `frame_convert_ms`, or `frame_scale_ms` increase, the issue is likely GUI/rendering load.
4. If gaps increase only while the drone is being moved, suspect orientation, antenna placement, link quality, or drone-side state/video behavior during motion.
5. If `plot_metric_changed:<metric>` appears immediately before gaps, repeat the same test without changing the metric to confirm whether the relationship is causal or coincidental.
6. If `telemetry_quality` or `video_quality` changes from `OK` to `DEGRADED` or `STALE`, mark the timestamp and compare it against movement, distance, GUI timing, and recovery events.

## E-GUI-TEL-IDLE - Control Panel Telemetry Idle Diagnostic

### Purpose

Measure telemetry packet continuity in the control panel without video load and without drone movement.

### Real Drone Steps

1. Power on Tello and connect the computer to Tello Wi-Fi.
2. Start the control panel:
```bash
cd tello_core
./build/tello_control_panel
```
3. Click `Connect + SDK`.
4. Enable CSV export and choose:
```text
../results/E-GUI-TEL-IDLE-001.csv
```
5. Start state recording.
6. Do not click `Start View`.
7. Keep the drone stationary for 2-3 minutes.
8. Stop state recording.
9. Export state recording as:
```text
../results/E-GUI-TEL-IDLE-001-state.csv
```
10. Disable CSV export or close the panel.

### Commands Sent to Drone

1. `command`
2. SDK keepalive query, if enabled by the control panel

Telemetry is received passively on UDP port `8890`.

### Success Criteria

1. `telemetry_packets_total` increases steadily.
2. `telemetry_gap_events_500ms` stays at `0` or very low.
3. `telemetry_quality` remains mostly `OK`.
4. `state_receiver_running=1`.
5. GUI timing fields remain low.

## E-GUI-VID-IDLE - Control Panel Video Idle Diagnostic

### Purpose

Measure whether enabling video affects telemetry continuity while the drone is stationary.

### Real Drone Steps

1. Power on Tello and connect the computer to Tello Wi-Fi.
2. Start the control panel.
3. Click `Connect + SDK`.
4. Enable CSV export and choose:
```text
../results/E-GUI-VID-IDLE-001.csv
```
5. Start state recording.
6. Click `Start View`.
7. Keep the drone stationary for 2-3 minutes.
8. Stop state recording.
9. Export state recording as:
```text
../results/E-GUI-VID-IDLE-001-state.csv
```
10. Click `Stop View`.
11. Disable CSV export or close the panel.

### Commands Sent to Drone

1. `command`
2. `streamon`
3. SDK keepalive query, if enabled
4. `streamoff` when stopping vision

### Success Criteria

1. `video_packets_total` increases steadily.
2. `video_gap_events_500ms` stays at `0` or very low.
3. `video_quality` remains mostly `OK` after startup.
4. Compare `telemetry_gap_events_500ms` and `telemetry_quality` against `E-GUI-TEL-IDLE`.
5. If telemetry gaps increase only when video is enabled, investigate bandwidth/CPU/scheduler interaction.

## E-GUI-MOVE - Control Panel Motion Diagnostic

### Purpose

Measure whether physically moving the drone changes telemetry or video continuity.

### Real Drone Steps

1. Power on Tello and connect the computer to Tello Wi-Fi.
2. Start the control panel.
3. Click `Connect + SDK`.
4. Enable CSV export and choose:
```text
../results/E-GUI-MOVE-001.csv
```
5. Start state recording.
6. Click `Start View`.
7. For 30 seconds, keep the drone still.
8. For 2 minutes, move the powered-on drone by hand:
   - up/down,
   - left/right,
   - gently rotate yaw direction.
9. Keep the drone within safe reach and do not take off for this diagnostic.
10. Stop state recording.
11. Export state recording as:
```text
../results/E-GUI-MOVE-001-state.csv
```
12. Click `Stop View`.
13. Disable CSV export or close the panel.

### Commands Sent to Drone

1. `command`
2. `streamon`
3. SDK keepalive query, if enabled
4. `streamoff` when stopping vision

### Success Criteria

1. Gap counters stay similar to the stationary video test if movement is not a factor.
2. If `telemetry_gap_events_500ms` or `video_gap_events_500ms` increases during movement, suspect Wi-Fi orientation/link quality or drone-side behavior during motion.
3. If quality changes from `OK` to `DEGRADED` or `STALE` during movement, mark the timestamp for the notebook analysis.
4. GUI timing fields should remain low; otherwise investigate rendering load.

## E-GUI-PLOT - Control Panel Plot Metric Diagnostic

### Purpose

Check whether changing the telemetry plot metric correlates with visible freezes or packet gaps.

### Real Drone Steps

1. Power on Tello and connect the computer to Tello Wi-Fi.
2. Start the control panel.
3. Click `Connect + SDK`.
4. Enable CSV export and choose:
```text
../results/E-GUI-PLOT-001.csv
```
5. Start state recording.
6. Click `Start View`.
7. Keep the drone stationary.
8. Wait 30 seconds on the default plot metric.
9. Change plot metric every 20-30 seconds:
   - `h`
   - `tof`
   - `baro`
   - `vgz`
   - `agz`
10. Stop state recording.
11. Export state recording as:
```text
../results/E-GUI-PLOT-001-state.csv
```
12. Click `Stop View`.
13. Disable CSV export or close the panel.

### Commands Sent to Drone

1. `command`
2. `streamon`
3. SDK keepalive query, if enabled
4. `streamoff` when stopping vision

### Success Criteria

1. CSV includes `plot_metric_changed:<metric>` event rows.
2. `plot_metric` reflects the selected plot metric.
3. If visible freezes happen, check whether they align with metric-change events.
4. If `plot_paint_ms` remains low and telemetry gap counters increase, the freeze is not caused by plot rendering cost.

## E-STATE-CLI - CLI Telemetry Diagnostic

### Purpose

Measure telemetry continuity without Qt, video rendering, or control-panel plotting.

### Command to Run

```bash
cd tello_core
./build/tello_cli --state-watch \
  --interval-ms 250 \
  --duration-s 180 \
  --metrics-csv ../results/E-STATE-CLI-001.csv \
  --test-id E-STATE-CLI-001 \
  --scenario telemetry-cli-diagnostic \
  --notes "Telemetry continuity without GUI"
```

### Commands Sent to Drone

1. `command`

Telemetry packets are received passively on UDP port `8890`.

### Success Criteria

1. `telemetry_gap_events_500ms` stays at `0` or very low.
2. `telemetry_quality` remains mostly `OK`.
3. If gaps appear here and in the panel, the issue is not caused by Qt rendering.

## E-VIDEO-CLI - CLI Video Diagnostic

### Purpose

Measure video continuity without Qt rendering or telemetry plotting.

### Command to Run

```bash
cd tello_core
./build/tello_cli --video-watch \
  --interval-ms 1000 \
  --duration-s 120 \
  --metrics-csv ../results/E-VIDEO-CLI-001.csv \
  --test-id E-VIDEO-CLI-001 \
  --scenario video-cli-diagnostic \
  --notes "Video continuity without control panel"
```

### Commands Sent to Drone

1. `command`
2. `streamon`
3. recovery commands if the stream stalls
4. `streamoff` at shutdown

### Success Criteria

1. `video_gap_events_500ms` stays at `0` or very low.
2. `video_quality` remains mostly `OK`.
3. If video gaps appear here and in the panel, the issue is not caused by Qt rendering.

## E-LINK-DIST - Link Distance and Orientation Diagnostic

### Purpose

Measure whether telemetry/video quality changes with distance, orientation, or room position. This is the main experiment for confirming network/environment/driver sensitivity before building a closed-loop controller.

### Real Drone Steps

1. Power on Tello and connect the computer to Tello Wi-Fi.
2. Start either the control panel with video enabled or the CLI video-watch command below.
3. Keep the drone stationary and close to the computer for 60 seconds.
4. Move the drone farther away for 60 seconds.
5. Rotate the drone body slowly through several orientations for 60 seconds.
6. Keep the drone powered on but do not take off for this diagnostic.

### Command to Run

```bash
cd tello_core
./build/tello_cli --video-watch \
  --interval-ms 1000 \
  --duration-s 120 \
  --metrics-csv ../results/E-LINK-DIST-001.csv \
  --test-id E-LINK-DIST-001 \
  --scenario link-distance-diagnostic \
  --notes "Near/far/orientation link quality diagnostic"
```

### Commands Sent to Drone

1. `command`
2. `streamon`
3. recovery commands if the stream stalls
4. `streamoff` at shutdown

### Success Criteria

1. `telemetry_quality` and `video_quality` remain mostly `OK` in the close stationary segment.
2. Any transition to `DEGRADED` or `STALE` aligns with distance, orientation, or room position.
3. If quality changes without GUI involvement, prioritize Wi-Fi/channel/driver mitigations over GUI optimization.

## E-RC-QUALITY - RC Stream Quality Diagnostic

### Purpose

Measure whether continuous RC traffic affects command, telemetry, or video quality. This experiment matters for future control-loop integration because RC commands are the control output path.

### Safety

Run this test only when the flight area is clear and you are ready to send neutral RC, `land`, or `emergency`. Prefer very small RC values first. Do not run aggressive RC tests while investigating basic packet loss.

### Real Drone Steps

1. Power on Tello and connect the computer to Tello Wi-Fi.
2. Start the control panel.
3. Click `Connect + SDK`.
4. Enable CSV export and choose:
```text
../results/E-RC-QUALITY-001.csv
```
5. Click `Start View`.
6. Keep RC stream disabled for 30 seconds.
7. Enable RC stream with all channels at zero for 30 seconds.
8. If safe, send small RC values one axis at a time, then return to `rc 0 0 0 0`.
9. Stop the RC stream, stop video, and close the run.

### Commands Sent to Drone

1. `command`
2. `streamon`
3. repeated `rc a b c d` while RC stream is active
4. `rc 0 0 0 0` before ending the RC segment
5. `streamoff` at shutdown

### Success Criteria

1. `rc_stream_active=1` marks the RC segment.
2. `telemetry_quality` and `video_quality` remain comparable to `E-GUI-VID-IDLE`.
3. Command failures do not spike while the RC stream is active.
4. If quality degrades only during RC stream, tune RC send rate and command scheduling before integrating a controller.

## E-KBD-PROFILE - Keyboard Profile Validation

### Purpose

Validate that keyboard control profiles can be created, edited, saved, reloaded, and mapped to the expected RC axes before any flight test.

This is a no-flight configuration test. It should be run before `E-KBD-NEUTRAL` and `E-KBD-RESPONSE`.

### Real Drone Steps

1. Power on Tello and connect the computer to Tello Wi-Fi.
2. Start the control panel.
3. Click `Connect + SDK`.
4. Enable CSV export and choose:
```text
../results/E-KBD-PROFILE-001.csv
```
5. In the Config panel, create a profile named `KeyboardTest`.
6. Set RC aggression to `20`.
7. Map keys to actions, for example:
   - `W` -> up,
   - `S` -> down,
   - `Up` -> forward,
   - `Down` -> back,
   - `Left` -> yaw left,
   - `Right` -> yaw right,
   - `A` -> left,
   - `D` -> right.
8. Save the profile.
9. Close and reopen the control panel.
10. Confirm that `KeyboardTest` and its mappings were restored.
11. Do not take off during this experiment.

### Commands Sent to Drone

1. `command`
2. optional `rc 0 0 0 0` when keyboard control is disabled

### Success Criteria

1. `control_profiles.json` exists after saving the profile.
2. The profile reloads with the same name, aggression value, and key bindings.
3. No non-neutral RC command is sent unless keyboard control is explicitly enabled.

## E-KBD-NEUTRAL - Keyboard Neutral Safety

### Purpose

Verify that keyboard control always returns to neutral RC when no mapped key is pressed.

This is the most important safety experiment for the keyboard feature. The expected behavior is continuous `rc 0 0 0 0` after all mapped keys are released.

### Safety

Run this test with the drone on the ground first. If you repeat it in flight, use very small aggression values and stay ready to send `land` or `emergency`.

### Real Drone Steps

1. Power on Tello and connect the computer to Tello Wi-Fi.
2. Start the control panel.
3. Click `Connect + SDK`.
4. Enable CSV export and choose:
```text
../results/E-KBD-NEUTRAL-001.csv
```
5. Start state recording and choose:
```text
../results/E-KBD-NEUTRAL-001-state.csv
```
6. Select the keyboard profile to test.
7. Set aggression to `20` or lower.
8. Enable keyboard RC control in the Operation panel.
9. Press and hold one mapped key for 1-2 seconds.
10. Release the key and wait 3 seconds.
11. Repeat for each mapped direction.
12. Disable keyboard RC control.
13. Stop and export state recording.

### Commands Sent to Drone

1. `command`
2. repeated `rc a b c d` while keyboard control is enabled
3. repeated `rc 0 0 0 0` when no mapped key is pressed

### Success Criteria

1. State recording includes `keyboard-control` RC samples while mapped keys are pressed.
2. After each key release, RC samples return to `0,0,0,0`.
3. CSV includes `keyboard-control:on:<profile>` and `keyboard-control:off` events.
4. `telemetry_quality` remains comparable to `E-GUI-VID-IDLE`.
5. No drift continues after key release during the optional flight repeat.

## E-KBD-RESPONSE - Keyboard Response Latency

### Purpose

Estimate the delay between a keyboard RC input and the drone's observed physical response in telemetry.

`rc` commands are sent without waiting for a normal SDK response, so this experiment measures command-to-telemetry reaction time instead of command-response latency. Use `vgz` for up/down response, `yaw` for yaw response, and `vgx`/`vgy` for horizontal response if the flight area is large enough.

### Safety

Run only in a clear indoor area with enough space. Use low aggression first, such as `20`. Keep the movement windows short and return to neutral after every input.

### Real Drone Steps

1. Power on Tello and connect the computer to Tello Wi-Fi.
2. Start the control panel.
3. Click `Connect + SDK`.
4. Enable CSV export and choose:
```text
../results/E-KBD-RESPONSE-001.csv
```
5. Start state recording and choose:
```text
../results/E-KBD-RESPONSE-001-state.csv
```
6. Select the keyboard profile to test and set aggression to `20`.
7. Click `takeoff`.
8. Enable keyboard RC control.
9. Run short input pulses:
   - hold `W` for 500 ms, release, wait 3 seconds,
   - hold `S` for 500 ms, release, wait 3 seconds,
   - hold yaw left for 500 ms, release, wait 3 seconds,
   - hold yaw right for 500 ms, release, wait 3 seconds.
10. Disable keyboard RC control.
11. Click `land`.
12. Stop and export state recording.

### Commands Sent to Drone

1. `command`
2. `takeoff`
3. repeated keyboard-generated `rc a b c d`
4. repeated `rc 0 0 0 0` after key release
5. `land`

### Success Criteria

1. Each key press appears as a `keyboard-control` RC sample in the state recording.
2. For up/down pulses, `vgz` changes shortly after the RC sample.
3. For yaw pulses, `yaw` changes shortly after the RC sample.
4. The estimated response delay is stable across repeated pulses.
5. `telemetry_quality` remains `OK` or only briefly `DEGRADED` during the test.
6. After key release, the telemetry trend returns toward neutral instead of continuing indefinitely.

## E-WIFI-LOSS - Wi-Fi Reconnect

### Purpose

Measure command-channel recovery after temporary Wi-Fi loss.

### Real Drone Steps

1. Power on Tello and connect the computer to Tello Wi-Fi.
2. Start the command-watch run below.
3. After 15-20 seconds, temporarily disconnect the computer from Tello Wi-Fi.
4. Wait about 5-10 seconds.
5. Reconnect to Tello Wi-Fi while the process is still running.
6. Let the run continue until duration limit or stop with Ctrl+C.

### Command to Run

```bash
cd tello_core
./build/tello_cli --watch \
  --interval-ms 1000 \
  --duration-s 120 \
  --metrics-csv ../results/E-WIFI-LOSS-001.csv \
  --test-id E-WIFI-LOSS-001 \
  --scenario wifi-reconnect \
  --notes "Manual Wi-Fi disconnect/reconnect during command watch"
```

### Commands Sent to Drone

1. `command`
2. repeated `battery?`

### Success Criteria

1. CSV shows failures/timeouts during Wi-Fi loss.
2. `event` includes reconnect-related transitions when detected.
3. Later rows return to `last_command_result=OK`.
4. `last_outage_failures` captures the outage length in failed attempts.

## E-VID-RESET - Video Stream Reset

### Purpose

Measure video recovery after intentional stream interruption.

### Real Drone Steps

1. Power on Tello and connect the computer to Tello Wi-Fi.
2. Start the video-watch run below.
3. After 15-20 seconds, interrupt only the video stream with another control path if available.
4. Let `tello_cli --video-watch` attempt recovery.
5. Continue until duration limit or stop with Ctrl+C.

### Command to Run

```bash
cd tello_core
./build/tello_cli --video-watch \
  --interval-ms 1000 \
  --duration-s 120 \
  --metrics-csv ../results/E-VID-RESET-001.csv \
  --test-id E-VID-RESET-001 \
  --scenario stream-reset \
  --notes "Manual stream interruption/recovery test"
```

### Commands Sent to Drone

From the test process:
1. `command`
2. `streamon`
3. recovery `streamon` attempts if the stream stalls
4. `streamoff` at shutdown

Optional manual interruption command from another client, if used:
1. `streamoff`

Do not power-cycle the drone for this experiment. Power-cycling resets SDK mode and belongs to `E-PWR-CYCLE`.

### Success Criteria

1. `video_age_ms` increases during stall.
2. `recovery_attempted=1` appears after stall threshold.
3. Packet counters resume increasing after recovery.
4. `recovery_result=OK` for successful recovery rows.

## E-PWR-CYCLE - Power-Cycle Recovery

### Purpose

Measure whether video/session recovery handles a drone power-cycle while the process stays alive.

### Real Drone Steps

1. Power on Tello and connect the computer to Tello Wi-Fi.
2. Start the video-watch run below.
3. After 20-30 seconds, power off the drone.
4. Wait 5-10 seconds.
5. Power the drone back on.
6. Reconnect the computer to Tello Wi-Fi if needed.
7. Keep the process running and observe recovery.

### Command to Run

```bash
cd tello_core
./build/tello_cli --video-watch \
  --interval-ms 1000 \
  --duration-s 180 \
  --metrics-csv ../results/E-PWR-CYCLE-001.csv \
  --test-id E-PWR-CYCLE-001 \
  --scenario power-cycle \
  --notes "Drone power-cycle while video-watch remains running"
```

### Commands Sent to Drone

Before power-cycle:
1. `command`
2. `streamon`

During recovery:
1. recovery `streamon`
2. `battery?` probe to detect whether the command channel is reachable
3. power-cycle recovery path:
   - reinitialize command socket
   - `command`
   - `battery?`
   - `streamon`
   - restart local video receiver/decoder pipeline

At shutdown:
1. `streamoff`

### Success Criteria

1. CSV shows packet stall after power-off.
2. `recovery_attempted=1` appears.
3. `recovery_stage=power_streamon` and `recovery_result=OK` appear after reconnect.
4. Packet counters resume increasing after the drone is reachable again.

## Analysis Notebook

After collecting CSV files, open the analysis notebook:

```bash
jupyter notebook notebooks/analyze_metrics.ipynb
```

The notebook loads:

1. all CSV files in `results/*.csv`,
2. a text summary for each run,
3. plots for command latency, telemetry rate, video packet rate, and decoder FPS.

An example dataset is provided at:

```text
results/sample_metrics.csv
```

An example generated plot is provided at:

```text
results/sample_metrics_plot.png
```

## Reporting Notes

For the final report, record:
1. hardware setup,
2. room/environment conditions,
3. distance from computer to drone,
4. number of runs per scenario,
5. average/min/max command latency,
6. average telemetry rate and maximum packet age,
7. average decode FPS and total decode errors,
8. recovery behavior observed in reconnect/reset/power-cycle tests.
