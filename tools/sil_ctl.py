#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Send commands to the SIL base controller's test control socket.

    tools/sil_ctl.py arm                 PNOZ cycle + clear software stop (needs the override)
    tools/sil_ctl.py state
    tools/sil_ctl.py "sw_stop 1" "imu invalid 1" ...   raw commands (boards/host/src/host_ctl.c)
"""
import os
import socket
import sys
import time

PATH = os.environ.get("SIL_CTL", "/tmp/oamr_base.ctl")


def send(s, cmd):
    s.sendall((cmd + "\n").encode())
    reply = s.recv(1024).decode().strip()
    print("%s -> %s" % (cmd, reply))
    return reply


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
        s.connect(PATH)
        for cmd in argv:
            if cmd == "arm":
                for c in ("column released", "estop 0", "safety_ok 0"):
                    if send(s, c).startswith("error"):
                        return 1
                time.sleep(0.4)
                send(s, "reset 1")
                time.sleep(0.25)
                send(s, "reset 0")
                send(s, "safety_ok 1")
                time.sleep(0.1)
                send(s, "sw_stop 0")
                time.sleep(0.1)
                send(s, "state")
            else:
                send(s, cmd)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
