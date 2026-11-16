"""Static and same-tick checks of the specification table.

Test oracle for specification review. Not firmware. Not a safety function.
"""
import copy
import itertools
import pathlib
import re
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "spec"))
import model  # noqa: E402

SPEC = model.load_yaml(model.SPEC)
MATRIX = ROOT / "docs" / "safety" / "spec" / "requirements_matrix.md"
FINDINGS = {f["id"]: f for f in SPEC["findings"]}
ALL_TRUE = {"run_ok": True, "arming_ok": True, "qualified": True, "base_stopped": True, "charge_pre_ok": True}
STATES = [s["id"] for s in SPEC["states"]]


# ---- priority list -----------------------------------------------------------------------
def test_priority_names_are_declared_events():
    assert model.StateMachineModel(copy.deepcopy(SPEC)).priority_problems() == []


def test_priority_check_detects_the_version_1_name():
    spec = copy.deepcopy(SPEC)
    spec["priority"] = ["reset_hold_5s", "wcm_fault", "charge_interlock_fault", "others"]
    assert model.StateMachineModel(spec).priority_problems() == [
        "priority entry charge_interlock_fault is not a declared event"]


def _model(state, spec=None):
    """A model in `state`, reached by the shortest table path; every condition input true."""
    m = model.StateMachineModel(copy.deepcopy(spec or SPEC))
    m.step([{"event": "boot"}], dict(ALL_TRUE))
    m.step([{"event": "jetson_stop_release"}])
    if state in ("RUN", "INHIBITED"):
        m.step([{"event": "reset_short"}])
    if state == "INHIBITED":
        m.step([{"event": "ui_stop"}])
    if state in ("CHARGING", "CHARGER_FAULT"):
        m.step([{"event": "charge_cmd"}])
    if state == "CHARGER_FAULT":
        m.step([{"event": "wcm_fault"}])
    assert m.state == state
    return m


# Events that charge_interlock outranks and that are applicable in CHARGING.
OUTRANKED = [e for e, meta in SPEC["events"].items()
             if not meta.get("internal") and e not in SPEC["priority"] and e != "boot"
             and not any(n["event"] == e and "CHARGING" in n["states"] for n in SPEC["not_applicable"])]


@pytest.mark.parametrize("other", OUTRANKED)
@pytest.mark.parametrize("interlock_first", [True, False], ids=["interlock_first", "interlock_last"])
def test_same_tick_charge_interlock_ranks_before_others(other, interlock_first):
    """In CHARGING, charge_interlock is applied before every 'others' event of the same tick."""
    m = _model("CHARGING")
    evs = [{"event": "charge_interlock"}, {"event": other}]
    st = m.step(evs if interlock_first else evs[::-1])
    assert st.rows[0] == "K1", st.rows
    assert m.latches["interlock_latched"] is True
    assert st.reason_mask[0] == "CHARGE_INTERLOCK" and st.first_reason == "CHARGE_INTERLOCK"


@pytest.mark.parametrize("interlock_first", [True, False], ids=["interlock_first", "interlock_last"])
def test_same_tick_wcm_fault_ranks_before_charge_interlock(interlock_first):
    m = _model("CHARGING")
    evs = [{"event": "charge_interlock"}, {"event": "wcm_fault"}]
    st = m.step(evs if interlock_first else evs[::-1])
    assert st.rows == ["W1", "K2"]
    assert st.state == "CHARGER_FAULT" and st.first_reason == "CHARGER_FAULT"
    assert st.reason_mask == ["CHARGER_FAULT", "CHARGE_INTERLOCK"]
    assert m.latches["wcm_latched"] and m.latches["interlock_latched"]


def test_same_tick_reset_hold_before_charge_interlock_is_open_finding_f2():
    assert FINDINGS["F2"]["status"] == "open"
    m = _model("CHARGING")
    with pytest.raises(model.SpecError, match="not applicable in LOCKED"):
        m.step([{"event": "charge_interlock"}, {"event": "reset_hold_5s"}])


# ---- tick semantics ------------------------------------------------------------------------
def test_tick_semantics_stated():
    ts = SPEC["tick_semantics"]
    for key in ("order", "application", "matching", "applicability", "outputs", "order_dependence"):
        assert ts.get(key), key
    assert "current state" in ts["applicability"]


def test_applicability_uses_the_same_state_as_row_matching():
    """An event raised after an earlier event of the tick changed the state is judged on the
    new state for both applicability and row matching: cmd_stale applies in RUN only."""
    m = _model("RUN")
    with pytest.raises(model.SpecError, match="cmd_stale is not applicable in LOCKED"):
        m.step([{"event": "estop_diag"}, {"event": "cmd_stale"}])
    m = _model("RUN")
    st = m.step([{"event": "cmd_stale"}, {"event": "estop_diag"}])
    assert st.rows == ["C1", "E1"]


# ---- permutations of E-stop, charger fault, reset and communication loss --------------------
def _outcome(m, st):
    snap = model._sm_snapshot(m, st)
    return {"state": snap["state"], "outputs": snap["outputs"], "motion_admissible": snap["motion_admissible"],
            "latches": snap["latches"], "reason_mask": sorted(st.reason_mask), "first_reason": st.first_reason}


def _run(state, events):
    m = _model(state)
    # Consistent inputs: an active E-stop diagnostic means run_ok is false (inputs, run_ok).
    st = m.step([{"event": e} for e in events], {"run_ok": "estop_diag" not in events})
    return _outcome(m, st)


PERM_CASES = [(state, reset, sub)
              for reset in ("reset_short", "reset_hold_5s")
              for state in STATES
              for n in range(2, 5)
              for sub in itertools.combinations(["estop_diag", "wcm_fault", reset, "session_lost"], n)]


