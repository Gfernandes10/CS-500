# Phase E - Final Experiment Protocol

## Purpose

This document defines the final real-drone experiment set for the DJI Tello C++ project.

At this stage, the goal is not to test every feature in isolation. The goal is to collect a compact set of evidence that answers the important engineering questions:

1. Is the command channel reliable enough for normal SDK operations?
2. Is telemetry fresh and stable enough for monitoring and future control loops?
3. Is the FFmpeg video path stable enough for live operation and reporting?
4. Does the Qt Control Panel remain responsive while logging telemetry, video, and keyboard control?
5. Does keyboard RC control return to neutral safely and keep a reasonable send cadence?
6. Does the recovery path handle realistic reconnect or power-cycle scenarios?

## What Changed From The Earlier Experiment Matrix

The previous matrix was useful during development, but several experiments now overlap. The final set below removes or merges runs that no longer add much evidence.

### Kept

1. `E1-SMOKE-CMD`: quick sanity check before any longer run.
2. `E2-CMD-BASE`: command-channel baseline.
3. `E3-STATE-CLI`: telemetry baseline without GUI.
4. `E4-VIDEO-CLI`: video baseline without GUI.
5. `E5-GUI-VID-IDLE`: main Control Panel stationary baseline.
6. `E6-KBD-PROFILE`: no-flight keyboard profile validation.
7. `E7-KBD-RESPONSE`: flight keyboard control, neutral behavior, and RC cadence.
8. `E8-PWR-CYCLE`: full recovery validation after drone restart.

### Merged Or Deprecated

| Experiment | Final Status | Reason |
|---|---|---|
| `E-TEL-BASE` | merged into `E3-STATE-CLI` | Both measure telemetry without GUI. The 180 s CLI run is more useful than the shorter baseline. |
| `E-VID-BASE` | merged into `E4-VIDEO-CLI` | Both measure video without GUI. The current FFmpeg stream work makes the longer CLI diagnostic more useful. |
| `E-GUI-BASE` | merged into `E5-GUI-VID-IDLE` | The GUI video-idle run already validates Control Panel logging, telemetry, video, and CSV export. |
| `E-GUI-TEL-IDLE` | optional only | Use only if you need to isolate GUI telemetry without video. It is not required for the final report if `E3-STATE-CLI` and `E5-GUI-VID-IDLE` are clean. |
| `E-GUI-PLOT` | deprecated unless the plot freezes again | Plot rendering is now covered by GUI timing metrics in `E5-GUI-VID-IDLE`. |
| `E-GUI-MOVE` | removed from final run set | Earlier runs did not show meaningful movement-related communication degradation, and normal operation should stay within safe range. |
| `E-LINK-DIST` | removed from final run set | Distance is not a core final claim because the drone will normally operate near the operator/computer. |
| `E-RC-QUALITY` | merged into `E7-KBD-RESPONSE` | Keyboard control is the most relevant RC path for the final implementation. RC cadence is recorded in the state CSV. |
| `E-KBD-NEUTRAL` | merged into `E7-KBD-RESPONSE` | Neutral return is tested before and during the keyboard response flight test. |
| `E9-WIFI-LOSS` | optional recovery diagnostic | Keep only if you specifically want command-only Wi-Fi reconnect evidence. |
| `E10-VID-RESET` | optional recovery diagnostic | Keep only if you can interrupt video without power-cycling the drone. A drone restart belongs to `E8-PWR-CYCLE`. |

## Safety Rules

1. Run flight-related tests only in a clear indoor area.
2. Keep the drone visible and reachable.
3. Keep one hand ready for `land` or `emergency`.
4. Use low RC aggression first, such as `20`.
5. Do not run keyboard flight tests until profile loading and neutral behavior are verified.
6. Avoid long back-to-back flights if `temph` approaches high values or the drone starts shutting down.
7. Prefer bounded CLI runs using `--duration-s`.

## Setup Checklist

1. Build the project:

```bash
cd tello_core
cmake --build build
```

2. Create the results directory:

```bash
mkdir -p ../results
```

3. Power on the DJI Tello.
4. Connect the computer to the Tello Wi-Fi network.
5. Wait 10-15 seconds after power-on.
6. Run the smoke test before any long run.

## Core Metrics To Use In The Report

Use this short metric set to avoid a noisy report.

### Command Channel

| Metric | Meaning |
|---|---|
| `last_command_result` | Whether the latest command returned `OK`, `TIMEOUT`, or `ERROR`. |
| `command_latency_ms_avg` | Average command-response latency for commands that wait for a reply. |
| `command_failures` | Cumulative command failures/timeouts. |
| `last_outage_failures` | Number of failed attempts during a detected outage/recovery window. |

