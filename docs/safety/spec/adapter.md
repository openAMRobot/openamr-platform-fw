# Driving the owner's C implementation with the spec vectors

Status: proposal for the firmware owner (Evans). Test oracle material for specification
review. Not firmware. Not a safety function.

The vectors in `tests/spec/vectors/` describe the rev 5 safety state machine (platform-fw #17)
and the lift permit cases (platform-fw #20) as data. The Python model in `tools/spec/model.py`
only proves that the vectors agree with `state_machine.yaml`. The owner's C code is the
implementation under test; this file describes the smallest interface his host harness needs
so that it can replay the same files unchanged. Nothing here prescribes how the C code is
structured inside.

## 1. Vector format

Every vector file has `id`, `title`, `covers` (rev 5 or #20 references), `status`
(`REV5`, `ADDED` with `added_reason`, `CONFLICT`, `PROPOSAL`), optional `kind`
(`state_machine` by default, `permit_base`, `permit_ctl017`), optional `params`, and either
`steps` or `cases` (each case has its own `id` and `steps` and starts from a fresh instance).

A step is:

```yaml
- t: 0.3                  # seconds, monotonic, never decreasing
  inputs: {run_ok: false} # condition inputs; they keep their value until changed
  events:                 # applied in this tick, in priority order (see 3)
  - event: safety_ok_low
  expect:                 # only the keys present are checked
    state: LOCKED
    latches: {reset_seen: false, ui_stop: false, jetson_stop: false}
    outputs: {motor: ZERO, lamp: 'ON', charge_request: 'OFF'}
    motion_admissible: false
    reasons: [HW_SAFETY_NOT_ENABLED]   # reasons emitted in this tick, in order
    reason_mask: [...]                 # lock-episode mask (rev 5 4.1)
    first_reason: HW_SAFETY_NOT_ENABLED
    results: [rejected]                # replies to supervisory or motion commands
```

`motion_admissible` answers: "would a fresh, current-session, new-sequence motion command
received right now be executed?" It is the "first admissible motion command" column of rev 5.
The harness checks it by sending such a command to a copy of the controller context (or by
calling a pure query function if the owner provides one), never to the live context.

## 2. Minimal step interface for the state machine

The harness needs five things from the C side. Names are suggestions; the owner chooses.

| Need | Suggested C shape | Notes |
| --- | --- | --- |
| Create a context | `void sm_init(sm_ctx_t *ctx, const sm_cfg_t *cfg);` | No global state. Unconfigured config must report NOT_CONFIGURED. |
| Feed one tick | `void sm_tick(sm_ctx_t *ctx, uint32_t now_ms, const sm_in_t *in, const sm_event_t *ev, size_t n_ev, sm_out_t *out);` | `in` carries levels, `ev` the edge events of this tick. |
| Read state | fields of `sm_out_t`: state, latches, motor, lamp, charge request | Matches `expect.state`, `latches`, `outputs`. |
| Read reasons | reason list of the tick, `reason_mask`, `first_reason` | Numeric registry IDs mapped back to names by a table in the harness. |
| Copy a context | plain struct copy | For the `motion_admissible` probe. |

The harness itself (YAML reader, comparison, report) is test code under `tests/`. A small
Python driver using `ctypes` on a host shared library, or a C driver reading a JSON export of
the vectors, both work. The Python model can export a vector to JSON with
`python3 -c "import json,sys; sys.path.insert(0,'tools/spec'); import model; print(json.dumps(model.load_yaml(sys.argv[1])))" tests/spec/vectors/h01.yaml`.

### 2.1 Mapping events to raw inputs

The vectors use abstract events because rev 5 states its rules as events. Where the C code
expects raw signal levels, the adapter expands each event into input edges with the timing
parameters of rev 5. None of these numbers is fixed by the vectors; the adapter takes them
from the owner's configuration.

| Vector event | Raw expansion in the adapter |
| --- | --- |
| `boot {cause}` | fresh `sm_init`, reset cause injected |
| `safety_ok_low` | Y32 high to low (acts at once, rev 5 5.2) |
| `estop_diag` | base E-stop spare NO active, or CTL-017 moving-plate status active or stale |
| `session_lost` | no valid session heartbeat for the host session timeout (I8, open) |
| `bms_fault` | BMS warning, fault or stale data |
| `input_invalid {subcause}` | encoder data missing, stale or implausible; `RESET_HIGH`: RESET NC input high for 2 s |
| `reset_hold_5s` | RESET input high for 5 s (one event per press) |
| `reset_short` | RESET low to high, high for less than 2 s, high to low; `inputs.qualified` decides whether SAFETY_OK is held high (passes) or held low for the 1 s window (fails) |
| `wcm_fault`, `wcm_fault_clear` | WCM_FAULT input asserted or normal |
| `charge_interlock` | SAFETY_OK still high 500 ms after CHARGE_REQUEST, or high again for 20 ms while charging |
| `drive_fault`, `drive_comms {kind}` | drive fault report; heartbeat consumer timeout, stale drive status or configuration read-back mismatch |
| `cmd_stale` | no fresh motion command for the command freshness limit (200 ms, I8 decision 3, open) |
| `base_not_stopped` | stopped evidence lost while charging |
| `ui_stop`, `ui_resume`, `jetson_stop_release`, `charge_cmd`, `charge_cmd_removed`, `charger_ack`, `motion_cmd` | supervisory or motion command with `session` (`current` or `old`) and `seq` (`new` or `replay`) translated to concrete epoch and sequence values by the adapter |

Condition inputs (`run_ok`, `base_stopped`, `arming_ok`, `charge_pre_ok`) are aggregates. The
adapter sets the underlying raw inputs to a nominal value when true and to one failing value
when false. The owner may add vectors that split an aggregate into its parts; the model then
needs the same split, which is a change to `state_machine.yaml`.

## 3. Same-tick order

Events in one step are applied in the priority of rev 5 section 4: the 5 s RESET hold first,
then WCM_FAULT, then the charge-interlock fault, then every other event in file order. If the C
code processes the raw inputs of one tick in a fixed internal order, the adapter must give
that order the same effect. Vector `h34` checks this; open question Q7 in the report asks the
owner to fix the order inside "others".

## 4. Lift permit vectors

`permit_base_*` drive the base controller's permit producer; `permit_ctl017_*` drive the
CTL-017 gate. The wire format is deliberately abstract (CONFLICT C7): COB-ID, mapping,
period, sequence width, session handshake and timeout are not decided. Each `permit` or
`wheel` or `arms` event carries `seq` and `session`; the adapter encodes them in whatever
frame the frozen I8 and CTL-017 object dictionary define.

| Producer side need | Consumer side need |
| --- | --- |
| feed one wheel or arm sample with receipt time, sequence and session | feed one permit frame with receipt time, sequence, session and value |
| establish a source session (`source_session`) | establish the producer session (`producer_session`) |
| set base fault and CTL-017 link state | send `MOVE_TO_STOP`, `START_HOMING`, `STOP`, with `local_ok` standing for the local lift checks |
| read permit, reason, STOP request | read lift state, target, drive output (`RUN` or `STOP`), reject and stop reason |

Parameters live in `params` with a `param_status` per value: PROPOSED (from #20, provisional
until B5) or TEST VALUE with TODO(Evans). The C harness passes them through its own config
struct; a missing parameter must make the module report NOT_CONFIGURED and never permit
(see `test_permit_unconfigured_never_permits`).

## 5. What the vectors do not cover

- Raw debounce timing (20 ms), the 1 s qualification window and the 2 s and 5 s RESET timers
  are folded into events; the owner's own host tests check them at the input level.
- Heartbeat or session start with a stale epoch (part of H27) has no event in the model.
- Bench tests B1 to B10 are physical and stay with the owner.
- The vectors never prove that a motor or the lift physically stopped.
