# Phase A Architecture Overview

## Goal
Establish a clean and buildable C++ project skeleton for the Tello communication library, with clear module boundaries and stub implementations.

## High-Level Modules

1. Core networking and commands
- `UdpSocket`: low-level UDP abstraction.
- `CommandExecutor`: command send/receive policy (retry/timeout).
- `TelloClient`: high-level facade API.

2. Telemetry
- `StateParser`: parses raw state strings into structured data.
- `StateReceiver`: background receiver and thread-safe latest state access.

3. Video
- `VideoReceiver`: receives raw video packets and dispatches packet callbacks.

4. Metrics and logging
- `MetricsCollector`: runtime performance metrics container.
- `Logger` / `ConsoleLogger`: pluggable logging abstraction.

## Folder-Level Architecture

- `include/tello/`
  - Public contracts (headers) for all modules.
- `src/`
  - Source files with Phase A stubs.
- `apps/cli/`
  - Minimal executable to validate library wiring.
- `cmake/`
  - Compiler flags and project build options.
- `docs/`
  - Technical documentation and phase records.
- `tests/`
  - Reserved for unit/integration tests (Phase B+).

## Dependency Direction

- `apps/cli` depends on `tello_core`.
- `tello_core` depends on:
  - Standard library
  - Threads package (CMake `find_package(Threads)`)

Inside `tello_core`:
- `TelloClient` depends on `UdpSocket` and `CommandExecutor`.
- `StateReceiver` depends on `UdpSocket` and `StateParser`.
- `VideoReceiver` depends on `UdpSocket`.
- All modules can use `Logger` and shared `types`.

## Current Status (Phase A)

Implemented:
- Full folder structure.
- Root CMake and compiler options.
- Header contracts for all initial modules.
- `.cpp` stubs with TODO markers.
- Minimal CLI application.

Not implemented yet:
- Real UDP communication.
- Command parsing and retry logic.
- Telemetry parsing/receive loop.
- Video packet processing.
- Metrics calculations.

## Next Step
Run full build validation to confirm all stubs compile and link successfully.