### Telemetry

| Metric | Meaning |
|---|---|
| `telemetry_hz_ema` | Smoothed telemetry receive rate. |
| `telemetry_age_ms` | Age of the latest telemetry packet. |
| `telemetry_max_interarrival_ms` | Worst observed telemetry packet gap. |
| `telemetry_gap_events_500ms` | Count of serious telemetry gaps. |
| `telemetry_quality` / `telemetry_quality_score` | Compact freshness indicator. |
| `steady_elapsed_ms` | Monotonic state CSV time base. Use this for gap plots. |

### Video

| Metric | Meaning |
|---|---|
| `video_age_ms` | Age of the latest decoded/received video data. |
| `video_quality` / `video_quality_score` | Compact video freshness indicator. |
| `decoder_fps_ema` | Smoothed decode rate. |
| `decoder_errors` | Decode error count. |
| `frame_width`, `frame_height` | Confirms valid decoded frames. |

### GUI And Control

| Metric | Meaning |
|---|---|
| `gui_vision_tick_delay_ms` | Qt vision timer delay. High values indicate event-loop scheduling delay. |
| `vision_refresh_duration_ms` | Actual cost of one vision refresh. Low values mean rendering is not the bottleneck. |
| `plot_paint_ms` | Cost of drawing the telemetry plot. |
| `command_mutex_wait_ms` | API command-channel contention. `-1` means a non-blocking RC send skipped instead of waiting. |
| `rc_a`, `rc_b`, `rc_c`, `rc_d` | RC channels recorded in the state CSV. |
| `rc_steady_elapsed_ms` | Monotonic RC command timeline. Use this for RC cadence analysis. |
| `templ`, `temph` | Drone temperature range. Useful for thermal discussion. |

## Quality Labels

The central metrics CSV includes:

1. `telemetry_quality`
2. `telemetry_quality_score`
3. `video_quality`
4. `video_quality_score`

Labels:

1. `NO_DATA`: no packet has been received yet.
2. `OK`: latest data is fresh.
3. `DEGRADED`: data is delayed but still present.
4. `STALE`: data is too old for safe control assumptions.

For a future closed-loop controller, treat `OK` as normal, `DEGRADED` as cautious/limited-control, and `STALE`/`NO_DATA` as unsafe for feedback control.

## Final Experiment Matrix

| ID | Purpose | Tool | Duration | Required |
|---|---|---|---:|---|
| `E1-SMOKE-CMD` | Confirm basic SDK connectivity before the run set. | `tello_cli --once` | one command | Yes |
| `E2-CMD-BASE` | Measure command latency and reliability. | `tello_cli --watch` | 60 s | Yes |
| `E3-STATE-CLI` | Measure telemetry without GUI/video load. | `tello_cli --state-watch` | 180 s | Yes |
| `E4-VIDEO-CLI` | Measure video without Qt rendering. | `tello_cli --video-watch` | 120-180 s | Yes |
| `E5-GUI-VID-IDLE` | Measure Control Panel with FFmpeg video enabled and drone stationary. | `tello_control_panel` | 2-3 min | Yes |
| `E6-KBD-PROFILE` | Validate keyboard profile persistence and mappings. | `tello_control_panel` | manual | Yes |
| `E7-KBD-RESPONSE` | Validate keyboard RC cadence, neutral return, and flight response. | `tello_control_panel` | manual flight | Yes, safety-gated |
| `E8-PWR-CYCLE` | Validate full session/video recovery after drone restart. | `tello_cli --video-watch` | 180 s | Yes |
| `E9-WIFI-LOSS` | Optional command-only reconnect evidence. | `tello_cli --watch` | 120 s | Optional |
| `E10-VID-RESET` | Optional video-only stream reset evidence. | `tello_cli --video-watch` | 120 s | Optional |

## E1-SMOKE-CMD - Connectivity Smoke Test

### Objective

Confirm that the computer is connected to the drone, the SDK command channel opens, and the drone responds to a simple query. This prevents wasting time on longer experiments when the basic connection is not ready.

### Procedure

1. Power on Tello.
2. Connect to Tello Wi-Fi.
3. Wait 10-15 seconds.
4. Run:

```bash
cd tello_core
./build/tello_cli --once \
  --metrics-csv ../results/E1-SMOKE-CMD-001.csv \
  --test-id E1-SMOKE-CMD-001 \
  --scenario command-smoke \
  --notes "pre-experiment connectivity check"
```

### Commands Sent

