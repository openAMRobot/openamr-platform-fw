# CTL-017 object table, snapshot for the drift test

Verbatim copy of the generated object table (section 10) of `docs/ctl017_object_dictionary.md`
in openamr-upperbody-fw PR #15, head commit 1a68c50c71902d1d9d7903e7bb47b6b6efa4d9fc (proposal
v1). It is the only input of `tests/test_ctl017_od_drift.py`. When the owner adopts or changes
the dictionary, replace the table below with the new one, run `gen_ctl017_od.py` on the new
document and fix the emulator until the drift test passes. Do not edit the rows by hand.
Set `CTL017_OD_DOC` to the path of the live document to compare the snapshot with it as well.

<!-- BEGIN GENERATED OD TABLE (tools/ctl017_od/gen_od_docs.py) -->

| Index | Sub | Name | Type | Access | PDO | Default |
|---|---|---|---|---|---|---|
| 0x1000 | 0 | Device type | UNSIGNED32 | const |  | 0x00000000 |
| 0x1001 | 0 | Error register | UNSIGNED8 | ro |  | 0 |
| 0x1003 | 0 | Pre-defined error field: number of errors | UNSIGNED8 | rw |  | 0 |
| 0x1003 | 1 | Standard error field 1 | UNSIGNED32 | ro |  | 0x00000000 |
| 0x1003 | 2 | Standard error field 2 | UNSIGNED32 | ro |  | 0x00000000 |
| 0x1003 | 3 | Standard error field 3 | UNSIGNED32 | ro |  | 0x00000000 |
| 0x1003 | 4 | Standard error field 4 | UNSIGNED32 | ro |  | 0x00000000 |
| 0x1003 | 5 | Standard error field 5 | UNSIGNED32 | ro |  | 0x00000000 |
| 0x1003 | 6 | Standard error field 6 | UNSIGNED32 | ro |  | 0x00000000 |
| 0x1003 | 7 | Standard error field 7 | UNSIGNED32 | ro |  | 0x00000000 |
| 0x1003 | 8 | Standard error field 8 | UNSIGNED32 | ro |  | 0x00000000 |
| 0x1014 | 0 | COB-ID EMCY | UNSIGNED32 | const |  | 0x00000090 |
| 0x1016 | 0 | Consumer heartbeat time: highest sub-index | UNSIGNED8 | const |  | 1 |
| 0x1016 | 1 | Consumer heartbeat time 1 (bits 16-23 node-ID, bits 0-15 ms) | UNSIGNED32 | rw |  | 0x0001012C |
| 0x1017 | 0 | Producer heartbeat time (ms) | UNSIGNED16 | rw |  | 0x0064 |
| 0x1018 | 0 | Identity: highest sub-index | UNSIGNED8 | const |  | 4 |
| 0x1018 | 1 | Vendor-ID | UNSIGNED32 | const |  | 0x00000000 |
| 0x1018 | 2 | Product code | UNSIGNED32 | const |  | 0x00000011 |
| 0x1018 | 3 | Revision number | UNSIGNED32 | const |  | 0x00010000 |
| 0x1018 | 4 | Serial number | UNSIGNED32 | ro |  | 0x00000000 |
| 0x1400 | 0 | RPDO1 comm: highest sub-index | UNSIGNED8 | const |  | 5 |
| 0x1400 | 1 | RPDO1 COB-ID | UNSIGNED32 | const |  | 0x00000210 |
| 0x1400 | 2 | RPDO1 transmission type | UNSIGNED8 | const |  | 254 |
| 0x1400 | 5 | RPDO1 event timer: command deadline (ms) | UNSIGNED16 | rw (PRE-OP only) |  | 0x012C |
| 0x1401 | 0 | RPDO2 comm: highest sub-index | UNSIGNED8 | const |  | 5 |
| 0x1401 | 1 | RPDO2 COB-ID | UNSIGNED32 | const |  | 0x00000310 |
| 0x1401 | 2 | RPDO2 transmission type | UNSIGNED8 | const |  | 254 |
| 0x1401 | 5 | RPDO2 event timer (ms, 0 = no deadline) | UNSIGNED16 | const |  | 0x0000 |
| 0x1600 | 0 | RPDO1 mapping: number of entries | UNSIGNED8 | const |  | 3 |
| 0x1600 | 1 | RPDO1 mapping 1 | UNSIGNED32 | const |  | 0x20100108 |
| 0x1600 | 2 | RPDO1 mapping 2 | UNSIGNED32 | const |  | 0x20100208 |
| 0x1600 | 3 | RPDO1 mapping 3 | UNSIGNED32 | const |  | 0x20100308 |
| 0x1601 | 0 | RPDO2 mapping: number of entries | UNSIGNED8 | const |  | 3 |
| 0x1601 | 1 | RPDO2 mapping 1 | UNSIGNED32 | const |  | 0x20110108 |
| 0x1601 | 2 | RPDO2 mapping 2 | UNSIGNED32 | const |  | 0x20110208 |
| 0x1601 | 3 | RPDO2 mapping 3 | UNSIGNED32 | const |  | 0x20110308 |
| 0x1800 | 0 | TPDO1 comm: highest sub-index | UNSIGNED8 | const |  | 5 |
| 0x1800 | 1 | TPDO1 COB-ID | UNSIGNED32 | const |  | 0x40000190 |
| 0x1800 | 2 | TPDO1 transmission type | UNSIGNED8 | const |  | 254 |
| 0x1800 | 3 | TPDO1 inhibit time (100 us) | UNSIGNED16 | rw (PRE-OP only) |  | 0x0064 |
| 0x1800 | 5 | TPDO1 event timer (ms) | UNSIGNED16 | rw |  | 0x0064 |
| 0x1801 | 0 | TPDO2 comm: highest sub-index | UNSIGNED8 | const |  | 5 |
| 0x1801 | 1 | TPDO2 COB-ID | UNSIGNED32 | const |  | 0x40000290 |
| 0x1801 | 2 | TPDO2 transmission type | UNSIGNED8 | const |  | 254 |
| 0x1801 | 3 | TPDO2 inhibit time (100 us) | UNSIGNED16 | rw (PRE-OP only) |  | 0x0064 |
| 0x1801 | 5 | TPDO2 event timer (ms) | UNSIGNED16 | rw |  | 0x0064 |
| 0x1A00 | 0 | TPDO1 mapping: number of entries | UNSIGNED8 | const |  | 7 |
| 0x1A00 | 1 | TPDO1 mapping 1 | UNSIGNED32 | const |  | 0x20000108 |
| 0x1A00 | 2 | TPDO1 mapping 2 | UNSIGNED32 | const |  | 0x20000208 |
| 0x1A00 | 3 | TPDO1 mapping 3 | UNSIGNED32 | const |  | 0x20000310 |
| 0x1A00 | 4 | TPDO1 mapping 4 | UNSIGNED32 | const |  | 0x20000408 |
| 0x1A00 | 5 | TPDO1 mapping 5 | UNSIGNED32 | const |  | 0x20000508 |
| 0x1A00 | 6 | TPDO1 mapping 6 | UNSIGNED32 | const |  | 0x20000608 |
| 0x1A00 | 7 | TPDO1 mapping 7 | UNSIGNED32 | const |  | 0x20000708 |
| 0x1A01 | 0 | TPDO2 mapping: number of entries | UNSIGNED8 | const |  | 6 |
| 0x1A01 | 1 | TPDO2 mapping 1 | UNSIGNED32 | const |  | 0x20010108 |
| 0x1A01 | 2 | TPDO2 mapping 2 | UNSIGNED32 | const |  | 0x20010208 |
| 0x1A01 | 3 | TPDO2 mapping 3 | UNSIGNED32 | const |  | 0x20010310 |
| 0x1A01 | 4 | TPDO2 mapping 4 | UNSIGNED32 | const |  | 0x20010410 |
| 0x1A01 | 5 | TPDO2 mapping 5 | UNSIGNED32 | const |  | 0x20010508 |
| 0x1A01 | 6 | TPDO2 mapping 6 | UNSIGNED32 | const |  | 0x20010608 |
| 0x2000 | 0 | LiftStatus: highest sub-index | UNSIGNED8 | const |  | 7 |
| 0x2000 | 1 | state | UNSIGNED8 | ro | yes | 0 |
| 0x2000 | 2 | flags | UNSIGNED8 | ro | yes | 0 |
| 0x2000 | 3 | position (0.1 mm above lower end, 0xFFFF unknown) | UNSIGNED16 | ro | yes | 0x0000 |
| 0x2000 | 4 | active_stop_id (0xFF none) | UNSIGNED8 | ro | yes | 0 |
| 0x2000 | 5 | last_seq | UNSIGNED8 | ro | yes | 0 |
| 0x2000 | 6 | cmd_result (0xFF none since start-up) | UNSIGNED8 | ro | yes | 0 |
| 0x2000 | 7 | status_counter | UNSIGNED8 | ro | yes | 0 |
| 0x2001 | 0 | LiftStatus2: highest sub-index | UNSIGNED8 | const |  | 6 |
| 0x2001 | 1 | inputs (stop sensors, second E-stop, Hall supply, spares) | UNSIGNED8 | ro | yes | 0 |
| 0x2001 | 2 | target_stop_id (0xFF none) | UNSIGNED8 | ro | yes | 0 |
| 0x2001 | 3 | motor current (mA) | UNSIGNED16 | ro | yes | 0x0000 |
| 0x2001 | 4 | velocity (0.1 mm/s, positive up) | INTEGER16 | ro | yes | 0 |
| 0x2001 | 5 | faults F01..F08 (bit n-1 = Fn) | UNSIGNED8 | ro | yes | 0 |
| 0x2001 | 6 | aux flags (F09, head light on, Getac enable) | UNSIGNED8 | ro | yes | 0 |
| 0x2002 | 0 | Lift fault bitmask (bit n-1 = Fn) | UNSIGNED16 | ro |  | 0x0000 |
| 0x2010 | 0 | LiftCommand: highest sub-index | UNSIGNED8 | const |  | 3 |
| 0x2010 | 1 | seq | UNSIGNED8 | rww (RPDO only) | yes | 0 |
| 0x2010 | 2 | op (0 STOP, 1 MOVE_TO_STOP, 2 CLEAR_FAULT, 3 START_HOMING) | UNSIGNED8 | rww (RPDO only) | yes | 0 |
| 0x2010 | 3 | stop_id (0 L1000, 1 L1175, 2 L1350) | UNSIGNED8 | rww (RPDO only) | yes | 0 |
| 0x2011 | 0 | AuxCommand: highest sub-index | UNSIGNED8 | const |  | 3 |
| 0x2011 | 1 | head light on (0/1) | UNSIGNED8 | rw | yes | 0 |
| 0x2011 | 2 | head light dim level (0..100 %) | UNSIGNED8 | rw | yes | 100 |
| 0x2011 | 3 | Getac power or charging enable (0/1) | UNSIGNED8 | rw | yes | 0 |
| 0x2100 | 0 | Stop positions: highest sub-index | UNSIGNED8 | const |  | 3 |
| 0x2100 | 1 | stop 0 L1000 position (um above lower end) | INTEGER32 | rw (PRE-OP only) |  | 5000 |
| 0x2100 | 2 | stop 1 L1175 position (um above lower end) | INTEGER32 | rw (PRE-OP only) |  | 180000 |
| 0x2100 | 3 | stop 2 L1350 position (um above lower end) | INTEGER32 | rw (PRE-OP only) |  | 355000 |
| 0x2101 | 0 | Lift parameters: highest sub-index | UNSIGNED8 | const |  | 32 |
| 0x2101 | 1 | hall_nm_per_count | INTEGER32 | rw (PRE-OP only) |  | 209200 |
| 0x2101 | 2 | hall_count_up_positive | BOOLEAN | rw (PRE-OP only) |  | 1 |
| 0x2101 | 3 | stop_tol_um | INTEGER32 | rw (PRE-OP only) |  | 2000 |
| 0x2101 | 4 | xcheck_tol_um | INTEGER32 | rw (PRE-OP only) |  | 2000 |
| 0x2101 | 5 | sensor_edge_offset_up_um | INTEGER32 | rw (PRE-OP only) |  | 0 |
| 0x2101 | 6 | sensor_edge_offset_down_um | INTEGER32 | rw (PRE-OP only) |  | 0 |
| 0x2101 | 7 | stop_lead_um | INTEGER32 | rw (PRE-OP only) |  | 0 |
| 0x2101 | 8 | sensor_overrun_um | INTEGER32 | rw (PRE-OP only) |  | 3000 |
| 0x2101 | 9 | duty_move_permille | UNSIGNED16 | rw (PRE-OP only) |  | 0x03E8 |
| 0x2101 | 10 | duty_homing_permille | UNSIGNED16 | rw (PRE-OP only) |  | 0x01F4 |
| 0x2101 | 11 | move_speed_um_s | INTEGER32 | rw (PRE-OP only) |  | 18000 |
| 0x2101 | 12 | homing_speed_um_s | INTEGER32 | rw (PRE-OP only) |  | 9000 |
| 0x2101 | 13 | hall_window_ms | UNSIGNED16 | rw (PRE-OP only) |  | 0x00C8 |
| 0x2101 | 14 | hall_start_grace_ms | UNSIGNED16 | rw (PRE-OP only) |  | 0x012C |
| 0x2101 | 15 | move_rate_min_cps | UNSIGNED16 | rw (PRE-OP only) |  | 0x002B |
| 0x2101 | 16 | move_rate_max_cps | UNSIGNED16 | rw (PRE-OP only) |  | 0x0082 |
| 0x2101 | 17 | homing_rate_min_cps | UNSIGNED16 | rw (PRE-OP only) |  | 0x000F |
| 0x2101 | 18 | homing_rate_max_cps | UNSIGNED16 | rw (PRE-OP only) |  | 0x0056 |
| 0x2101 | 19 | stall_ms | UNSIGNED16 | rw (PRE-OP only) |  | 0x012C |
| 0x2101 | 20 | wrong_dir_counts | UNSIGNED16 | rw (PRE-OP only) |  | 0x000A |
| 0x2101 | 21 | travel_timeout_factor_pct | UNSIGNED16 | rw (PRE-OP only) |  | 0x0096 |
| 0x2101 | 22 | travel_timeout_margin_ms | UNSIGNED16 | rw (PRE-OP only) |  | 0x07D0 |
| 0x2101 | 23 | overcurrent_ma | UNSIGNED16 | rw (PRE-OP only) |  | 0x1B58 |
| 0x2101 | 24 | overcurrent_ms | UNSIGNED16 | rw (PRE-OP only) |  | 0x0064 |
| 0x2101 | 25 | inrush_blank_ms | UNSIGNED16 | rw (PRE-OP only) |  | 0x00C8 |
| 0x2101 | 26 | sensor_debounce_ms | UNSIGNED16 | rw (PRE-OP only) |  | 0x0005 |
| 0x2101 | 27 | standstill_quiet_ms | UNSIGNED16 | rw (PRE-OP only) |  | 0x0064 |
| 0x2101 | 28 | standstill_timeout_ms | UNSIGNED16 | rw (PRE-OP only) |  | 0x01F4 |
| 0x2101 | 29 | end_switches_wired | BOOLEAN | rw (PRE-OP only) |  | 0 |
| 0x2101 | 30 | homing_continue_to_stop0 | BOOLEAN | rw (PRE-OP only) |  | 1 |
| 0x2101 | 31 | stop_mode_brake | BOOLEAN | rw (PRE-OP only) |  | 0 |
| 0x2101 | 32 | rereference_on_sensor | BOOLEAN | rw (PRE-OP only) |  | 1 |
| 0x2300 | 0 | Diagnostics: highest sub-index | UNSIGNED8 | const |  | 3 |
| 0x2300 | 1 | Hall count (raw) | INTEGER32 | ro |  | 0 |
| 0x2300 | 2 | Reset cause (board port, TODO(Evans) encoding) | UNSIGNED8 | ro |  | 0 |
| 0x2300 | 3 | RPDO length errors | UNSIGNED32 | ro |  | 0x00000000 |

<!-- END GENERATED OD TABLE -->
