# Base controller 2.0 I/O

*Applies to the OpenAMRobot 2.0 base controller, the STM32H723ZG (Gate B). The firmware in this
repository today is the Gate A unit (Teensy 4.0 with the MPU6500); see the
[overlay README](../../boards/teensy_4_0/linorobot2_overlay/README.md).*

This page records the decided 2.0 I/O as a reference for the STM32H723ZG port. It lists
requirements, not pins.

Source: P-03 rev19.6 items 14, 19 and 22; OpenAMRobot 2.0 Detailed Hardware BOM Issue 7.10.
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
- `imu_link` is at (0.0544, 0, 0.1210) m from `base_link`, yaw -90 deg (rpy 0, 0, -pi/2), on the electronics bracket MMP.04.00.00.002 (P-03 rev19.6 item 19, D5 update of 16 October). The firmware publishes raw sensor-frame values; the axis mapping is in the URDF, with no firmware remap.

## Safety and charging I/O (P-03 item 22)

- PNOZ s3 Y32 enabled status is a controller input through one complete ISO1212 isolated input module; channel 2 is spare.
- The base E-stop spare NO contact is a direct controller diagnostic input. It reports that button only. On the Conestoga build, the front and back E-stops are monitored on separate controller pins.
- RESET NC and the charger fault open-collector signal are direct controller inputs with 3.3 V pull-ups and series resistors.
- RESET lamp and KCHG coil are controller outputs through one TI TPL7407L low-side driver supplied from the regulated 24 V rail.
- Separate K1_CLOSED and K2_CLOSED firmware feedback is removed. Each contactor's NC auxiliary is in the PNOZ start circuit.
- GPIO assignments remain in the pin map and are not assigned on this page.

References: the NUCLEO-H723ZG pin map (team document, owner Evans Kwashie) and the [platform-hw datasheet folders](https://github.com/openAMRobot/openamr-platform-hw/tree/main/datasheets).

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