1. `command`
2. `battery?`

### What To Check

1. `last_command_result=OK`
2. `command_latency_ms_avg` is non-zero and reasonable
3. CSV contains a valid header and data row

## E2-CMD-BASE - Command Baseline

### Objective

Measure command-channel reliability while the drone is idle. This tells us whether normal SDK queries can be sent repeatedly without timeouts or latency spikes.

### Procedure

1. Keep Tello stationary on a flat surface.
2. Run:

```bash
cd tello_core
./build/tello_cli --watch \
  --interval-ms 1000 \
  --duration-s 120 \
  --metrics-csv ../results/E2-CMD-BASE-001.csv \
  --test-id E2-CMD-BASE-001 \
  --scenario command-baseline \
  --notes "Tello idle on desk; command baseline"
```

### Commands Sent

1. `command`
2. repeated `battery?`

### Important Metrics

1. `last_command_result`
2. `command_latency_ms_avg`
3. `command_failures`
4. `last_outage_failures`

### Expected Evidence

Most commands should return `OK`, latency should remain stable, and failures should be low or zero.

## E3-STATE-CLI - Telemetry Baseline Without GUI

### Objective

Measure telemetry freshness without Qt, plotting, or video rendering. This is the clean baseline for the state channel.

### Procedure

1. Keep Tello stationary.
2. Run:

```bash
cd tello_core
./build/tello_cli --state-watch \
  --interval-ms 250 \
  --duration-s 150 \
  --metrics-csv ../results/E3-STATE-CLI-001.csv \
  --test-id E3-STATE-CLI-001 \
  --scenario telemetry-cli-baseline \
  --notes "Telemetry continuity without GUI"
```

### Commands Sent

1. `command`

Telemetry is then received passively on UDP port `8890`.

### Important Metrics

1. `telemetry_hz_ema`
2. `telemetry_age_ms`
3. `telemetry_max_interarrival_ms`
4. `telemetry_gap_events_500ms`
5. `telemetry_quality`

### Expected Evidence

Telemetry should remain mostly `OK`. If this run has serious gaps, later GUI runs should not be blamed first.

## E4-VIDEO-CLI - Video Baseline Without GUI

### Objective

Measure video continuity without Qt rendering. This tells us whether the video stream and decoder are healthy before adding Control Panel load.

### Procedure

1. Keep Tello stationary with the camera facing a well-lit scene.
2. Run:

```bash
cd tello_core
./build/tello_cli --video-watch \
  --interval-ms 1000 \
  --duration-s 180 \
  --metrics-csv ../results/E4-VIDEO-CLI-001.csv \
  --test-id E4-VIDEO-CLI-001 \
  --scenario video-cli-baseline \
  --notes "Video continuity without Control Panel"
```

### Commands Sent

1. `command`
2. `streamon`
3. recovery commands if the stream stalls
4. `streamoff` at shutdown

### Important Metrics

1. `video_age_ms`
2. `video_quality`
3. `decoder_fps_ema`
4. `decoder_errors`
5. `frame_width`, `frame_height`
6. recovery fields if a stall occurs

### Expected Evidence

Video quality should remain mostly `OK`, decoded frames should be valid, and decoder FPS should stabilize after startup.

## E5-GUI-VID-IDLE - Control Panel Stationary Baseline

### Objective

Measure the final Control Panel under normal stationary use: telemetry, FFmpeg video display, GUI timers, CSV recording, and state recording. This is the main GUI baseline.

### Procedure

1. Start the Control Panel:

```bash
cd tello_core
./build/tello_control_panel
```

2. Click `Connect + SDK`.
3. In Config, set:

```text
GUI metrics csv: ../results/E5-GUI-VID-IDLE-001.csv
state csv:       ../results/E5-GUI-VID-IDLE-001-state.csv
```

4. Click `Start Recording`.
5. In Operation, select `FFmpeg Stream` as the Vision backend.
6. Click `Start Vision`.
7. Keep the drone stationary for 2-3 minutes.
8. Click `Stop Vision`.
9. Click `Stop Recording`.
10. Click `Export CSVs`.

### Commands Sent

1. `command`
2. `streamon`
3. SDK keepalive when no RC stream is active
4. `streamoff`

### Important Metrics

1. `telemetry_quality`
2. `video_quality`
3. `decoder_fps_ema`
4. `gui_vision_tick_delay_ms`
5. `vision_refresh_duration_ms`
6. `plot_paint_ms`
7. `command_mutex_wait_ms`
8. state CSV `steady_elapsed_ms` gaps

### Expected Evidence

