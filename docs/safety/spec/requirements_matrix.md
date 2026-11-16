# Requirement to vector matrix

Test oracle traceability for specification review. Not firmware. Not safety evidence.

One row per requirement identifier that `state_machine.yaml` cites from its source documents
(header and every `source` field), plus the identifiers the vectors' `covers` lists name from
the same documents. The source documents themselves (rev 5 review copy, platform-fw #20,
D-24, P-03) were not available to this check: the list is complete only with respect to what
the specification cites, not with respect to the documents. "not traced" means no vector
names the requirement in `covers`; it does not mean the requirement is untested by accident or
tested elsewhere. A vector listed here names the requirement in `covers`; that is a claim of the
vector author, checked only for consistency by `tests/spec/test_spec_checks.py`.

Identifiers in `covers` that are not from a source document and have no row: `All` (h23,
every state), `B9` and `D14` (rows of `fault_tree.md` in this PR).

Section 4 (same-tick priority) and 4.1 are also checked outside the vectors, by the same-tick
and permutation tests of `tests/spec/test_spec_checks.py`; those tests are not listed in the
Vectors column, so 4 (same-tick priority) stays "not traced" here.

| Requirement | Document | Kind | Vectors | Status |
| --- | --- | --- | --- | --- |
| 2.1 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | section | - | not traced |
| 2.2 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | section | - | not traced |
| 2.3 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | section | h16, h17, h18, h33 | traced |
| 2.4 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | section | - | not traced |
| 3 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | section | h33, h35 | traced |
| 3.1 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | section | h06, h27 | traced |
| 4 (same-tick priority) | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | section | - | not traced |
| 4 command table | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | section | x01_no_effect_cells | traced |
| 4 events by state | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | section | h24 | traced |
| 4 scope by state | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | section | - | not traced |
| 4.1 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | section | h34 | traced |
| 5.1 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | section | h10, h25, x01_no_effect_cells | traced |
| 5.2 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | section | h15 | traced |
| 5.3 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | section | h11, x01_no_effect_cells | traced |
| 5.4 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | section | - | not traced |
| 5.5 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | section | - | not traced |
| 5.6 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | section | h31, x01_no_effect_cells | traced |
| 8 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | section | - | not traced |
| T1 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | transition row | h01, h26 | traced |
| T2 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | transition row | h02, h03, h29 | traced |
| T3 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | transition row | h03, h04, h14, h28, h29 | traced |
| T4 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | transition row | h05, h19, h28 | traced |
| T5 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | transition row | h06, h20 | traced |
| T6 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | transition row | h07, h20 | traced |
| T7 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | transition row | h08 | traced |
| T8 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | transition row | h09, h10, h25, h26 | traced |
| T9 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | transition row | h11, h21, h32 | traced |
| T10a | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | transition row | h12, h30 | traced |
| T10b | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | transition row | h13, h30 | traced |
| T11 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | transition row | h22 | traced |
| H1 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h01 | traced |
| H2 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h02 | traced |
| H3 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h03 | traced |
| H4 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h04 | traced |
| H5 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h05 | traced |
| H6 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h06 | traced |
| H7 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h07 | traced |
| H8 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h08 | traced |
| H9 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h09 | traced |
| H10 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h10 | traced |
| H11 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h11 | traced |
| H12 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h12 | traced |
| H13 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h13 | traced |
| H14 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h14 | traced |
| H15 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h15 | traced |
| H16 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h16 | traced |
| H17 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h17 | traced |
| H18 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h18 | traced |
| H19 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h19 | traced |
| H20 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h20 | traced |
| H21 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h21 | traced |
| H22 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h22 | traced |
| H23 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h23 | traced |
| H24 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h24 | traced |
| H25 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h25 | traced |
| H26 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h26 | traced |
| H27 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h27 | traced |
| H28 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h28 | traced |
| H29 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h29 | traced |
| H30 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h30 | traced |
| H31 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h31 | traced |
| H32 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h32 | traced |
| H33 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h33 | traced |
| H34 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h34 | traced |
| H35 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | host test | h35 | traced |
| R1 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | requirement | h02 | traced |
| R2 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | requirement | h10 | traced |
| R4 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | requirement | permit_ctl_01_admission | traced |
| R7 | rev 5 (E-01 safety state machine, review copy of 11 Nov 2026, platform-fw #17) | requirement | h22 | traced |
| #20 1 | platform-fw #20 (lift permit design, 11 to 13 Nov 2026) | item | permit_base_02_opposite_wheels | traced |
| #20 2 | platform-fw #20 (lift permit design, 11 to 13 Nov 2026) | item | permit_base_05_arms, permit_base_06_loss_while_permitted, permit_base_07_reboot, permit_ctl_03_recovery | traced |
| #20 3 | platform-fw #20 (lift permit design, 11 to 13 Nov 2026) | item | permit_ctl_01_admission, permit_ctl_02_during_motion, permit_ctl_03_recovery | traced |
| #20 4 | platform-fw #20 (lift permit design, 11 to 13 Nov 2026) | item | permit_base_01_hold_time, permit_base_02_opposite_wheels, permit_base_03_one_wheel_moving, permit_base_04_encoder_evidence, permit_base_06_loss_while_permitted, permit_base_07_reboot | traced |
| #20 4 CTL-017 | platform-fw #20 (lift permit design, 11 to 13 Nov 2026) | item | permit_ctl_01_admission, permit_ctl_02_during_motion | traced |
| #20 5 | platform-fw #20 (lift permit design, 11 to 13 Nov 2026) | item | permit_base_05_arms | traced |
| D-24 | decision D-24 (11 Nov 2026) | decision | - | not traced |
| P-03 item 30 | P-03 item 30 | item | - | not traced |
