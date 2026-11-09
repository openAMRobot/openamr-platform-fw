# CAN termination check (power off)

**Proves:** each CAN bus has exactly two 120 ohm terminations, one at each end of the trunk.
**Applies to:** CAN1 (ZLAC8015D), CAN2 (Daly BMS), CAN3 (CTL-017). Do it once per bus, and again
after any harness or connector change.

## Preconditions

- Robot fully powered off, battery disconnected, no charger. Wait 1 minute for the DC links to discharge.
- No node bench-powered from USB. A powered node can corrupt the reading.
- Multimeter on the resistance range, probes on CAN_H and CAN_L at the bus connector of the base controller.

## Measure

1. Measure CAN_H to CAN_L. Record the value.
2. Measure CAN_H to ground and CAN_L to ground. Both must be open or very high (no short, no leakage path).
3. Remove one node connector at the far end and measure again. Expect about 120 ohm (one termination left). Put the connector back.

## Pass criterion

- CAN_H to CAN_L reads **about 60 ohm** (two 120 ohm in parallel). Accept 55 to 65 ohm.
- About 120 ohm with one end disconnected, **open** (no reading) with both terminations removed.
- No short between CAN_H, CAN_L and ground.

## Reading it

| Reading | Meaning |
|---|---|
| about 60 ohm | correct, two terminations |
| about 120 ohm | one termination missing or one node end not wired |
| about 40 ohm | a third termination (an extra module or an enabled on-board resistor) |
| open | no termination, or a broken wire |
| near 0 ohm | short between CAN_H and CAN_L |

Check the on-board termination of each node (NUCLEO and transceiver boards, the ZLAC8015D, the
isolated CAN2 module). Which ones carry the two terminations of each bus is `TODO(Evans)`.

## Record

Date, bus, the three readings, tester. Attach to the G4 evidence.
