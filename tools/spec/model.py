"""Test oracle for specification review. Not firmware. Not a safety function.

Reference interpreter for docs/safety/spec/state_machine.yaml (base controller safety
state machine rev 5, platform-fw #17) and for the lift permit cases of platform-fw #20.
It exists so that the specification tables and the test vectors can be checked against
each other on a Linux host. It decides nothing on the robot and must never be compiled
into, linked with or translated into firmware.
"""
from __future__ import annotations

import copy
import pathlib
from dataclasses import dataclass, field
from typing import Any

import yaml

ROOT = pathlib.Path(__file__).resolve().parents[2]
SPEC = ROOT / "docs" / "safety" / "spec" / "state_machine.yaml"
VECTORS = ROOT / "tests" / "spec" / "vectors"

ALL = "*"


class SpecError(Exception):
    """The vector or the table is inconsistent with the specification."""


def load_yaml(path: pathlib.Path | str) -> Any:
    with open(path, encoding="utf-8") as fh:
        return yaml.safe_load(fh)


def _as_list(x: Any) -> list:
    return x if isinstance(x, list) else [x]


# --------------------------------------------------------------------------------------
# Safety state machine rev 5
# --------------------------------------------------------------------------------------
@dataclass
class SmStatus:
    state: str
    latches: dict
    vars: dict
    motor: str
    reason_mask: list
    first_reason: str | None
    reasons: list = field(default_factory=list)   # reasons emitted in the last step
    results: list = field(default_factory=list)   # command replies in the last step
    rows: list = field(default_factory=list)      # transition rows fired in the last step


