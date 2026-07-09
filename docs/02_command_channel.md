# 02 - Command Channel

## Objective
Implement and validate a reliable command/response channel for DJI Tello over UDP, including retry/timeout handling and recovery after Wi-Fi loss.

## Implemented Scope
1. UDP command socket implementation in [tello_core/src/udp_socket.cpp](../tello_core/src/udp_socket.cpp).
2. Command execution with retry, timeout, and response parsing in [tello_core/src/command_executor.cpp](../tello_core/src/command_executor.cpp).
3. Client-level command session recovery and SDK re-entry in [tello_core/src/tello_client.cpp](../tello_core/src/tello_client.cpp).
4. CLI validation loop with connection state visibility in [tello_core/apps/cli/main.cpp](../tello_core/apps/cli/main.cpp).

## Technical Decisions
1. Fixed command endpoint.
- Remote endpoint: `192.168.10.1:8889`.
- Local command bind kept stable via `local_port` to reduce UDP endpoint drift across runs.

2. Layered reliability model.
- `UdpSocket`: transport operations and timeout mapping.
- `CommandExecutor`: per-command retry/timeout policy.
- `TelloClient`: session-level recovery (`command` re-entry, channel reopen, hard reinitialize fallback).

3. Query-response protection.
- Query commands (e.g., `battery?`) reject stale `ok` acknowledgments when necessary.

4. Connection state is owned by the client layer.
- `ConnectionState`: `CONNECTED`, `RECOVERING`, `DISCONNECTED`.
- `ConnectionEvent`: `LOST`, `RESTORED`, `TRANSIENT_LOSS_RECOVERED`.
- `TRANSIENT_LOSS_RECOVERED` is used when one or more internal attempts fail but the high-level command eventually returns `OK` after recovery/retry.

5. Reliability policy is configurable at client API level.
- `TelloClient::ReliabilityConfig` centralizes command retry and session recovery constants.
- This avoids scattered hardcoded timing values.

6. CLI supports two operational modes.
- `--once`: single smoke run for quick checks.
- `--watch`: continuous reconnect test loop for runtime validation.
- `--state-watch`: telemetry receiver diagnostics and metrics export.
- `--video-watch`: FFmpeg Stream video diagnostics and recovery validation.

7. Critical commands use a longer timeout.
- Normal queries such as `battery?` use the regular command timeout.
- Critical operations such as `takeoff`, `land`, stream control, recovery, and emergency paths can use the critical-command timeout policy.
- In the Qt Control Panel, critical commands are dispatched through a background command worker so the GUI event loop does not wait for SDK timeouts.

8. RC commands use a no-wait send path.
- Continuous `rc a b c d` commands are sent without waiting for an SDK response.
- The project intentionally does not depend on per-RC ACK behavior for keyboard control.
- A dedicated RC worker keeps command timing independent from GUI rendering.

9. SDK keepalive is centralized in `TelloClient`.
- When no RC stream is active, keepalive can periodically send `battery?`.
- This avoids long idle periods with no SDK commands; the Tello SDK states that the drone automatically lands if it receives no command for 15 seconds.
- Keepalive is paused around manual/critical command paths and while keyboard RC is active.

## Validation Matrix

### Scenario 1 - Baseline connected startup
Steps:
1. Connect host to Tello Wi-Fi.
2. Run CLI reconnect test loop.
Expected:
- `initialize: OK`
- `battery?: OK | response="N"`
- state converges to `CONNECTED`.
Result:
- Passed.

### Scenario 2 - Link loss and restoration (same process)
Steps:
1. Start in connected state.
2. Turn Wi-Fi off while process is running.
3. Turn Wi-Fi on again without restarting process.
Expected:
- transient `TIMEOUT/ERROR`
- state transitions through `RECOVERING`/`DISCONNECTED`
- eventual recovery to `CONNECTED`
- battery queries resume.
Result:
- Passed after client-level recovery hardening.

### Scenario 3 - Startup without Wi-Fi, then reconnect
Steps:
1. Start process with Wi-Fi disconnected.
2. Reconnect Wi-Fi while process remains running.
Expected:
- initial failures
- eventual transition to `CONNECTED` and successful query.
Result:
- Passed after fixing early-return recovery paths.

### Scenario 4 - Repetition stability
Steps:
1. Execute command checks repeatedly.
Expected:
- no random stuck state under stable link.
Result:
- Passed in repeated runs.

## Known Limitations
1. UDP command/response behavior can produce delayed or missing responses even when the drone physically executes the command.
2. Real-drone testing showed recovered transient command outages around periodic `battery?` queries; packet capture indicated absent or late drone responses rather than local API parsing failure.
3. RC commands are intentionally no-wait; safety depends on repeated RC cadence, neutral fallback, and link-quality monitoring rather than per-command acknowledgement.
4. Additional offline tests are still desirable for failure-injection and retry paths.

## Phase B Exit Criteria
- [x] Build succeeds.
- [x] `command` entry mode works in normal conditions.
- [x] `battery?` returns payload reliably in connected state.
- [x] Recovery works after Wi-Fi loss and return in same process.
- [x] Connection state/events are exposed by `TelloClient`.
- [x] Transient recovered command outages are represented separately from persistent loss.
- [x] Critical-command timing and command attempt logs are exported through metrics.
