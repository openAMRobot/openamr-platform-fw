# Bench scripts for CAN and watchdog tests (gate G4)

Scripts for real hardware: CAN and watchdog bench without load. Each file starts with a header that
says what it proves and the pass criterion. They are tested against the emulators in `tools/can_sil`
on `vcan` before they are used on hardware.

| File | Sends motion? | Proves |
|---|---|---|
| `can_up.sh` | no | bitrate, restart-ms 0 during bring-up, txqueuelen 1000 |
| `termination_check.md` | no | about 60 ohm per bus, power off |
| `nmt_scan.py` | no (SDO reads only) | expected nodes present, heartbeat period |
| `zlac_read_faults.py` | no (read only) | no active fault, 2000h value |
| `zlac_set_link_loss.py` | no | 2000h written, read back, saved only with `--save-confirmed` |
| `zlac_spin.py` | **yes** | drive turns at the commanded capped speed |
| `zlac_link_loss_test.md` | uses `zlac_spin.py` | the wheels stop by themselves after CAN1 is unplugged |
| `busload.sh` | no | bus load below the limit |

## Order on the bench

1. `termination_check.md` for each bus, power off.
2. `sudo tools/bench/can_up.sh can1 500000` (CAN2 250000, CAN3 500000).
3. `python3 tools/bench/nmt_scan.py can1 --expect 1 --expect-period-ms 100`
4. `python3 tools/bench/zlac_read_faults.py can1`
5. `python3 tools/bench/zlac_set_link_loss.py can1 --timeout-ms 250` then again with `--save-confirmed` once the read-back is right.
6. `tools/bench/busload.sh can1 500000 30`
7. `zlac_link_loss_test.md`, wheels up.

## Motion rules

- Every script that can turn a wheel refuses to run without `--wheels-up-confirmed` and `--wheel-diameter-m`.
- Commanded speed up to 0.1 m/s is used as given; 0.1 to 0.2 m/s is capped to 0.1 m/s with a
  warning; above 0.2 m/s the script refuses (exit 2, nothing sent) unless `--allow-high-speed`
  is given, and never above the 0.5 m/s ceiling.
- The hardwired E-stop is the stop. These scripts are not a safety function.

## Setup

```
python3 -m pip install python-can
sudo apt-get install can-utils   # canbusload, candump
```

`MAX_LOAD_PERCENT` in `busload.sh`, the stop-time limit in `zlac_link_loss_test.md` and the
wheel diameter are open parameters, `TODO(Evans)`.
