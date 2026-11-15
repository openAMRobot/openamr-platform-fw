# SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image.
"""Adapter of docs/safety/spec/adapter.md: replays every vector of tests/spec/vectors on the
Python model (tools/spec/model.py) and on the C simulation reference (libsimref_adapter.so,
through ctypes) and lists every difference. Agreement is consistency between two readings of
the same table, never owner approval."""
from __future__ import annotations

import copy
import ctypes
import json
import os
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "spec"))
import model  # noqa: E402

LIB_ENV = "SIMREF_ADAPTER_LIB"
POSES = {"UNKNOWN": 0, "STOWED": 1, "CARRY": 2}
CAUSE = {"POWER_ON": 0, "WATCHDOG": 1, "SOFTWARE": 2}
SUBCAUSE = {"MISSING": 0, "STALE": 1, "IMPLAUSIBLE": 2, "RESET_HIGH": 3}
KIND = {"COMMS": 0, "CONFIG": 1}
INPUT_BITS = ("run_ok", "base_stopped", "arming_ok", "qualified", "charge_pre_ok")
SRC = {"left": 0, "right": 1, "arms": 2}
# Vector kinds that the base controller reference does not implement: the CTL-017 gate is the
# lift controller's logic (STM32G474RE), outside the platform lead's decision of 15 Nov 2026.
OUT_OF_SCOPE_KINDS = ("permit_ctl017",)


def load_lib(path: str | None = None) -> ctypes.CDLL:
    path = path or os.environ.get(LIB_ENV)
    if not path or not os.path.exists(path):
        raise RuntimeError(f"{LIB_ENV} must name the built libsimref_adapter.so (got {path!r})")
    lib = ctypes.CDLL(path)
    vp, ip, d, ll, u = ctypes.c_void_p, ctypes.POINTER(ctypes.c_int), ctypes.c_double, ctypes.c_longlong, ctypes.c_uint
    lib.simref_ad_sm_new.restype = vp
    lib.simref_ad_free.argtypes = [vp]
    lib.simref_ad_sm_tick.argtypes = [vp, u, ctypes.c_int, ip, ip, ip, ip, ip]
    lib.simref_ad_sm_snapshot.argtypes = [vp, ctypes.c_char_p, ctypes.c_int]
    lib.simref_ad_event_name.restype = ctypes.c_char_p
    lib.simref_ad_permit_new.restype = vp
    lib.simref_ad_permit_new.argtypes = [u, d, d, d, ll, ll, ll, u]
    for name in ("simref_ad_permit_boot", "simref_ad_permit_begin"):
        getattr(lib, name).argtypes = [vp]
    lib.simref_ad_permit_session.argtypes = [vp, ctypes.c_int, u]
    lib.simref_ad_permit_wheel.argtypes = [vp, ll, ctypes.c_int, u, ll, ctypes.c_int, d]
    lib.simref_ad_permit_arms.argtypes = [vp, ll, u, ll] + [ctypes.c_int] * 8
    lib.simref_ad_permit_flags.argtypes = [vp] + [ctypes.c_int] * 4
    lib.simref_ad_permit_evaluate.argtypes = [vp, ll]
    lib.simref_ad_permit_snapshot.argtypes = [vp, ctypes.c_char_p, ctypes.c_int]
    lib.event_ids = {lib.simref_ad_event_name(i).decode(): i for i in range(lib.simref_ad_event_count())}
    return lib


def _snap(fn, h) -> dict:
    buf = ctypes.create_string_buffer(16384)
    n = fn(h, buf, len(buf))
    if n < 0:
        raise RuntimeError("snapshot buffer too small")
    return json.loads(buf.value.decode())


def _us(t) -> int:
    return int(round(float(t) * 1e6))


