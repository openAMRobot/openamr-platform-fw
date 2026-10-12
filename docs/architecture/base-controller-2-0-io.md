# Base controller 2.0 I/O

*Applies to the OpenAMRobot 2.0 base controller, the STM32H723ZG (Gate B). The firmware in this
repository today is the Gate A unit (Teensy 4.0 with the MPU6500); see the
[overlay README](../../boards/teensy_4_0/linorobot2_overlay/README.md).*

This page records the decided 2.0 I/O as a reference for the STM32H723ZG port. It lists
requirements, not pins.

Source: P-03 Decision Addendum rev19.0 items 14 and 19 (12 October 2026), BOM Issue 7.7.
Register entries: `BASE-CONTROLLER-IO`, `BASE-CONTROLLER-GATES` and `IMU-TOPIC-OWNERSHIP` in
[decisions.yaml](https://github.com/openAMRobot/.github/blob/main/decisions.yaml).

## Near-field sensors

| Sensor | Count | Position | Interface |
|---|---|---|---|
| MaxBotix MB7060 | 4 | front and rear panel edge holes | one dedicated UART each, 9600 8N1 with receive inversion; one trigger GPIO each for sequential triggering |
| Benewake TF-Luna (cliff) | 2 | front and rear centre holes | one UART each; pin 5 tied to 3.3 V |

Totals: six sensor UARTs plus the debug UART, and four trigger GPIOs. No sensor I2C leaves the
controller board.

## IMU

- ICM-42688-P (MIKROE-4237) on a dedicated SPI, Mode 0, at most 8 MHz.
- Data-ready interrupt with DMA.
- WHO_AM_I `0x47` checked at boot.
- Publishes `/imu/data_raw`; the host filter owns `/imu/data`.
- Mounted on the centreline at x 0.195 m from `base_link`.

## CAN

| Peripheral | Bus | Use |
|---|---|---|
| FDCAN1 | CAN1 | traction (ZLAC8015D) |
| FDCAN2 | CAN2 | isolated BMS (Daly via CTL-014) |
| FDCAN3 | CAN3 | upper-body auxiliary actuators and the lift interface (CANopen, no safety function) |

## Host link

- Ethernet to the Jetson, micro-ROS over UDP.
- USB for the bench only.

## Pin allocation

Pin allocation is owned by Evans (I8 work package). This page lists requirements, not pins.
