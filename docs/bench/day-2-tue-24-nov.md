# Day 2, Tuesday 24 November: CAN1 up, drive alive, 2000h saved; IMU

Procedures: 03 steps 1 to 7, then 05. The drive is powered but **nothing turns today**. Owner: Arka, Evans on call.

## Procedure 03 steps 1 to 7 (ZLAC8015D, CAN1)
- [ ] Wheel diameter measured and written (`TODO(Evans)` value, with how it was measured).
- [ ] Bench supply: voltage inside 25.6 to 29.2 V, current limit set, inrush method agreed with Evans before power-on.
- [ ] Step 1 termination (supply off): 55 to 65 ohm. Do not power a wrong reading.
- [ ] Step 2 `sudo tools/bench/can_up.sh can1 500000`, `bitrate 500000`.
- [ ] Step 3 `nmt_scan.py can1 --expect 1 --expect-period-ms 100` PASS.
- [ ] Step 4 `zlac_read_faults.py can1` PASS; the starting 2000h value recorded.
- [ ] Step 5 `zlac_set_link_loss.py can1 --timeout-ms 250`, read back equal, then `--save-confirmed`; power cycle, `2000h = 250` still there.
- [ ] Step 6 `busload.sh can1 500000 30`, idle peak recorded.
- [ ] Step 7 two-person checkpoint written, ready for tomorrow. Do not run step 8 today.

## Procedure 05: IMU (no motion)
- [ ] Jumpers JP1 and JP2 checked with a meter; VIN about 5 V, VDDIO about 3.0 V.
- [ ] `s_imu_result` = OK, WHO_AM_I = 0x47, `configured` = 1.
- [ ] Samples about 200 Hz, 1 g on a still board, signs on the six poses, 5 minute stability.
- [ ] Pulled MISO wire shows health not healthy and recovery.

## End of day
- [ ] All `candump -L` logs saved. Supply off. Open values sent to Evans.

Stop if: a wheel moves, a supply folds back unexplained, VDDIO above 3.3 V.
