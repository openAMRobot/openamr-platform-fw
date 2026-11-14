"""Checks the test vectors against the specification table with the reference model.

Test oracle for specification review. Not firmware. Not a safety function.
"""
import copy
import pathlib
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "tools" / "spec"))
import model  # noqa: E402

VECTORS = model.load_vectors()
SPEC = model.load_yaml(model.SPEC)


@pytest.mark.parametrize("path,vec", VECTORS, ids=[p.stem for p, _ in VECTORS])
def test_vector_consistent_with_table(path, vec):
    errs, _ = model.run_vector(vec, copy.deepcopy(SPEC))
    assert not errs, "\n".join(errs)


SM_VECTORS = [(p, v) for p, v in VECTORS if v.get("kind", "state_machine") == "state_machine"]


def _fired_and_visited():
    fired, visited = set(), set()

    def collect(vec):
        _, m = model.run_vector(vec, copy.deepcopy(SPEC))
        fired.update(m.fired)
        visited.update(m.visited)

    for _, vec in SM_VECTORS:
        if "cases" in vec:
            for case in vec["cases"]:
                collect({**{k: v for k, v in vec.items() if k != "cases"}, **case})
        else:
            collect(vec)
    return fired, visited


def test_table_is_complete():
    problems = model.StateMachineModel(copy.deepcopy(SPEC)).table_problems()
    assert not problems, "\n".join(problems)


def test_every_transition_exercised():
    fired, _ = _fired_and_visited()
    rows = {r["id"] for r in SPEC["transitions"]}
    assert rows - fired == set(), f"rows never exercised by any vector: {sorted(rows - fired)}"


def test_every_state_reachable():
    _, visited = _fired_and_visited()
    assert visited == {s["id"] for s in SPEC["states"]}


def test_host_tests_h1_to_h35_present():
    ids = {v["id"] for _, v in SM_VECTORS}
    missing = [f"h{i:02d}" for i in range(1, 36) if f"h{i:02d}" not in ids]
    assert not missing


def test_every_vector_documents_status_and_source():
    for path, vec in VECTORS:
        assert vec.get("status") in ("REV5", "ADDED", "CONFLICT", "PROPOSAL"), path.name
        if vec["status"] == "ADDED":
            assert vec.get("added_reason"), path.name
        assert vec.get("covers"), path.name


def test_conflicts_keep_both_sides():
    for c in SPEC["conflicts"]:
        assert c["side_a"]["source"] and c["side_b"]["source"], c["id"]


def _mutate_one_state(vec):
    """Change the first expected state of the vector to a different one."""
    mutated = copy.deepcopy(vec)
    steps = mutated["cases"][0]["steps"] if "cases" in mutated else mutated["steps"]
    for step in steps:
        exp = step.get("expect", {})
        if "state" in exp:
            exp["state"] = "RUN" if exp["state"] != "RUN" else "LOCKED"
            return mutated
    raise AssertionError("vector has no expected state")


@pytest.mark.parametrize("path,vec", SM_VECTORS, ids=[p.stem for p, _ in SM_VECTORS])
def test_mutation_is_detected(path, vec):
    """Mutation check: a copy with one expected state changed must fail against the model."""
    errs, _ = model.run_vector(_mutate_one_state(vec), copy.deepcopy(SPEC))
    assert errs, f"{path.name}: changing an expected state was not detected"


PERMIT_VECTORS = [(p, v) for p, v in VECTORS if v.get("kind") in ("permit_base", "permit_ctl017")]


def _iter_steps(vec):
    for case in vec.get("cases", [vec]):
        yield from case["steps"]


@pytest.mark.parametrize("path,vec", PERMIT_VECTORS, ids=[p.stem for p, _ in PERMIT_VECTORS])
def test_permit_mutation_is_detected(path, vec):
    """Flip one expected permit or lift state in a copy; the model must disagree."""
    mutated = copy.deepcopy(vec)
    for step in _iter_steps(mutated):
        exp = step.get("expect") or {}
        if "permit" in exp:
            exp["permit"] = not exp["permit"]
            break
        if "lift" in exp:
            exp["lift"] = "MOVING" if exp["lift"] == "IDLE" else "IDLE"
            break
    else:
        raise AssertionError("vector has no permit or lift expectation")
    errs, _ = model.run_vector(mutated)
    assert errs


def test_permit_parameters_are_named_and_marked():
    for path, vec in PERMIT_VECTORS:
        status = vec.get("param_status") or {}
        assert set(vec["params"]) == set(status), path.name


def test_permit_unconfigured_never_permits():
    base = model.BasePermitModel({})
    assert base.step(0.0, [])["permit"] is False
    assert base.reason == "NOT_CONFIGURED"
    ctl = model.CtlPermitModel({})
    out = ctl.step(0.0, [{"event": "producer_session", "params": {"session": "S"}},
                         {"event": "permit", "params": {"seq": 1, "value": True, "session": "S"}},
                         {"event": "cmd", "params": {"kind": "MOVE_TO_STOP"}}])
    assert out["lift"] == "IDLE" and out["reject"] == "NOT_CONFIGURED"


def test_base_permit_reasons_all_exercised():
    seen = set()
    for _, vec in PERMIT_VECTORS:
        if vec["kind"] != "permit_base":
            continue
        for step in _iter_steps(vec):
            r = (step.get("expect") or {}).get("reason")
            if r:
                seen.add(r)
    assert {"BASE_MOVING", "BASE_FEEDBACK_INVALID", "BASE_STOP_NOT_QUALIFIED", "ARM_STATUS_MISSING", "ARM_STATUS_STALE",
            "ARM_POSE_UNSAFE", "ARM_MOTION_ACTIVE", "ARM_CONTROLLER_FAULT", "BASE_FAULT", "CTL017_LINK_LOST", "OK"} <= seen
