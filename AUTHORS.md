# Authors and Contributors

This firmware is an **overlay on the linorobot2 firmware** — it is a **derivative work**, not a
from-scratch project. Credit therefore goes **both** to the upstream linorobot project (which wrote
the base firmware) **and** to the OpenAMRobot contributors (who adapted and tuned it for this robot).

## Maintainer

- OpenAMRobot organization — contact: botshare.ai@gmail.com

## Upstream — linorobot2 (the base firmware this repository overlays)

- **Juan Miguel Jimeno** and the **linorobot / linorobot2 project**
  — <https://github.com/linorobot/linorobot2>
  - Authors of the **base Teensy firmware** this repository is built on: the micro-ROS node and its
    lifecycle, the differential-drive control loop, the encoder / IMU driver integration, and the
    `firmware.ino`, `pid.cpp`, and `lino_base_config.h` files we overlay.
  - **Copyright (c) 2021 Juan Miguel Jimeno**, licensed **Apache-2.0**. The files we derive from
    linorobot2 **remain Apache-2.0**. See [`LICENSE`](LICENSE), [`LICENSE-Apache-2.0`](LICENSE-Apache-2.0),
    and [`NOTICE.md`](NOTICE.md). The linorobot name is used only to describe origin, not to imply
    endorsement.

## OpenAMRobot contributors

Recognition is given to contributors whose work has materially shaped this repository. Listing here
complements GitHub history — it makes non-trivial contributions easy to find for new readers.

### Firmware overlay, control loop, and documentation

- **Matthieu Vinet** — [@SHuttooo](https://github.com/SHuttooo)
  - **Teensy 4.0 `linorobot2_overlay`** brought to the firmware deployed on the real robot:
    - **Feedforward + dither** on top of the PID, and **back-calculation anti-windup** on the integral.
    - **Runtime encoder-ripple correction table** (`/debug/enc_cal`) — hot-loaded and phase-aligned per
      boot to flatten the AS5040 eccentricity ripple (~±40 % → ~±4 %).
    - **`/debug` telemetry + tuning contract**: `/debug/left`, `/debug/right`, `/debug/pwm`,
      and the `/debug/openloop`, `/debug/tune`, `/debug/enc_cal` command topics; bounded open-loop test
      mode (`ENABLE_POWERED_DEBUG`).
    - **MPU6500 IMU workaround** (driven through the MPU9250 driver via the WHO_AM_I check).
    - Real-robot **PID / motor tuning** (`K_P 2.0 / K_I 0.1 / K_D 0.1`, `MOTOR2_GAIN`) and the measured
      drivetrain constants in `config/lino_base_config.h`.
  - **Firmware documentation set**: control loop, `/debug` telemetry, encoder ripple calibration,
    micro-ROS bring-up, build & flash, motion safety, troubleshooting.
  - **Licensing/attribution**: set up the MIT + Apache-2.0 dual-licensing and the `NOTICE`.

  > **License note on these changes:** the control-loop changes above live *inside* the linorobot2
  > files (`firmware.ino`, `pid.cpp`, `lino_base_config.h`), so as modifications of Apache-2.0 source
  > they are contributed **under Apache-2.0** (with the required "changed by" notice). Only the
  > **standalone documentation** authored here is MIT. See `NOTICE.md` for the per-file breakdown.

### Repository scaffolding

- **Alex** ([OpenAMRobot maintainer](mailto:botshare.ai@gmail.com))
  - Initial `openamr-platform-fw` repository structure and governance scaffolding.
