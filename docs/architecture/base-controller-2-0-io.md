# Base controller 2.0 I/O

*Applies to the OpenAMRobot 2.0 base controller, the STM32H723ZG (Gate B). The firmware in this
repository today is the Gate A unit (Teensy 4.0 with the MPU6500); see the
[overlay README](../../boards/teensy_4_0/linorobot2_overlay/README.md).*

This page records the decided 2.0 I/O as a reference for the STM32H723ZG port. It lists
requirements. The few pins it names (IMU on SPI4, CAN2 on FDCAN2) are from the controller pin
map proposal and are recorded here for reference only; pin allocation stays with the I8 work
package.

Source: P-03 rev20.6 (items 14, 19, 22, 26, 29 and 30); E-01 Safety and Power Distribution
Baseline rev1.8; OpenAMRobot 2.0 Detailed Hardware BOM Issue 7.14.
Register entries: `BASE-CONTROLLER-IO`, `BASE-CONTROLLER-GATES`, `IMU-TOPIC-OWNERSHIP` and
`FRAMES-REP105` in [decisions.yaml](https://github.com/openAMRobot/.github/blob/main/decisions.yaml).

## Near-field sensors

| Sensor | Count | Position | Interface |
|---|---|---|---|
| MaxBotix MB7060 | 4 | front and rear panel edge holes | one dedicated UART each, 9600 8N1 with receive inversion; one trigger GPIO each for sequential triggering |
| Benewake TF-Luna (cliff) | 2 | front and rear centre holes | one UART each; pin 5 tied to 3.3 V |

Totals: six sensor UARTs plus the debug UART, and four trigger GPIOs. No sensor I2C leaves the
controller board.

## IMU

- ICM-42688-P on the TDK InvenSense EV_ICM-42688-P evaluation board, on a dedicated SPI:
  SPI4, Mode 0, at most 8 MHz. SCK PE12, MISO PE13, MOSI PE14; chip select PE11 as a
  software GPIO (pins unchanged).
- Wired directly from the controller to header CN1 of the board; no click shield (jumper wires
  on the bench, harness CTL-012 on the robot):

  | CN1 pin | Signal | Controller side |
  |---|---|---|
  | 16 | SCLK | SPI4 SCK (PE12) |
  | 18 | SDI | SPI4 MOSI (PE14) |
  | 20 | SDO | SPI4 MISO (PE13) |
  | 4 | CS | chip select GPIO (PE11) |
  | 3 | INT1 | data-ready input; EXTI GPIO not assigned yet |
  | 11, 13 | GND | GND |
  | 19 | VIN | 5 V |

- Jumpers: JP2 1-2 (VDD 3.0 V), JP1 1-2 (VDDIO = VDD). The IMU I/O level is therefore 3.0 V;
  the 3.3 V controller outputs stay within the VDDIO + 0.5 V input rating.
- Data-ready interrupt with DMA.
- WHO_AM_I `0x47` checked at boot.
- Publishes `/imu/data_raw` in frame `imu_link`, with the real gyro covariance and the timestamp
  taken at the data-ready interrupt, plus `/imu/temperature`. The EKF reads `/imu/data_raw`
  directly (angular velocity z only, no filter node); `/imu/data` stays reserved for a host
  orientation filter if one is added.
- `imu_link` is at (0.0544, 0, 0.1210) m from `base_link`, yaw -90 deg (rpy 0, 0, -pi/2), on the
  centreline on the electronics bracket MMP.04.00.00.002, chip +Y axis forward. The firmware
  publishes raw sensor-frame values; the axis mapping is in the URDF, with no firmware remap.
  The orientation is checked on the delivered board.

## Safety and charging I/O

These signals are diagnostics, not safety. The hardwired chain (Pilz PNOZ s3 and the K1 and K2
contactors) removes actuator power on its own; see [motion safety](../safety/motion-safety.md).

| Signal | Direction | Interface |
|---|---|---|
| SAFETY_OK (PNOZ s3 Y32) | input | one complete ISO1212 isolated input module; channel 2 spare |
| Base E-stop spare NO | input | direct GPIO, 3.3 V pull-up and series resistor; reports the base button only |
| RESET button NC | input | direct GPIO, 3.3 V pull-up and series resistor |
| Charger fault (TZBOT, open collector) | input | direct GPIO, 3.3 V pull-up and series resistor |
| RESET lamp | output | TI TPL7407LDR low-side driver on an Adafruit 1207 SOIC-16 breakout, supplied from the regulated 24 V rail |
| Charge relay KCHG coil | output | same TPL7407LDR driver |

- Both driver outputs are off from power-up until the firmware enables them.
- The second E-stop sits on top of the moving adapter plate. Its spare NO goes to the upper-body
  controller CTL-017 as a diagnostic input and is reported over CAN3; it is not wired to this
  controller.
- There is no separate K1 or K2 feedback input: each contactor's NC auxiliary sits in the PNOZ
  start circuit.
- The use of the spare driver channels is open and decided by the electrical safety owner
  (P-03 item 30 A4).
- GPIO assignments are in the pin map; none is assigned on this page.

## CAN

| Peripheral | Bus | Use |
|---|---|---|
| FDCAN1 | CAN1 | traction (ZLAC8015D) |
| FDCAN2 | CAN2 | isolated BMS (Daly via CTL-014); CAN isolator on PB12 (RX) and PB6 (TX) |
| FDCAN3 | CAN3 | upper-body controller CTL-017, a separate board (node 0x10, 500 kbit/s; CANopen, no safety function) |

No click shield: the IMU and the CAN2 isolator are wired directly to SPI4 and FDCAN2 (jumper
wires on the bench, harness CTL-012 on the robot).

## Host link

- Ethernet to the Jetson, micro-ROS over UDP.
- USB for the bench only.

## Pin allocation

Pin allocation is owned by the I8 work package (electrical and firmware owner). This page lists
requirements; the pins named above are reference copies of the pin map proposal, not an
allocation.
