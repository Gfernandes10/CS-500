# CS 500 Final Report Plan

## Working Title

**Development and Evaluation of a Modular C++ Communication, Control, and Visualization System for the DJI Tello Drone**

## Report Metadata

- Course: CS 500 Project
- Student: Gabriel Fernandes
- Supervisor: Prof. Stefan Bruda
- Project: DJI Tello C/C++ communication library with desktop GUI and planned ROS2 integration
- Draft language: English
- Target length: medium technical report
- Figure strategy: embed the most important generated figures in the main body; keep supporting plots available for appendix use

## Planned Structure

1. **Abstract**
   - Summarize the project objective, implemented system, experimental validation, and ROS2 as planned next stage.

2. **Introduction**
   - Motivate a modular communication/control stack for a low-cost robotics platform.
   - Define the CS 500 project scope.
   - State the main technical objectives: command reliability, telemetry, video, metrics, GUI, keyboard RC control, recovery, and ROS2 future integration.

3. **Background**
   - Explain the DJI Tello SDK channels:
     - command channel on UDP `8889`;
     - telemetry state channel on UDP `8890`;
     - video channel on UDP `11111`.
   - Explain SDK mode, command responses, `rc` control, and FFmpeg's role in H264 video.

4. **System Architecture**
   - Present `tello_core` as the reusable C++ library.
   - Describe the main modules:
     - `UdpSocket`;
     - `CommandExecutor`;
     - `TelloClient`;
     - `StateParser` and `StateReceiver`;
     - FFmpeg Stream video module;
     - `MetricsCollector`;
     - CLI tools;
     - Qt Control Panel.
   - Emphasize that the GUI and CLI are clients of the same core library.

5. **Implementation**
   - Command channel: retry/timeout policy, stale response handling, SDK keepalive, recovery state.
   - Telemetry: parser, background receiver, latest-state cache, monotonic timestamps.
   - Video: FFmpeg Stream as the only supported video runtime path.
   - Metrics: central CSV schema, experiment metadata, and generated plots/tables.
   - Qt Control Panel: command UI, telemetry plot, video, CSV export, safety controls.
   - Keyboard RC: configurable profiles, dedicated RC worker, neutral command behavior.

6. **Milestone Plan Traceability**
   - Map each original CS500 milestone phase to delivered work.
   - Explicitly identify fully delivered phases, expanded phases, partial deliverables, and remaining gaps.
   - State that ROS2 integration is the main technical milestone still pending.

7. **Experimental Methodology**
   - Include the final experiment matrix:
     - `E1-SMOKE-CMD`;
     - `E2-CMD-BASE`;
     - `E3-STATE-CLI`;
     - `E4-VIDEO-CLI`;
     - `E5-GUI-VID-IDLE`;
     - `E6-KBD-PROFILE`;
     - `E7-KBD-RESPONSE`;
     - `E8-PWR-CYCLE`;
     - `E9-WIFI-LOSS`.

8. **Results**
   - Main quantitative findings:
     - command baseline average latency around `34.6 ms` with zero final failures;
     - telemetry quality remained `OK` in baseline runs;
     - FFmpeg Stream video reached about `31-32 FPS`;
     - GUI idle run maintained video and telemetry quality;
     - keyboard flight recorded `1106` RC rows and `291` non-zero RC rows;
     - median vertical RC reaction latency was `814 ms`;
     - power-cycle recovery was captured with `8` recovery rows;
     - Wi-Fi loss/reconnect run ended with command recovery.

9. **Discussion**
   - Interpret command reliability, telemetry freshness, FFmpeg Stream decision, GUI timing, keyboard RC safety, and recovery behavior.
   - Discuss practical constraints: Wi-Fi, Tello thermal behavior, SDK limitations, and real-device variability.

10. **Limitations**
   - Single-drone test setup.
   - Limited repetitions per scenario.
   - No external motion capture for RC response latency.
   - No closed-loop controller yet.
   - ROS2 integration still pending.

11. **Future Work / ROS2 Placeholder**
    - Add `ros/tello_ros2_bridge`.
    - Publish telemetry topics.
    - Publish camera frames if feasible.
    - Subscribe to velocity/RC command inputs.
    - Expose takeoff, land, and emergency services.
    - Keep ROS2 layer thin and reuse `tello_core`.

12. **Conclusion**
    - Summarize implemented features, validation results, and readiness for ROS2 integration.

13. **Appendix**
    - DJI Tello SDK command summary.
    - Experiment summary.
    - CSV schema summary if space permits.
    - Additional figures.
    - Build and environment notes.

## Execution Checklist For Current Revision

1. Treat FFmpeg Stream as the only supported video path in code, build configuration, documentation, and report text.
2. Remove the retired video path from public/runtime behavior and avoid presenting it as retained functionality.
3. Add a reader-friendly DJI Tello SDK introduction and SDK command appendix.
4. Remove internal report language, including direct references to planning documents and result-generation artifacts.
5. Enumerate Qt Control Panel Config and Operation features so annotated screenshots can map labels to report text.
6. Define quality as transport freshness/continuity and state the `OK`, `DEGRADED`, `STALE`, and `NO_DATA` criteria.
7. Clarify the RC reaction plot: one point per valid vertical RC pulse, blue for upward commands, orange for downward commands.