class StateMachineModel:
    """Interprets the rev 5 table. One call to step() is one control tick."""

    LOCK_STATES = ("LOCKED", "CHARGER_FAULT")
    ACTIVE_STATES = ("RUN", "INHIBITED", "CHARGING")

    def __init__(self, spec: dict | None = None, params: dict | None = None):
        self.spec = spec if spec is not None else load_yaml(SPEC)
        self.params = dict(params or {})
        self.states = {s["id"]: s for s in self.spec["states"]}
        self.rows = self.spec["transitions"]
        self.na = self.spec.get("not_applicable", [])
        self.priority = self.spec["priority"]
        self.inputs: dict = {}
        self.fired: set = set()
        initial = [s["id"] for s in self.spec["states"] if s.get("initial")]
        if len(initial) != 1:
            raise SpecError("exactly one initial state required")
        # Before the first boot event nothing is defined; vectors start with boot.
        self.state = initial[0]
        self.latches = {l["id"]: (l["boot"] == "set") for l in self.spec["latches"]}
        self.vars = {"charge_cmd_present": False, "wcm_active": False, "reboot_pending": False}
        self.motor = "ZERO"
        self.reason_mask: list = []
        self.first_reason: str | None = None
        self.visited = {self.state}
        self.tick_start = self.state
        self._reasons: list = []
        self._results: list = []
        self._rows: list = []

    # ---- helpers ----------------------------------------------------------------------
    def _value(self, key: str) -> Any:
        if key in self.latches:
            return self.latches[key]
        if key in self.vars:
            return self.vars[key]
        return bool(self.inputs.get(key, False))  # missing input reads as not satisfied

    def _row_matches(self, row: dict, event: str, params: dict) -> bool:
        if event not in _as_list(row["event"]):
            return False
        frm = row["from"]
        if frm != ALL and self.state not in _as_list(frm):
            return False
        for k, v in (row.get("when") or {}).items():
            if params.get(k) != v:
                return False
        for k, v in (row.get("guard") or {}).items():
            if self._value(k) != v:
                return False
        if row["id"] == "Q1" and self.params.get("wcm_blocks_run") and self.vars["wcm_active"]:
            return False  # CONFLICT C3, owner ruling pending
        return True

    def _check_applicable(self, event: str) -> None:
        for na in self.na:
            if na["event"] == event and self.tick_start in na["states"]:
                raise SpecError(f"event {event} is not applicable in {self.state} ({na['source']})")

    def _record(self, reason: str, prev_state: str) -> None:
        new_episode = prev_state in self.ACTIVE_STATES and self.state in self.LOCK_STATES
        if new_episode:
            self.reason_mask = []
            self.first_reason = None
        if reason not in self.reason_mask:
            self.reason_mask.append(reason)
        if self.first_reason is None:
            self.first_reason = reason

    def _ack(self) -> bool:
        cleared = False
        if self.latches["wcm_latched"] and not self.vars["wcm_active"]:
            self.latches["wcm_latched"] = False
            cleared = True
        if self.latches["interlock_latched"]:
            # CHARGE_REQUEST is off in CHARGER_FAULT and the attempt has ended (5.3).
            self.latches["interlock_latched"] = False
            cleared = True
        return cleared

    # ---- one event ----------------------------------------------------------------------
    def apply(self, event: str, params: dict | None = None, depth: int = 0) -> None:
        params = dict(params or {})
        if depth > 4:
            raise SpecError("internal event loop")
        if event == "boot":
            params.setdefault("cause", "POWER_ON")
        meta = self.spec["events"].get(event)
        if meta is None:
            raise SpecError(f"unknown event {event}")
        for k in (meta.get("params") or {}):
            params.setdefault(k, {"session": "current", "seq": "new", "queued": False}.get(k))
        self._check_applicable(event)
        row = next((r for r in self.rows if self._row_matches(r, event, params)), None)
        if row is None:
            raise SpecError(f"no transition row for event {event} in state {self.state} with {params}")
        self.fired.add(row["id"])
        self._rows.append(row["id"])
        prev = self.state
        for k, v in (row.get("set") or {}).items():
            self.latches[k] = v
        for k, v in (row.get("vars") or {}).items():
            self.vars[k] = v
        if event == "boot":
            self.reason_mask, self.first_reason = [], None
        if "to" in row:
            self.state = row["to"]
            self.visited.add(self.state)
        if row.get("motor"):
            self.motor = row["motor"]
        if self.state != "RUN":
            self.motor = "ZERO"
        if row.get("run_entry") or (self.state == "RUN" and prev != "RUN"):
            self.motor = "ZERO"
            self.reason_mask, self.first_reason = [], None   # lock episode ends (4.1)
        result = row.get("result")
        if row.get("ack"):
            result = "accepted" if self._ack() else "rejected"
            if result == "rejected":
                self._reasons.append("CHARGER_ACK_REJECTED")
        if result:
            self._results.append(result)
        if row.get("reason"):
            reason = row["reason"].format(**params)
            self._reasons.append(reason)
            if row.get("record"):
                self._record(reason, prev)
        if row.get("then"):
            self.apply(row["then"], {}, depth + 1)

    # ---- one tick -----------------------------------------------------------------------
    def step(self, events: list, inputs: dict | None = None) -> SmStatus:
        if inputs:
            self.inputs.update(inputs)
        self._reasons, self._results, self._rows = [], [], []
        self.tick_start = self.state

        def rank(ev):
            name = ev["event"]
            return self.priority.index(name) if name in self.priority else self.priority.index("others")

        for ev in sorted(events, key=rank):  # sorted() is stable: same rank keeps vector order
            self.apply(ev["event"], ev.get("params"))
        return self.status()

    def status(self) -> SmStatus:
        return SmStatus(self.state, dict(self.latches), dict(self.vars), self.motor,
                        list(self.reason_mask), self.first_reason, list(self._reasons),
                        list(self._results), list(self._rows))

    def outputs(self) -> dict:
        s = self.states[self.state]
        return {"motor": self.motor, "lamp": s["lamp"], "charge_request": s["charge_request"]}

    # ---- static table checks --------------------------------------------------------------
    def table_problems(self) -> list:
        """Every (state, external event) pair must be handled or declared not applicable."""
        problems = []
        ids = [r["id"] for r in self.rows]
        if len(ids) != len(set(ids)):
            problems.append("duplicate row ids")
        states = list(self.states)
        for r in self.rows:
            for s in ([] if r["from"] == ALL else _as_list(r["from"])):
                if s not in states:
                    problems.append(f"row {r['id']} names unknown state {s}")
            if r.get("to") and r["to"] not in states:
                problems.append(f"row {r['id']} targets unknown state {r['to']}")
            if not r.get("source"):
                problems.append(f"row {r['id']} has no source")
            if r.get("status") == "CONFLICT" and not r.get("conflict"):
                problems.append(f"row {r['id']} is CONFLICT without both sides")
        for ev, meta in self.spec["events"].items():
            if meta.get("internal"):
                continue
            for s in states:
                if any(n["event"] == ev and s in n["states"] for n in self.na):
                    continue
                if not any(ev in _as_list(r["event"]) and (r["from"] == ALL or s in _as_list(r["from"]))
                           and not r.get("guard") and not r.get("when") for r in self.rows):
                    problems.append(f"no unconditional fallback for {ev} in {s}")
        return problems


