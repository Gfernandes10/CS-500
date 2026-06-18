# CS500 Milestone Plan  
**Project: DJI Tello C/C++ Communication Library with Desktop and ROS Integration**  
**Timeline: May 4 → August 7 (≈13 weeks)**

---

# Phase 0 — Project Definition & Architecture  
**May 4 – May 10 (Week 1)**

## Objectives
Define a clear and feasible project scope aligned with CS500 requirements, and establish a solid architectural foundation before implementation begins.

## Tasks
- Study the DJI Tello SDK communication model (command, state, video channels)
- Define system architecture:
  - modules (core, telemetry, video, ROS, GUI)
  - data flow between components
  - threading model
- Define API design for the core library:
  - command interface
  - telemetry access
  - video callbacks
- Choose technologies:
  - C++17
  - CMake build system
  - ROS2 (Humble or compatible)
  - Qt (tentative for GUI)
- Create repository structure:
  - `core/`
  - `apps/`
  - `ros/`
  - `tests/`
- Define coding conventions and logging strategy

## Deliverables
- Project specification document (2–3 pages)
- High-level architecture diagram
- Initial repository with folder structure and CMake setup

## Methodology
- Top-down system design
- Interface-first approach (define APIs before implementation)
- Keep architecture modular and minimal to ensure feasibility

---

# Phase 1 — Core Networking & Command Layer  
**May 11 – May 24 (Weeks 2–3)**

## Objectives
Implement reliable bidirectional communication with the DJI Tello drone using UDP sockets.

## Tasks
- Implement UDP socket abstraction layer:
  - socket initialization
  - send/receive functions
- Implement command channel:
  - send commands (`command`, `takeoff`, `land`, etc.)
  - receive and validate responses (`ok`, `error`)
- Implement timeout and retry mechanism:
  - configurable timeout duration
  - retry logic for failed commands
- Implement basic logging:
  - command sent
  - response received
  - errors and timeouts
- Develop a CLI test application:
  - connect to drone
  - send manual commands
  - print responses

## Deliverables
- Functional `TelloClient` class
- Command handling module
- CLI tool demonstrating command execution
- Logging system integrated into core

## Methodology
- Incremental development with frequent real-device testing
- Validate each command individually before abstraction
- Keep implementation simple and testable
- Use synchronous model first, then refine if needed

---

# Phase 2 — Telemetry System  
**May 25 – June 7 (Weeks 4–5)**

## Objectives
Implement structured reception and parsing of telemetry data from the drone.

## Tasks
- Implement telemetry UDP receiver:
  - continuous reception loop
  - buffering of incoming data
- Parse telemetry messages:
  - convert raw strings into structured data
  - extract fields (battery, pitch, roll, yaw, height, etc.)
- Define `TelloState` data structure
- Implement thread-safe access:
  - mutex or lock-free approach
  - safe read/write patterns
- Expose telemetry API:
  - getter functions
  - optional callback mechanism

## Deliverables
- `StateReceiver` module
- Structured telemetry representation
- API for accessing real-time drone state
- Logging of telemetry data

## Methodology
- Separate parsing logic from networking logic
- Validate parsed values against expected ranges
- Use unit tests for parser validation
- Ensure thread safety from the beginning

---

# Phase 3 — Video Stream Integration  
**June 8 – June 21 (Weeks 6–7)**

## Objectives
Receive, decode, and expose the drone’s video stream.

## Tasks
- Implement video UDP receiver:
  - handle continuous stream packets
- Integrate decoding pipeline:
  - OpenCV or FFmpeg for frame decoding
- Convert stream into usable frame format
- Implement frame access interface:
  - callback-based
  - or polling-based
- Create a minimal viewer:
  - simple window displaying frames
  - optional FPS counter

## Deliverables
- `VideoReceiver` module
- Frame decoding pipeline
- Basic video display tool
- API for accessing frames

## Methodology
- Build pipeline step-by-step:
  1. receive raw packets
  2. reconstruct frames
  3. decode frames
  4. display frames
- Validate each stage independently
- Keep implementation minimal and efficient

---

# Phase 4 — Evaluation & Metrics  
**June 22 – July 5 (Weeks 8–9)**

## Objectives
Introduce experimental evaluation to meet CS500 academic requirements.

## Tasks
- Implement measurement tools:
  - command-response latency
  - telemetry update rate (Hz)
  - video frame rate (FPS)
- Add timestamp-based logging
- Add unified experiment metadata in CSV exports (`test_id`, `scenario`, `notes`, `run_mode`)
- Design experiments:
  - multiple runs per metric
  - controlled conditions (idle vs active)
- Collect data:
  - log files
  - structured outputs
- Analyze results:
  - compute averages, min/max, standard deviation
  - identify performance bottlenecks

## Deliverables
- Benchmarking module integrated into library
- Experimental data logs
- Metadata-ready CSV datasets for cross-run grouping/filtering
- Tables and/or plots of results
- Initial performance analysis

## Methodology
- Empirical evaluation approach
- Repeatable experiments
- Statistical analysis (mean, variance)
- Compare system behavior under different conditions

---

# Phase 5 — ROS Integration  
**July 6 – July 19 (Weeks 10–11)**

## Objectives
Expose the system through ROS2 for integration with robotics applications.

## Tasks
- Create ROS2 package
- Wrap core library inside ROS node
- Implement publishers:
  - telemetry topics
  - camera frames
- Implement subscribers:
  - velocity/control commands
- Implement services/actions:
  - takeoff
  - landing
- Test using ROS tools:
  - `ros2 topic echo`
  - `rqt`

## Deliverables
- ROS2 package
- Functional ROS node
- Demonstration of message flow
- Integration with core library

## Methodology
- Keep ROS layer thin (no duplication of logic)
- Use standard message types where possible
- Test incrementally (topic by topic)
- Validate integration with real drone

---

# Phase 6 — Desktop GUI 
**July 20 – July 30 (Week 12)**

## Objectives
Develop a simple GUI application for visualization and control.

## Tasks
- Set up Qt project
- Implement main window layout:
  - video panel
  - telemetry display
  - control buttons
- Connect GUI to core library:
  - display live video
  - update telemetry in real time
  - send control commands
- Implement basic controls:
  - takeoff
  - land
  - directional commands

## Deliverables
- Functional desktop application
- Integrated visualization and control interface

## Methodology
- Focus on simplicity and usability
- Avoid complex UI design
- Reuse existing APIs from core library
- Treat GUI as demonstration client, not core component

---

# Phase 7 — Final Report & Presentation  
**July 31 – August 7 (Week 13)**

## Objectives
Produce the final academic deliverables required by CS500.

## Tasks
- Write final report:
  - introduction and motivation
  - system architecture
  - implementation details
  - evaluation methodology
  - results and discussion
- Prepare presentation slides
- Create demonstration:
  - live demo or recorded video
- Review and refine documentation

## Deliverables
- Final technical report
- Presentation slides
- Demo (live or recorded)

## Methodology
- Structured technical writing
- Clear explanation of design decisions
- Use figures/diagrams to support explanations
- Present quantitative results clearly