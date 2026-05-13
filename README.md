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