# --------------------------------------------------------------------------------------
# Lift permit, base side (producer) and CTL-017 side (final local gate), platform-fw #20
# --------------------------------------------------------------------------------------
# Every number is a named parameter supplied by the vector. None of them is decided:
# stop_v_max (m/s), stop_w_max (rad/s), stop_hold_s, wheel_stale_s, arm_stale_s,
# track_m (TODO(Evans) drivetrain geometry), permit_timeout_s (TODO(Evans) I8 / CTL-017 OD),
# approved_poses (CONFLICT C8: STOWED only, or STOWED and CARRY).
PERMIT_PARAMS_BASE = ("stop_v_max", "stop_w_max", "stop_hold_s", "wheel_stale_s",
                      "arm_stale_s", "track_m", "approved_poses")
PERMIT_PARAMS_CTL = ("permit_timeout_s",)


def eval_expr(expr: Any, params: dict) -> Any:
    """Numbers in vectors may be written as arithmetic over parameter names."""
    if isinstance(expr, (int, float, bool)) or expr is None:
        return expr
    if isinstance(expr, str):
        try:
            return eval(expr, {"__builtins__": {}}, dict(params))  # noqa: S307, test data only
        except NameError:
            return expr
    return expr


class _Source:
    """Sequence and session filter for one evidence source (3.1 style, applied to evidence)."""

    def __init__(self):
        self.session = None   # session the consumer has established for this source
        self.last_seq = None
        self.sample = None
        self.rx = None

    def accept(self, msg: dict, now: float) -> str | None:
        if self.session is None or msg.get("session") != self.session:
            return "WRONG_SESSION"
        if not msg.get("valid", True):
            self.sample, self.rx = None, None
            return "INVALID"
        seq = msg.get("seq")
        if self.last_seq is not None and seq == self.last_seq:
            return "DUPLICATE"
        if self.last_seq is not None and seq < self.last_seq:
            return "OUT_OF_ORDER"
        self.last_seq, self.sample, self.rx = seq, msg, now
        return None

    def new_session(self, session) -> None:
        self.session, self.last_seq, self.sample, self.rx = session, None, None, None

    def fresh(self, now: float, stale_s: float) -> bool:
        return self.sample is not None and self.rx is not None and (now - self.rx) <= stale_s