@pytest.mark.parametrize("state,reset,sub", PERM_CASES,
                         ids=[f"{s}-{'+'.join(sub)}" for s, _, sub in PERM_CASES])
def test_permutations_same_safe_outcome(state, reset, sub):
    results = [_run(state, list(p)) for p in itertools.permutations(sub)]
    first = results[0]
    for r in results[1:]:
        for key in ("state", "outputs", "motion_admissible"):
            assert r[key] == first[key], (key, r[key], first[key])
    if {"estop_diag", "session_lost", "reset_hold_5s"} & set(sub):
        assert first["state"] != "RUN" and first["outputs"]["motor"] == "ZERO"
        assert first["motion_admissible"] is False
    if state == "CHARGING" and "wcm_fault" in sub and "reset_hold_5s" not in sub:
        assert first["state"] == "CHARGER_FAULT" and first["latches"]["wcm_latched"] is True
    # Any other difference between orders must come from a pair declared in finding F5.
    declared = [set(d["pair"]) for d in FINDINGS["F5"]["order_dependent"]]
    if any(r != first for r in results):
        assert any(d <= set(sub) for d in declared), sub


def test_order_dependent_pairs_match_finding_f5():
    found = {}
    for reset in ("reset_short", "reset_hold_5s"):
        for state in STATES:
            for pair in itertools.combinations(["estop_diag", "wcm_fault", reset, "session_lost"], 2):
                a, b = _run(state, list(pair)), _run(state, list(pair[::-1]))
                fields = {k for k in a if a[k] != b[k]}
                if fields:
                    found.setdefault(tuple(sorted(pair)), set()).update(fields)
    declared = {tuple(sorted(d["pair"])): set(d["fields"]) for d in FINDINGS["F5"]["order_dependent"]}
    assert found == declared


# ---- static row analysis -----------------------------------------------------------------------
def test_static_row_analysis_matches_findings():
    a = model.row_analysis(copy.deepcopy(SPEC))
    assert a["no_row"] == []
    assert [list(p) for p in a["overlaps"]] == FINDINGS["F3"]["overlaps"]
    assert a["unreachable"] == FINDINGS["F4"]["unreachable_rows"]
    assert a["unreachable_states"] == FINDINGS["F4"]["unreachable_states"]


def test_static_check_detects_a_shadowed_row():
    spec = copy.deepcopy(SPEC)
    i = next(i for i, r in enumerate(spec["transitions"]) if r["id"] == "U1")
    spec["transitions"].insert(i + 1, {**spec["transitions"][i], "id": "U1_COPY"})
    assert "U1_COPY" in model.row_analysis(spec)["unreachable"]


def test_static_check_detects_an_unreachable_state():
    spec = copy.deepcopy(SPEC)
    spec["states"].append({"id": "ORPHAN", "motion": "inhibited", "lamp": "ON", "charge_request": "OFF", "source": "test"})
    assert model.row_analysis(spec)["unreachable_states"] == ["ORPHAN"]


def test_static_check_detects_a_partial_overlap():
    spec = copy.deepcopy(SPEC)
    i = next(i for i, r in enumerate(spec["transitions"]) if r["id"] == "P3")
    spec["transitions"].insert(i, {"id": "P0", "from": "LOCKED", "event": "reset_short", "guard": {"run_ok": True}, "source": "test"})
    assert ("P0", "P3") in model.row_analysis(spec)["overlaps"]


# ---- version and conflicts ---------------------------------------------------------------------
def test_version_field_present():
    assert isinstance(SPEC.get("version"), int) and SPEC["version"] >= 2


def test_conflicts_c1_to_c8_listed_open():
    ids = [f"C{i}" for i in range(1, 9)]
    assert SPEC["open_conflicts"] == ids
    assert [c["id"] for c in SPEC["conflicts"]] == ids
    assert all(c["status"] == "open" for c in SPEC["conflicts"])


def test_findings_are_open():
    assert all(f["status"] == "open" for f in SPEC["findings"])


# ---- requirement to vector matrix ---------------------------------------------------------------
def _matrix_rows():
    rows = []
    for line in MATRIX.read_text(encoding="utf-8").splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) == 5 and cells[0] not in ("Requirement", "---"):
            rows.append(cells)
    return rows


def _covers():
    cov = {}
    for _, vec in model.load_vectors():
        for case in [vec] + list(vec.get("cases") or []):
            for c in case.get("covers") or []:
                cov.setdefault(str(c), set()).add(vec["id"])
    return cov


ALIASES = {"4 (same-tick priority)": ["4"]}
NOT_SOURCE = {"All", "B9", "D14"}


def test_matrix_vectors_exist_and_match_covers():
    ids = {vec["id"] for _, vec in model.load_vectors()}
    cov = _covers()
    rows = _matrix_rows()
    assert rows
    for req, _doc, _kind, vectors, status in rows:
        listed = set() if vectors == "-" else {v.strip() for v in vectors.split(",")}
        assert listed <= ids, (req, listed - ids)
        expected = set(cov.get(req, set()))
        for alias in ALIASES.get(req, []):
            expected |= cov.get(alias, set())
        assert listed == expected, req
        assert status == ("traced" if listed else "not traced"), req


def test_matrix_has_a_row_for_every_cited_requirement():
    reqs = {r[0] for r in _matrix_rows()}
    known = reqs | {a for v in ALIASES.values() for a in v} | NOT_SOURCE
    assert set(_covers()) - known == set()
    text = model.SPEC.read_text(encoding="utf-8")
    cited = set()
    for src in re.findall(r'source: "([^"]*)"', text):
        cited.update(re.findall(r"\b(?:T\d+[ab]?|H\d+|R\d+|\d\.\d)\b", src))
    assert cited - reqs == set()
