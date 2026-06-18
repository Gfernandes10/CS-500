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
- `ConnectionEvent`: `LOST`, `RESTORED`.

5. Reliability policy is configurable at client API level.
- `TelloClient::ReliabilityConfig` centralizes command retry and session recovery constants.
- This avoids scattered hardcoded timing values.

6. CLI supports two operational modes.
- `--once`: single smoke run for quick checks.
- `--watch`: continuous reconnect test loop for runtime validation.

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
1. Stop responsiveness depends on active network call windows.
2. No formal automated unit tests yet for failure-injection paths.

## Phase B Exit Criteria
- [x] Build succeeds.
- [x] `command` entry mode works in normal conditions.
- [x] `battery?` returns payload reliably in connected state.
- [x] Recovery works after Wi-Fi loss and return in same process.
- [x] Connection state/events are exposed by `TelloClient`.

## Next Step (Phase C)
Start telemetry implementation:
1. Parser robustness in [tello_core/src/state_parser.cpp](../tello_core/src/state_parser.cpp).
2. Receiver loop and thread-safe state in [tello_core/src/state_receiver.cpp](../tello_core/src/state_receiver.cpp).
3. Unit tests focused on state parsing edge cases.