class BasePermitModel:
    """Base controller side. Permit false at boot; withdrawn at once with a STOP request."""

    def __init__(self, params: dict):
        missing = [p for p in PERMIT_PARAMS_BASE if p not in params]
        self.configured = not missing
        self.p = params
        self.boot()

    def boot(self):
        self.left, self.right, self.arms = _Source(), _Source(), _Source()
        self.permit = False
        self.stopped_since = None
        self.base_fault = False
        self.ctl_link_ok = False
        self.reason = "BOOT"
        self.stop_request = False
        self.rejections: list = []

    def handle(self, ev: dict, now: float) -> None:
        name, a = ev["event"], ev.get("params") or {}
        if name == "boot":
            self.boot()
        elif name == "source_session":
            {"left": self.left, "right": self.right, "arms": self.arms}[a["source"]].new_session(a["session"])
        elif name in ("wheel", "arms"):
            src = self.arms if name == "arms" else (self.left if a["side"] == "left" else self.right)
            why = src.accept(a, now)
            if why:
                # Fail closed (INTERPRETATION of platform-fw #20: invalid, stale, out-of-sequence
                # or pre-reboot evidence is UNKNOWN): the source loses its sample and a wheel
                # rejection restarts the stopped qualification.
                self.rejections.append(f"{name}:{a.get('side', 'arms')}:{why}")
                src.sample, src.rx = None, None
                if name == "wheel":
                    self.stopped_since = None
        elif name == "base_fault":
            self.base_fault = bool(a.get("active", True))
        elif name == "ctl017_link":
            self.ctl_link_ok = bool(a.get("ok", True))
        elif name == "tick":
            pass
        else:
            raise SpecError(f"unknown base permit event {name}")

    def evaluate(self, now: float) -> None:
        p = {k: eval_expr(v, self.p) for k, v in self.p.items()}
        was = self.permit
        reason = None
        if not self.configured:
            reason = "NOT_CONFIGURED"
        wheels_ok = self.left.fresh(now, p.get("wheel_stale_s", 0)) and self.right.fresh(now, p.get("wheel_stale_s", 0))
        if reason is None and not wheels_ok:
            reason = "BASE_FEEDBACK_INVALID"
            self.stopped_since = None
        if reason is None:
            vl = float(eval_expr(self.left.sample["v"], p))
            vr = float(eval_expr(self.right.sample["v"], p))
            v, w = (vl + vr) / 2.0, (vr - vl) / float(p["track_m"])
            if abs(v) >= p["stop_v_max"] or abs(w) >= p["stop_w_max"]:
                reason = "BASE_MOVING"
                self.stopped_since = None
            elif self.stopped_since is None:
                self.stopped_since = now
        if reason is None and (now - self.stopped_since) < p["stop_hold_s"] - 1e-9:
            reason = "BASE_STOP_NOT_QUALIFIED"
        if reason is None:
            if self.arms.sample is None:
                reason = "ARM_STATUS_MISSING"
            elif not self.arms.fresh(now, p["arm_stale_s"]):
                reason = "ARM_STATUS_STALE"
            else:
                for arm in ("left", "right"):
                    st = self.arms.sample[arm]
                    if st.get("fault"):
                        reason = "ARM_CONTROLLER_FAULT"
                    elif st.get("moving", True):
                        reason = "ARM_MOTION_ACTIVE"
                    elif st.get("pose", "UNKNOWN") not in p["approved_poses"]:
                        reason = "ARM_POSE_UNSAFE"
                    if reason:
                        break
                pose = self.arms.sample.get("bimanual", "UNKNOWN")
                if reason is None and pose not in p["approved_poses"]:
                    reason = "ARM_POSE_UNSAFE"
        if reason is None and self.base_fault:
            reason = "BASE_FAULT"
        if reason is None and not self.ctl_link_ok:
            reason = "CTL017_LINK_LOST"
        self.permit = reason is None
        self.reason = reason or "OK"
        self.stop_request = was and not self.permit

    def step(self, t: float, events: list) -> dict:
        self.rejections = []
        for ev in events:
            self.handle(ev, t)
        self.evaluate(t)
        return {"permit": self.permit, "reason": self.reason, "stop_request": self.stop_request,
                "rejections": list(self.rejections)}


class CtlPermitModel:
    """CTL-017 side: the final local actuator gate. Wire layout deliberately abstract."""

    def __init__(self, params: dict):
        self.configured = all(k in params for k in PERMIT_PARAMS_CTL)
        self.p = params
        self.boot()

    def boot(self):
        self.src = _Source()
        self.lift = "IDLE"          # IDLE, MOVING, HOMING
        self.target = None
        self.drive = "STOP"
        self.last_reject = None
        self.stop_reason = None
        self.heartbeat_rx = None

    def permit_valid(self, now: float) -> tuple[bool, str]:
        if not self.configured:
            return False, "NOT_CONFIGURED"
        s = self.src
        if s.session is None or s.sample is None:
            return False, "NO_PERMIT"
        if not s.fresh(now, float(eval_expr(self.p["permit_timeout_s"], self.p))):
            return False, "PERMIT_EXPIRED"
        if not s.sample.get("value"):
            return False, "PERMIT_FALSE"
        return True, "OK"

    def step(self, t: float, events: list) -> dict:
        self.last_reject, rejections = None, []
        for ev in events:
            name, a = ev["event"], ev.get("params") or {}
            if name == "boot":
                self.boot()
            elif name == "producer_session":
                self.src.new_session(a["session"])
            elif name == "heartbeat":
                self.heartbeat_rx = t
            elif name == "permit":
                why = self.src.accept({**a, "valid": True}, t)
                if why:
                    # A rejected permit frame drops the permit (fail closed, INTERPRETATION).
                    rejections.append(f"PERMIT_{why}")
                    self.src.sample, self.src.rx = None, None
            elif name == "cmd":
                kind = a["kind"]
                if kind == "STOP":
                    self.lift, self.target, self.drive, self.stop_reason = "IDLE", None, "STOP", "STOP_CMD"
                    continue
                ok, why = self.permit_valid(t)
                if not ok:
                    self.last_reject = why
                    continue
                if not a.get("local_ok", True):
                    self.last_reject = "LOCAL_CHECK"
                    continue
                self.lift = "MOVING" if kind == "MOVE_TO_STOP" else "HOMING"
                self.target = a.get("target") if kind == "MOVE_TO_STOP" else "HOME"
                self.drive, self.stop_reason = "RUN", None
            elif name == "motion_done":
                self.lift, self.target, self.drive = "IDLE", None, "STOP"
            elif name == "tick":
                pass
            else:
                raise SpecError(f"unknown CTL-017 permit event {name}")
        if self.lift != "IDLE":
            ok, why = self.permit_valid(t)
            if not ok:
                self.lift, self.target, self.drive = "IDLE", None, "STOP"
                self.stop_reason = "PERMIT_WITHDRAWN" if why == "PERMIT_FALSE" else why
        return {"lift": self.lift, "target": self.target, "drive": self.drive,
                "reject": self.last_reject, "stop_reason": self.stop_reason,
                "rejections": rejections, "permit_valid": self.permit_valid(t)[0]}


