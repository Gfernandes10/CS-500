# 03 - Telemetry

## Objective
Implement and validate telemetry ingestion from DJI Tello state channel (`UDP 8890`) with robust parsing, thread-safe state access, and observability metrics.

## Implemented Scope
1. Telemetry parser in [tello_core/src/state_parser.cpp](../tello_core/src/state_parser.cpp).
2. Background state receiver in [tello_core/src/state_receiver.cpp](../tello_core/src/state_receiver.cpp).
3. Public telemetry statistics API in [tello_core/include/tello/state_receiver.hpp](../tello_core/include/tello/state_receiver.hpp).
4. Runtime smoke apps:
- Parser smoke: [tello_core/apps/cli/parser_smoke.cpp](../tello_core/apps/cli/parser_smoke.cpp)
- Receiver smoke: [tello_core/apps/cli/state_receiver_smoke.cpp](../tello_core/apps/cli/state_receiver_smoke.cpp)
- Integrated mode in main CLI: [tello_core/apps/cli/main.cpp](../tello_core/apps/cli/main.cpp) (`--state-watch`)

## Telemetry Read Model (Didactic)
1. Drone pushes state packets asynchronously.
2. Receiver thread continuously calls `recv` on port `8890`.
3. Each packet is parsed into `TelloState`.
4. Latest valid state is atomically replaced under mutex.
5. Consumers pull snapshots using `getLatestState()`.
6. The receiver also maintains a configurable recent-state ring buffer.
7. During recording, state samples and RC command samples are stored on the same recording timeline and can be exported to CSV.

The core API remains a pull/latest-state-cache model. ROS2 integration adds pub/sub behavior outside the core by publishing `/tello/state` and related topics from the ROS driver.

## Observability API
`StateReceiver::TelemetryStats` exposes:
1. `packets_total`
2. `packets_valid`
3. `packets_invalid`
4. `recv_timeouts`
5. `recv_errors`
6. `rx_hz_instant`
7. `rx_hz_ema`
8. `last_packet_age_ms`
9. `last_interarrival_ms`
10. `max_interarrival_ms`
11. `gap_events_300ms`
12. `gap_events_500ms`
13. `gap_events_1000ms`

### Rate Computation
- `rx_hz_instant` is computed from inter-packet delta time.
- `rx_hz_ema` applies exponential smoothing for stable visualization.
- `last_packet_age_ms` is computed from monotonic clock at snapshot time.

## Validation Performed
1. Parser valid frame test: passed (`rc=OK`, fields populated).
2. Parser malformed frame test: passed (`rc=PARSE_ERROR`, useful message).
3. Receiver startup/bind test: passed.
4. Runtime packet processing test: passed with real telemetry stream.
5. Metrics visibility test: passed (Hz/age/counters exposed and printed).

## Test Strategy (Offline vs Hardware)
1. Offline unit tests (default)
- Run without drone/hardware dependency.
- Current parser target: `unit_state_parser`.
- Metrics CSV/link-quality behavior is covered by `unit_metrics_collector`.
- Labels: `unit`, `offline`.

2. Optional hardware integration tests
- Enabled only with `-DENABLE_HARDWARE_TESTS=ON`.
- Hardware tests are manual/environment-dependent and are not part of the default offline test run.

### Commands
1. Configure/build default (offline only):
- `cmake -S . -B build`
- `cmake --build build`

2. Run offline tests:
- `ctest --test-dir build -L offline --output-on-failure`

3. Optional hardware tests can be enabled explicitly when a drone is available:
- `cmake -S . -B build -DENABLE_HARDWARE_TESTS=ON`
- `cmake --build build`
- `ctest --test-dir build -L hardware --output-on-failure`

## Known Limitations
1. Core consumers still use pull/latest-state access; pub/sub streaming is provided by the ROS layer.
2. State recording depends on UDP packet arrival and local scheduling; large inter-sample gaps should be interpreted as telemetry/receiver/logging continuity events.
3. Hardware tests remain manual/optional and environment-dependent.

## Phase C Checklist (Current)
- [x] Parser implemented
- [x] Parser smoke validated
- [x] State receiver implemented
- [x] Runtime receiver smoke validated
- [x] Telemetry metrics API (rate/age/counters) implemented
- [x] Telemetry integrated in main CLI (`--state-watch`)
- [x] State history buffer implemented
- [x] State recording and CSV export implemented
- [x] RC command samples recorded alongside state recording
- [x] Automated tests consolidated in project test target (offline default + optional hardware)

---

## Phase C Sign-off (Completed)

**Date:** 2026-06-18

### Status
- Phase C telemetry ingestion is functionally complete and validated for parser correctness, runtime state reception, and telemetry observability.

### Completed Deliverables
1. `state_parser` implementation with malformed-frame handling.
2. `state_receiver` implementation with background UDP receive loop on `8890`.
3. Thread-safe latest-state snapshot API.
4. Telemetry observability API (`rx_hz`, packet counters, timeout/error counters, packet age).
5. CLI integration mode for telemetry (`--state-watch`).
6. State history buffer and state recording/export support.
7. RC command recording on the state recording timeline.
8. Offline unit tests enabled by default and hardware tests kept optional.

### Validation Summary
1. Build: passed.
2. Offline tests (`ctest -L offline`): passed.
3. Parser smoke: passed (valid + malformed frames).
4. Runtime state smoke with real telemetry stream: passed.
5. Observability metrics visibility (Hz/age/counters): passed.

### Residual Risks
1. Consumer model remains pull-based in the core library; ROS provides pub/sub integration.
2. Hardware tests remain manual/optional and environment-dependent.
3. Telemetry quality remains sensitive to Wi-Fi, drone state, and local scheduling.
