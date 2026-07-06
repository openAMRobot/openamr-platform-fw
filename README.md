# OpenAMR Platform Firmware

Embedded firmware, microcontroller code, motor controller interfaces, sensor drivers, and low-level control firmware for the OpenAMR mobile robot platform.

## Repository Status

Current maturity level: Experimental

## Purpose

This repository contains firmware-related resources for the OpenAMR mobile robot platform, including:

- microcontroller firmware
- motor controller communication
- encoder and sensor reading
- safety I/O
- battery monitoring
- low-level communication bridges
- board-specific configurations
- flashing and bringup documentation

## Repository Structure

```text
openamr-platform-fw/
├── boards/
├── firmware/
├── configs/
├── docs/
├── tests/
└── tools/
```

## Documentation

Firmware documentation lives in [`docs/`](docs/):

- Architecture — [control loop & motor control](docs/architecture/control-loop.md),
  [debug & tuning interface](docs/architecture/debug-telemetry.md),
  [encoder ripple calibration](docs/architecture/encoder-calibration.md)
- [micro-ROS bringup](docs/bringup/micro-ros-bringup.md)
- [build & flash (Teensy 4.0)](docs/flashing/build-and-flash.md)
- [motion safety](docs/safety/motion-safety.md)
- [troubleshooting](docs/troubleshooting/common-issues.md)

The current firmware is a [Teensy 4.0 linorobot2 overlay](boards/teensy_4_0/linorobot2_overlay/README.md).

## Repository Boundaries

Firmware belongs in this repository.

ROS 2 software belongs in:

```text
openamr-platform-sw
```

Hardware, CAD, BOM, and wiring belong in:

```text
openamr-platform-hw
```

Organization-wide documentation belongs in:

```text
openamrobot-docs
```

## Safety Notice

Firmware can directly affect motors, batteries, sensors, and safety systems.

Users are responsible for validating:

- motor behavior
- emergency stop behavior
- battery safety
- sensor correctness
- communication reliability
- deployment suitability
- regulatory compliance

## License

MIT License.