# --------------------------------------------------------------------------------------
# Vector runner
# --------------------------------------------------------------------------------------
def _sm_snapshot(m: StateMachineModel, st: SmStatus) -> dict:
    probe = copy.deepcopy(m)
    probe._rows, probe._results, probe._reasons = [], [], []
    probe.apply("motion_cmd", {"session": "current", "seq": "new", "queued": False})
    return {
        "state": st.state,
        "latches": {k: st.latches[k] for k in ("reset_seen", "ui_stop", "jetson_stop", "wcm_latched", "interlock_latched")},
        "outputs": m.outputs(),
        "reasons": st.reasons,
        "reason_mask": st.reason_mask,
        "first_reason": st.first_reason,
        "results": st.results,
        "motion_admissible": probe.motor == "FOLLOW_CMD",
    }


def _compare(expect: dict, actual: dict, where: str) -> list:
    errs = []
    for k, v in expect.items():
        if k not in actual:
            errs.append(f"{where}: unknown expectation key {k}")
        elif isinstance(v, dict) and isinstance(actual[k], dict):
            for kk, vv in v.items():
                if actual[k].get(kk) != vv:
                    errs.append(f"{where}: {k}.{kk} expected {vv!r}, model {actual[k].get(kk)!r}")
        elif actual[k] != v:
            errs.append(f"{where}: {k} expected {v!r}, model {actual[k]!r}")
    return errs


SM_REQUIRED = ("state", "latches", "outputs", "motion_admissible")


def run_vector(vec: dict, spec: dict | None = None) -> tuple[list, Any]:
    """Return (errors, model). Errors are empty when the vector agrees with the table.

    A vector either has 'steps' or a list of 'cases', each with its own 'steps' and an
    independent model instance (used for the H24 matrix).
    """
    if "cases" in vec:
        errs, last = [], None
        for case in vec["cases"]:
            sub = {**{k: v for k, v in vec.items() if k != "cases"}, **case}
            sub["id"] = f"{vec['id']}/{case['id']}"
            e, last = run_vector(sub, copy.deepcopy(spec) if spec is not None else None)
            errs += e
        return errs, last
    kind = vec.get("kind", "state_machine")
    params = {k: v for k, v in (vec.get("params") or {}).items()}
    errs: list = []
    last_t = None
    if kind == "state_machine":
        m: Any = StateMachineModel(spec, params)
    elif kind == "permit_base":
        m = BasePermitModel(params)
    elif kind == "permit_ctl017":
        m = CtlPermitModel(params)
    else:
        return [f"unknown kind {kind}"], None
    for i, step in enumerate(vec["steps"]):
        where = f"{vec['id']} step {i}"
        t = float(eval_expr(step.get("t", 0), params))
        if last_t is not None and t < last_t:
            errs.append(f"{where}: time goes backwards")
        last_t = t
        try:
            if kind == "state_machine":
                st = m.step(step.get("events", []), step.get("inputs"))
                actual = _sm_snapshot(m, st)
                exp = step.get("expect") or {}
                for req in SM_REQUIRED:
                    if req not in exp:
                        errs.append(f"{where}: expectation {req} missing")
            else:
                evs = [{"event": e["event"], "params": {k: eval_expr(v, params) for k, v in (e.get("params") or {}).items()}}
                       for e in step.get("events", [])]
                actual = m.step(t, evs)
                exp = step.get("expect") or {}
        except SpecError as exc:
            errs.append(f"{where}: {exc}")
            break
        errs += _compare(exp, actual, where)
    return errs, m


def load_vectors(directory: pathlib.Path = VECTORS) -> list:
    return [(p, load_yaml(p)) for p in sorted(directory.glob("*.yaml"))]