class CStateMachine:
    def __init__(self, lib):
        self.lib, self.h, self.inputs = lib, lib.simref_ad_sm_new(), {}

    def close(self):
        self.lib.simref_ad_free(self.h)

    def step(self, events: list, inputs: dict | None) -> tuple[int, dict]:
        if inputs:
            self.inputs.update(inputs)
        bits = sum(1 << i for i, k in enumerate(INPUT_BITS) if self.inputs.get(k, False))
        n = len(events)
        arr = lambda vals: (ctypes.c_int * max(n, 1))(*vals)  # noqa: E731
        ids, p, old, rep, q = [], [], [], [], []
        for ev in events:
            a = ev.get("params") or {}
            ids.append(self.lib.event_ids[ev["event"]])
            name = ev["event"]
            if name == "boot":
                p.append(CAUSE[a.get("cause", "POWER_ON")])
            elif name == "input_invalid":
                p.append(SUBCAUSE[a["subcause"]])
            elif name == "drive_comms":
                p.append(KIND[a["kind"]])
            else:
                p.append(0)
            old.append(1 if a.get("session", "current") == "old" else 0)
            rep.append(1 if a.get("seq", "new") == "replay" else 0)
            q.append(1 if a.get("queued", False) else 0)
        rc = self.lib.simref_ad_sm_tick(self.h, bits, n, arr(ids), arr(p), arr(old), arr(rep), arr(q))
        return rc, _snap(self.lib.simref_ad_sm_snapshot, self.h)


def model_sm_snapshot(m: model.StateMachineModel, st: model.SmStatus) -> dict:
    s = model._sm_snapshot(m, st)
    s["rows"] = list(st.rows)
    return s


SM_KEYS = ("state", "latches", "outputs", "reasons", "reason_mask", "first_reason", "results", "motion_admissible", "rows")


def compare_sm(vec: dict, lib, spec: dict) -> list:
    """Every step: the full snapshot of the C library must equal the model's; both must also
    agree on a spec error at the same step."""
    diffs = []
    m = model.StateMachineModel(copy.deepcopy(spec), dict(vec.get("params") or {}))
    c = CStateMachine(lib)
    try:
        for i, step in enumerate(vec["steps"]):
            where = f"{vec['id']} step {i}"
            m_err = None
            try:
                mst = m.step(step.get("events", []), step.get("inputs"))
                msnap = model_sm_snapshot(m, mst)
            except model.SpecError as exc:
                m_err = str(exc)
            rc, csnap = c.step(step.get("events", []), step.get("inputs"))
            if m_err is not None or rc != 0:
                if (m_err is None) != (rc == 0):
                    diffs.append(f"{where}: model error={m_err!r}, C rc={rc}")
                break
            for k in SM_KEYS:
                if msnap[k] != csnap[k]:
                    diffs.append(f"{where}: {k}: model {msnap[k]!r} C {csnap[k]!r}")
            diffs += model._compare(step.get("expect") or {}, {k: csnap[k] for k in csnap if k != "rows"},
                                    where + " (C vs vector)")
    finally:
        c.close()
    return diffs


