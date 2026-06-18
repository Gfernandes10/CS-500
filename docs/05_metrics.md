# Phase E - Metrics and Evaluation

## Objective
Create a repeatable metrics pipeline for command/telemetry/video evaluation and final report analysis.

## Current Implementation (E1)
1. Viewer runtime metrics export to CSV is implemented.
2. Export is enabled with command-line option:
- --metrics-csv PATH
- Optional E4 experiment metadata:
- --test-id ID
- --scenario NAME
- --notes TEXT

Example:
- ./build/tello_viewer --interval-ms 1000 --duration-s 30 --metrics-csv results_viewer.csv --test-id E4-001 --scenario normal --notes "baseline run"

## Current Implementation (E2)
1. CLI video-watch metrics export to CSV is implemented.
2. Export is enabled with command-line option:
- --metrics-csv PATH
- Optional E4 experiment metadata:
- --test-id ID
- --scenario NAME
- --notes TEXT

Example:
- ./build/tello_cli --video-watch --interval-ms 1000 --duration-s 30 --metrics-csv results_cli_video.csv --test-id E4-002 --scenario normal --notes "video baseline"

## Current Implementation (E3)
1. Command-channel latency export in `--once` and `--watch` modes is implemented.
2. Export is enabled with command-line option:
- --metrics-csv PATH
- Optional E4 experiment metadata:
- --test-id ID
- --scenario NAME
- --notes TEXT

Example:
- ./build/tello_cli --watch --interval-ms 1000 --duration-s 30 --metrics-csv results_cli_command.csv --test-id E4-003 --scenario reconnect --notes "wifi flap"

## Current Implementation (E4)
1. Unified experiment metadata is implemented for all current CSV exports (viewer, CLI video-watch, CLI command channel).
2. CSV rows now include a shared metadata prefix for run grouping and post-analysis filtering.

## CSV Schema (Current)
1. test_id
2. scenario
3. notes
4. run_mode
5. elapsed_ms
6. packets_total
7. nal_units
8. frames_decoded
9. keyframes
10. decode_fps_ema
11. decode_errors
12. width
13. height
14. paused
15. overlay_enabled

## CSV Schema (CLI Video-Watch)
1. test_id
2. scenario
3. notes
4. run_mode
5. elapsed_ms
6. attempt
7. packets_total
8. delta
9. pps_ema
10. bytes_total
11. age_ms
12. timeouts
13. errors
14. nal_units
15. nal_sps
16. nal_pps
17. nal_idr
18. nal_nonidr
19. nal_other
20. nal_gated
21. parser_resyncs
22. buffered_bytes
23. dec_frames
24. dec_fps_ema
25. dec_errors
26. recovery_attempted
27. recovery_result
28. recovery_hard
29. event
30. cmd_state

## CSV Schema (CLI Command Channel)
1. test_id
2. scenario
3. notes
4. run_mode
5. elapsed_ms
6. attempt
7. command
8. latency_ms
9. result
10. response
11. cmd_state
12. event
13. last_outage_failures

## Validation Performed
1. Build passed with metrics export integrated.
2. Runtime test generated CSV file with header and data rows.
3. Observed values matched live terminal logs for packet/decode counters.
4. CLI CSV export generated expected header and data rows for bounded video-watch run.
5. Command CSV export generated expected header and multiple timing rows for bounded watch run.
6. Unified metadata fields validated in CLI once/watch, CLI video-watch, and viewer exports.

## Next Metrics Steps
1. Add telemetry rate and age export in the same metadata-prefixed format.
2. Define experimental protocol matrix (normal run, stream reset, Wi-Fi reconnect, power-cycle).
3. Add a small script/template for plotting core metrics from generated CSV files.
4. Add optional experiment metadata dictionary file for scenario normalization.
