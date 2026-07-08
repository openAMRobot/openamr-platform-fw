#!/usr/bin/env bash
# Calibrate the encoder ripple table for the CURRENT boot and load it into the firmware (NO reflash).
# Run once after each Teensy power-on. The wheels must be free to spin (lift the robot).
#
# Why per power-on: the encoder is incremental — its count resets to 0 at a random wheel angle at every
# boot, so the table's phase only matches the boot it was calibrated in. No reboot between calib and
# apply = phase stays correct. Restarting only the PC-side ROS (Teensy still powered) does NOT need this.
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"

source /opt/ros/jazzy/setup.bash
# Optional: source your ROS 2 overlay workspace if you have one. Not required — these scripts only
# use core std_msgs/geometry_msgs. Set OPENAMR_WS to the workspace install dir to source it.
[ -n "$OPENAMR_WS" ] && source "$OPENAMR_WS/setup.bash" 2>/dev/null || true
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
export ROS_DOMAIN_ID=0

PWMS="${1:-150,200,250}"
echo ">>> 1/2 calibration (open-loop spin at PWM $PWMS) — keep the wheels free..."
python3 "$DIR/encoder_calib.py" --arm "$PWMS"
echo ">>> 2/2 sending the table to the firmware (instant, no reflash)..."
python3 "$DIR/apply_enc_cal.py" /tmp/encoder_calib.json
echo ">>> done. The ripple correction is active until the Teensy is power-cycled."