class CPermit:
    def __init__(self, lib, params: dict):
        self.lib = lib
        p = {k: model.eval_expr(v, params) for k, v in params.items()}
        names = ("stop_v_max", "stop_w_max", "stop_hold_s", "wheel_stale_s", "arm_stale_s", "track_m", "approved_poses")
        set_bits = sum(1 << i for i, k in enumerate(names) if k in p)
        poses = sum(1 << POSES[x] for x in p.get("approved_poses", []) if x in POSES)
        self.h = lib.simref_ad_permit_new(set_bits, float(p.get("stop_v_max", 0)), float(p.get("stop_w_max", 0)),
                                          float(p.get("track_m", 1.0)), _us(p.get("stop_hold_s", 0)),
                                          _us(p.get("wheel_stale_s", 0)), _us(p.get("arm_stale_s", 0)), poses)
        self.params = params
        self.sessions: dict = {}

    def sid(self, s) -> int:
        return self.sessions.setdefault(s, len(self.sessions) + 1)

    def close(self):
        self.lib.simref_ad_free(self.h)

    def step(self, t: float, events: list) -> dict:
        L, now = self.lib, _us(t)
        L.simref_ad_permit_begin(self.h)
        for ev in events:
            name, a = ev["event"], ev.get("params") or {}
            if name == "boot":
                L.simref_ad_permit_boot(self.h)
            elif name == "source_session":
                L.simref_ad_permit_session(self.h, SRC[a["source"]], self.sid(a["session"]))
            elif name == "wheel":
                L.simref_ad_permit_wheel(self.h, now, SRC[a["side"]], self.sid(a.get("session")), int(a.get("seq", 0)),
                                         1 if a.get("valid", True) else 0, float(model.eval_expr(a.get("v", 0.0), self.params)))
            elif name == "arms":
                def arm(x):
                    st = a.get(x) or {}
                    return [POSES.get(st.get("pose", "UNKNOWN"), 3), 1 if st.get("moving", True) else 0,
                            1 if st.get("fault") else 0]
                L.simref_ad_permit_arms(self.h, now, self.sid(a.get("session")), int(a.get("seq", 0)),
                                        1 if a.get("valid", True) else 0, *arm("left"), *arm("right"),
                                        POSES.get(a.get("bimanual", "UNKNOWN"), 3))
            elif name == "base_fault":
                L.simref_ad_permit_flags(self.h, 1 if a.get("active", True) else 0, 1, 0, 0)
            elif name == "ctl017_link":
                L.simref_ad_permit_flags(self.h, 0, 0, 1 if a.get("ok", True) else 0, 1)
            elif name == "tick":
                pass
            else:
                raise RuntimeError(f"unknown base permit event {name}")
        L.simref_ad_permit_evaluate(self.h, now)
        return _snap(L.simref_ad_permit_snapshot, self.h)


PERMIT_KEYS = ("permit", "reason", "stop_request", "rejections")


def compare_permit(vec: dict, lib) -> list:
    diffs = []
    params = dict(vec.get("params") or {})
    m = model.BasePermitModel(params)
    c = CPermit(lib, params)
    try:
        for i, step in enumerate(vec["steps"]):
            where = f"{vec['id']} step {i}"
            t = float(model.eval_expr(step.get("t", 0), params))
            evs = [{"event": e["event"], "params": {k: model.eval_expr(v, params) for k, v in (e.get("params") or {}).items()}}
                   for e in step.get("events", [])]
            ms = m.step(t, copy.deepcopy(evs))
            cs = c.step(t, evs)
            for k in PERMIT_KEYS:
                if ms[k] != cs[k]:
                    diffs.append(f"{where}: {k}: model {ms[k]!r} C {cs[k]!r}")
            diffs += model._compare(step.get("expect") or {}, cs, where + " (C vs vector)")
    finally:
        c.close()
    return diffs


def expand(vec: dict) -> list:
    if "cases" not in vec:
        return [vec]
    out = []
    for case in vec["cases"]:
        sub = {**{k: v for k, v in vec.items() if k != "cases"}, **case}
        sub["id"] = f"{vec['id']}/{case['id']}"
        out.append(sub)
    return out


def run_all(lib) -> dict:
    """Returns {vector id: {'kind', 'result': 'SAME'|'DIFFERENT'|'OUT_OF_SCOPE', 'diffs'}}."""
    spec = model.load_yaml(model.SPEC)
    report = {}
    for path, vec in model.load_vectors():
        kind = vec.get("kind", "state_machine")
        for sub in expand(vec):
            if kind in OUT_OF_SCOPE_KINDS:
                report[sub["id"]] = {"kind": kind, "result": "OUT_OF_SCOPE", "diffs": []}
                continue
            diffs = compare_sm(sub, lib, spec) if kind == "state_machine" else compare_permit(sub, lib)
            report[sub["id"]] = {"kind": kind, "result": "DIFFERENT" if diffs else "SAME", "diffs": diffs}
    return report


def main() -> int:
    lib = load_lib(sys.argv[1] if len(sys.argv) > 1 else None)
    rep = run_all(lib)
    counts: dict = {}
    for vid, r in rep.items():
        counts[r["result"]] = counts.get(r["result"], 0) + 1
        if r["diffs"]:
            print(f"DIFFERENT {vid}")
            for d in r["diffs"]:
                print("   ", d)
    print(json.dumps(counts, sort_keys=True))
    return 1 if counts.get("DIFFERENT") else 0


if __name__ == "__main__":
    sys.exit(main())