Telemetry and video should remain fresh. If GUI tick delay spikes but `vision_refresh_duration_ms` remains low, the issue is scheduling/event-loop timing rather than rendering cost.

## E6-KBD-PROFILE - Keyboard Profile Validation

### Objective

Validate keyboard profile persistence and mappings without flight. This confirms that the user-configurable control feature loads, saves, and maps keys correctly before any RC flight test.

### Procedure

1. Start the Control Panel.
2. Connect SDK.
3. In Config, create or select a profile named `KeyboardTest`.
4. Set aggression to `20`.
5. Map keys, for example:
   - `W` -> up,
   - `S` -> down,
   - `A` -> left,
   - `D` -> right,
   - arrow up -> forward,
   - arrow down -> backward,
   - arrow left -> yaw left,
   - arrow right -> yaw right.
6. Save the profile.
7. Close and reopen the Control Panel.
8. Confirm that profile name, aggression, and mappings persist.

### Commands Sent

1. `command`
2. No non-neutral RC should be sent unless keyboard control is explicitly enabled.

### Important Evidence

1. `control_profiles.json` exists.
2. The profile reloads correctly.
3. Keyboard control is disabled by default until explicitly enabled.

## E7-KBD-RESPONSE - Keyboard RC Safety And Flight Response

### Objective

Validate the final keyboard RC path in flight while the system is also receiving telemetry and rendering FFmpeg video. This is the main "real operation" experiment because the drone is airborne, keyboard RC commands are active, telemetry is being logged, and video is being decoded/displayed at the same time.

This one experiment covers:

1. keyboard control activation,
2. repeated RC sending from the dedicated worker,
3. neutral return after key release,
4. command-to-motion response in telemetry,
5. telemetry freshness while the drone is flying,
6. video freshness/decoder performance while the drone is flying,
7. GUI responsiveness while receiving video, telemetry, and RC input,
8. safety behavior around `takeoff` and `land`.

### Safety

Run only in a clear indoor area. Use aggression `20` first. Keep movement windows short. Be ready to click `land` or `emergency`.

### Procedure

1. Start the Control Panel and connect SDK.
2. Set:

```text
GUI metrics csv: ../results/E7-KBD-RESPONSE-001.csv
state csv:       ../results/E7-KBD-RESPONSE-001-state.csv
```

3. Click `Start Recording`.
4. In Operation, select `FFmpeg Stream`.
5. Click `Start Vision` and wait until decoded frames are visible.
6. Select the tested keyboard profile.
7. Confirm aggression is `20`.
8. Click `takeoff` and confirm the dialog.
9. Let the drone hover for 5-10 seconds while video and telemetry continue running.
10. Enable keyboard control.
11. Run short pulses:
   - hold up for about 500 ms, release, wait 3 seconds,
   - hold down for about 500 ms, release, wait 3 seconds,
   - hold yaw left for about 500 ms, release, wait 3 seconds,
   - hold yaw right for about 500 ms, release, wait 3 seconds.
12. Optional if space is safe:
   - short forward/backward pulses,
   - short left/right pulses.
13. Disable keyboard control.
14. Click `land`.
15. Click `Stop Vision`.
16. Stop recording and export CSVs.

### Commands Sent

1. `command`
2. `streamon`
3. `takeoff`
4. repeated keyboard-generated `rc a b c d`
5. repeated `rc 0 0 0 0` when no mapped key is pressed
6. `land`
7. `streamoff`

### Important Metrics

1. state CSV `rc_a`, `rc_b`, `rc_c`, `rc_d`
2. state CSV `rc_steady_elapsed_ms`
3. RC p95/p99/max send gap from the notebook
4. `command_mutex_wait_ms`
5. `telemetry_quality`
6. `telemetry_hz_ema`
7. `telemetry_age_ms`
8. `telemetry_gap_events_500ms`
9. `video_quality`
10. `video_age_ms`
11. `decoder_fps_ema`
12. `decoder_errors`
13. `gui_vision_tick_delay_ms`
14. `vision_refresh_duration_ms`
15. `plot_paint_ms`
16. `vgz` for up/down response
17. `yaw` for yaw response
18. `h` / `tof` for height behavior

### Expected Evidence

1. RC channels match the pressed keys.
2. RC returns to `0,0,0,0` after release.
3. RC p99 gap remains low enough for safe manual control.
4. `command_mutex_wait_ms` does not show blocking stalls during keyboard RC.
5. Telemetry shows motion shortly after RC pulses.
6. Telemetry remains mostly `OK` during hover and RC pulses.
7. Video remains mostly `OK`, decoder FPS stays usable, and decode errors do not grow rapidly.
8. GUI timing does not show long stalls that would make keyboard control unsafe.
9. The drone lands normally.

