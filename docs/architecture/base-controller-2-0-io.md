# Base controller 2.0 I/O

*Applies to the OpenAMRobot 2.0 base controller, the STM32H723ZG (Gate B). The firmware in this
repository today is the Gate A unit (Teensy 4.0 with the MPU6500); see the
[overlay README](../../boards/teensy_4_0/linorobot2_overlay/README.md).*

This page records the decided 2.0 I/O as a reference for the STM32H723ZG port. It lists
requirements. The few pins it names (IMU on SPI4, CAN2 on FDCAN2) are from the controller pin
map proposal and are recorded here for reference only; pin allocation stays with the I8 work
package.

Source: P-03 rev20.2; OpenAMRobot 2.0 Detailed Hardware BOM Issue 7.14.
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

- ICM-42688-P on the TDK InvenSense EV_ICM-42688-P evaluation board, on a dedicated SPI:
  SPI4, Mode 0, at most 8 MHz, on PE11 to PE14 (unchanged).
- Wired directly from the controller to header CN1 of the board; no click shield (harness
  CTL-012):

  | CN1 pin | Signal | Controller side |
  |---|---|---|
  | 16 | SCLK | SPI4 SCK |
  | 18 | SDI | SPI4 MOSI |
  | 20 | SDO | SPI4 MISO |
  | 4 | CS | SPI4 chip select (GPIO) |
  | 3 | INT1 | data-ready input; GPIO not assigned yet |
  | 11, 13 | GND | GND |
  | 19 | VIN | 5 V |

- Jumpers: JP2 1-2 (VDD 3.0 V), JP1 1-2 (VDDIO = VDD). The IMU I/O level is therefore 3.0 V.
- Data-ready interrupt with DMA.
- WHO_AM_I `0x47` checked at boot.
- Publishes `/imu/data_raw`; the host filter owns `/imu/data`.
- Mounted on the centreline at x 0.195 m from `base_link`.

## CAN

| Peripheral | Bus | Use |
|---|---|---|
| FDCAN1 | CAN1 | traction (ZLAC8015D) |
| FDCAN2 | CAN2 | isolated BMS (Daly via CTL-014); CAN isolator on PB12 (RX) and PB6 (TX) |
| FDCAN3 | CAN3 | upper-body auxiliary actuators and the lift interface (CANopen, no safety function) |

No click shield: the IMU and the CAN2 isolator are wired directly to SPI4 and FDCAN2 through
harness CTL-012.

## Host link

- Ethernet to the Jetson, micro-ROS over UDP.
- USB for the bench only.

## Pin allocation

Pin allocation is owned by Evans (I8 work package). This page lists requirements; the pins
named above are reference copies of the pin map proposal, not an allocation.
