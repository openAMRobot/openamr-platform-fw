# micro-ROS bringup

*Applies to the Teensy 4.0 `linorobot2_overlay` firmware.*

The firmware is a **micro-ROS** application. It connects to a **micro-ROS agent** on the host over
USB serial and exposes the robot's topics. This page covers the connection, the topic contract, and
the LED status codes.

## Transport & agent

- **USB serial** at **`BAUDRATE = 115200`** — this must match the agent exactly.
- Start the agent on the host, pointing at the Teensy's serial device:
  ```bash
  ros2 run micro_ros_agent micro_ros_agent serial -b 115200 -D <teensy-serial-by-id>
  ```
  Use the stable `/dev/serial/by-id/usb-Teensyduino_USB_Serial_*-if00` path (not `/dev/ttyACM*`,
  which can renumber).
- The firmware also has an (unused here) WiFi transport option (`USE_WIFI_TRANSPORT`), disabled in
  this config.

> ⚠️ **DDS must match the rest of the stack.** On this robot the host uses CycloneDDS on
> `ROS_DOMAIN_ID=0`. A host defaulting to a different RMW/domain will not see the topics. The agent
> may log harmless `Failed to parse type hash ... USER_DATA (null)` warnings — micro-ROS does not
> populate type hashes; they are not errors.

## Connection state machine

The firmware pings the agent and manages entities automatically:

`WAITING_AGENT` → `AGENT_AVAILABLE` → `AGENT_CONNECTED` → (`AGENT_DISCONNECTED`) → `WAITING_AGENT`.

On disconnect it calls `fullStop()` and destroys its ROS entities, then re-creates them when the
agent returns. Time is synchronised with the agent on connect so stamps are in ROS time.

## Topic contract

| Topic | Direction | Type | QoS |
|---|---|---|---|
| `/cmd_vel` | in | `geometry_msgs/Twist` | reliable |
| `/odom/unfiltered` | out | `nav_msgs/Odometry` | reliable |
| `/imu/data_raw` | out | `sensor_msgs/Imu` | reliable |
| `/imu/mag` | out | `sensor_msgs/MagneticField` | reliable |
| `/debug/left`, `/debug/right`, `/debug/pwm` | out | `geometry_msgs/Vector3` | **best-effort** |
| `/debug/openloop` | in | `geometry_msgs/Vector3` | reliable |
| `/debug/tune` | in | `geometry_msgs/Twist` | reliable |
| `/debug/enc_cal` | in | `std_msgs/Float32MultiArray` | reliable |

- The firmware publishes **raw** IMU (`/imu/data_raw` + `/imu/mag`); the host Madgwick/EKF pipeline
  fuses them into the filtered `/imu/data` and `/odom`.
- The `/debug/*` topics are covered in detail in
  [debug telemetry](../architecture/debug-telemetry.md).

## IMU note

The config enables `USE_MPU9250_IMU`, but the physical chip is an **MPU-6500** (`WHO_AM_I 0x70`).
The MPU9250 driver recognises it; the MPU6050 driver rejects it. IMU init failure is fatal (see LED
codes below).

## LED status codes (pin 13)

| Blink pattern | Meaning |
|---|---|
| Solid on | agent connected / idle (also toggles on each `/cmd_vel`) |
| Toggling with control | actively driving in closed loop |
| 2 blinks (loop) | fatal RCL error (`rclErrorLoop`) |
| 3 blinks | IMU init failed |
| 4 blinks | magnetometer init failed |

After bringup, if you use the encoder ripple table, run the host alignment once per Teensy
power-cycle — see [encoder calibration](../architecture/encoder-calibration.md).