## E8-PWR-CYCLE - Power-Cycle Recovery

### Objective

Validate the recovery strategy after the drone restarts while the process is still running. This is the most complete recovery test because a power-cycle resets SDK mode and video state.

### Procedure

1. Power on Tello and connect to Wi-Fi.
2. Start:

```bash
cd tello_core
./build/tello_cli --video-watch \
  --interval-ms 1000 \
  --duration-s 180 \
  --metrics-csv ../results/E8-PWR-CYCLE-001.csv \
  --test-id E8-PWR-CYCLE-001 \
  --scenario power-cycle \
  --notes "Drone power-cycle while video-watch remains running"
```

3. After 20-30 seconds, power off the drone.
4. Wait 5-10 seconds.
5. Power the drone back on.
6. Reconnect to Tello Wi-Fi if needed.
7. Let the process continue and observe recovery.

### Commands Sent

Before power-cycle:
1. `command`
2. `streamon`

During recovery:
1. recovery `streamon`
2. `battery?` command-channel probe
3. reinitialize command socket
4. `command`
5. `battery?`
6. `streamon`
7. restart local video receiver/decoder pipeline

At shutdown:
1. `streamoff`

### Important Metrics

1. `recovery_attempted`
2. `recovery_stage`
3. `recovery_result`
4. `recovery_hard`
5. `recovery_command_channel_available`
6. `video_age_ms`
7. `video_quality`
8. `last_outage_failures`

### Expected Evidence

The CSV should show the video/session stall, recovery attempt stages, successful SDK re-entry, and resumed video packets after the drone is reachable again.

## Optional: E9-WIFI-LOSS - Command Reconnect

### When To Run

Run this only if the report needs a command-only reconnect result. It is not required if `E8-PWR-CYCLE` already demonstrates the stronger recovery case.

### Procedure

```bash
cd tello_core
./build/tello_cli --watch \
  --interval-ms 1000 \
  --duration-s 120 \
  --metrics-csv ../results/E9-WIFI-LOSS-001.csv \
  --test-id E9-WIFI-LOSS-001 \
  --scenario wifi-reconnect \
  --notes "Manual Wi-Fi disconnect/reconnect during command watch"
```

After 15-20 seconds, disconnect from Tello Wi-Fi for 5-10 seconds, reconnect, and let the run finish.

### Important Metrics

1. `last_command_result`
2. `command_failures`
3. `connection_state`
4. `last_outage_failures`

## Optional: E10-VID-RESET - Video-Only Stream Reset

### When To Run

Run this only if you can interrupt video without restarting the drone. If you power-cycle the drone, use `E8-PWR-CYCLE` instead.

### Procedure

```bash
cd tello_core
./build/tello_cli --video-watch \
  --interval-ms 1000 \
  --duration-s 120 \
  --metrics-csv ../results/E10-VID-RESET-001.csv \
  --test-id E10-VID-RESET-001 \
  --scenario stream-reset \
  --notes "Manual stream interruption/recovery test"
```

After 15-20 seconds, interrupt the stream from another control path if available, then let recovery run.

### Important Metrics

1. `video_age_ms`
2. `recovery_attempted`
3. `recovery_stage`
4. `recovery_result`
5. `video_quality`

## Analysis Notebook

After collecting CSV files, open:

```bash
jupyter notebook notebooks/analyze_metrics.ipynb
```

The notebook loads all `results/*.csv` files and generates:

1. per-experiment summaries,
2. CLI vs GUI telemetry comparisons,
3. video freshness comparisons,
4. GUI timing/rendering comparisons,
5. keyboard RC cadence comparisons,
6. wall-clock vs steady-clock validation for state CSV gaps,
7. recovery-stage summaries.

For state CSV gap analysis, use `steady_elapsed_ms` and `rc_steady_elapsed_ms`, not wall-clock `timestamp_ms`.

## Reporting Notes

For the final report, focus on these conclusions:

1. Command channel reliability from `E2-CMD-BASE`.
2. Telemetry stability from `E3-STATE-CLI` and `E5-GUI-VID-IDLE`.
3. Video stability from `E4-VIDEO-CLI` and `E5-GUI-VID-IDLE`.
4. GUI responsiveness from GUI timing metrics.
5. Keyboard RC safety from `E7-KBD-RESPONSE`.
6. Recovery behavior from `E8-PWR-CYCLE`.
7. Thermal/link limitations from `temph` and any observed quality degradation.
