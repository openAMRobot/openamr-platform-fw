# micro-ROS bringup

*Applies to the Teensy 4.0 `linorobot2_overlay` firmware.*

The firmware is a **micro-ROS** application. It connects to a **micro-ROS agent** on the host over
USB serial and exposes the robot's topics. This page covers the connection, the topic contract, and
the LED status codes.

## Transport & agent

The micro-ROS transport topology is shown below.

> 📐 **[Diagram: micro-ROS node topology]** — *placeholder; not generated yet (prompt in the page source).*

<!-- DIAGRAM PLACEHOLDER (micro-ros-node-topology) — TO PLACE THE DIAGRAM, replace the blockquote line
above AND this whole comment with a single image line:
    ![Figure - how the Teensy connects to the ROS 2 graph through the micro-ROS agent.](diagrams/micro-ros-node-topology.svg)

Generation prompt (paste to Claude):
Draw a node/transport topology diagram:
- Teensy 4.0 runs a micro-ROS CLIENT (the firmware node) over USB serial (115200).
- Host (Raspberry Pi 5) runs the micro-ROS AGENT (micro_ros_agent serial -dev /dev/ttyACM0).
- The agent bridges the Teensy into the ROS 2 graph (DDS = CycloneDDS).
- Show the topics crossing the bridge: IN to Teensy = /cmd_vel, /debug/openloop, /debug/tune, /debug/enc_cal; OUT from Teensy = /odom/unfiltered, /imu/data_raw, /imu/mag, /debug/left, /debug/right, /debug/pwm.
- On the host side, show the EKF/Madgwick consuming /imu/data_raw + /odom to produce filtered /imu/data + /odom, feeding Nav2.
Note the connection state machine: control loop only runs while the agent is connected; on disconnect the firmware fullStop()s.

STYLE (keep ALL diagrams uniform): solid WHITE background — add a full-canvas white
rectangle as the first element. Flat, clean, technical look; dark text (#1a1a1a),
sans-serif. Use explicit hex colours ONLY — do NOT use CSS variables (CSS variables).
Shared palette across every diagram: 24 V / power = red #c0392b; 5 V = orange #e67e22;
3.3 V logic = blue #2c6fbb; data buses = grey #888888; warning / 'NOT FITTED' / danger
= red; wired / OK = green #2e8b57. Rounded-rectangle blocks, labelled arrows for
direction, English labels only, landscape orientation, no text overflow.
-->


- **USB serial** at **`BAUDRATE = 115200`** — this must match the agent exactly.
- Start the agent on the host, pointing at the Teensy's serial device. A non-interactive shell
  does **not** source ROS, so source it and set the matching RMW/domain first (see the DDS note
  below):
  ```bash
  source /opt/ros/jazzy/setup.bash
  source ~/linorobot2_ws/install/setup.bash
  export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
  export ROS_DOMAIN_ID=0
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
| `/imu/mag` | out | `sensor_msgs/MagneticField` | reliable | see note |
| `/debug/left`, `/debug/right`, `/debug/pwm` | out | `geometry_msgs/Vector3` | **best-effort** |
| `/debug/openloop` | in | `geometry_msgs/Vector3` | reliable |
| `/debug/tune` | in | `geometry_msgs/Twist` | reliable |
| `/debug/enc_cal` | in | `std_msgs/Float32MultiArray` | reliable |

- The firmware publishes **raw** IMU (`/imu/data_raw` + `/imu/mag`); the host Madgwick/EKF pipeline
  fuses them into the filtered `/imu/data` and `/odom`.
- ⚠️ **No real magnetometer.** The board carries an **MPU6500**, driven through the MPU9250 driver
  (WHO_AM_I workaround). The MPU6500 has **no magnetometer**, so `/imu/mag` is published for
  message-shape compatibility but carries **no meaningful magnetic field** — do not fuse it as a
  heading source. The host EKF should use the gyro/accel only (yaw from the gyro rate).
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
| 3 blinks | IMU init failed — **also** any `createEntities()` RCL failure (the non-syslog `RCCHECK` flashes 3 then retries) |
| 4 blinks | magnetometer init failed |

After bringup, if you use the encoder ripple table, run the host alignment once per Teensy
power-cycle — see [encoder calibration](../architecture/encoder-calibration.md).
