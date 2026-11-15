# SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image.
"""Runs every vector of tests/spec against the Python model and the C simulation reference.
Any difference fails and is listed. Needs SIMREF_ADAPTER_LIB (the build sets it in ctest)."""
import copy
import ctypes
import pathlib
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import simref_adapter as ad  # noqa: E402

LIB = ad.load_lib()
REPORT = ad.run_all(LIB)
SPEC = ad.model.load_yaml(ad.model.SPEC)


@pytest.mark.parametrize("vid", sorted(REPORT))
def test_vector_same_on_model_and_c(vid):
    r = REPORT[vid]
    if r["result"] == "OUT_OF_SCOPE":
        assert r["kind"] in ad.OUT_OF_SCOPE_KINDS
        return
    assert r["result"] == "SAME", "\n".join(r["diffs"])


def test_every_vector_file_is_covered():
    ids = set()
    for _, vec in ad.model.load_vectors():
        ids.update(v["id"] for v in ad.expand(vec))
    assert ids == set(REPORT)
    assert len(ids) > 100


def test_only_ctl017_gate_vectors_are_out_of_scope():
    out = {vid for vid, r in REPORT.items() if r["result"] == "OUT_OF_SCOPE"}
    assert out and all(vid.startswith("permit_ctl_") for vid in out)
    compared = [vid for vid, r in REPORT.items() if r["result"] != "OUT_OF_SCOPE"]
    assert any(v.startswith("h") for v in compared) and any(v.startswith("permit_base_") for v in compared)


def test_adapter_reports_a_difference():
    """Negative control: a model table where V3 leaves INHIBITED for LOCKED (C4 side b) must
    differ from the C build that follows side a."""
    spec = copy.deepcopy(SPEC)
    for row in spec["transitions"]:
        if row["id"] == "V3":
            row["to"] = "LOCKED"
    vec = next(v for _, v in ad.model.load_vectors() if v["id"] == "h02")
    diffs = ad.compare_sm(vec, LIB, spec)
    assert any("state" in d for d in diffs), diffs


def test_permit_adapter_reports_a_difference():
    vec = next(v for _, v in ad.model.load_vectors() if v["id"].startswith("permit_base_01"))
    vec = copy.deepcopy(ad.expand(vec)[0])
    vec["params"]["stop_hold_s"] = 0.25  # the model gets a shorter hold than the vector expects
    diffs = ad.compare_permit(vec, LIB)
    assert diffs


def test_c_table_rows_match_yaml():
    lib = LIB
    lib.simref_sm_row_count.restype = ctypes.c_size_t
    lib.simref_sm_row_id.restype = ctypes.c_char_p
    lib.simref_sm_row_id.argtypes = [ctypes.c_size_t]
    lib.simref_sm_row_to.argtypes = [ctypes.c_size_t]
    lib.simref_sm_row_from_mask.restype = ctypes.c_uint32
    lib.simref_sm_row_from_mask.argtypes = [ctypes.c_size_t]
    lib.simref_sm_row_event_mask.restype = ctypes.c_uint32
    lib.simref_sm_row_event_mask.argtypes = [ctypes.c_size_t]
    states = [s["id"] for s in SPEC["states"]]
    rows = SPEC["transitions"]
    assert lib.simref_sm_row_count() == len(rows)
    for i, r in enumerate(rows):
        assert lib.simref_sm_row_id(i).decode() == r["id"]
        frm = states if r["from"] == "*" else ad.model._as_list(r["from"])
        assert lib.simref_sm_row_from_mask(i) == sum(1 << states.index(s) for s in frm), r["id"]
        evs = ad.model._as_list(r["event"])
        assert lib.simref_sm_row_event_mask(i) == sum(1 << LIB.event_ids[e] for e in evs), r["id"]
        to = lib.simref_sm_row_to(i)
        assert to == (states.index(r["to"]) if "to" in r else -1), r["id"]


def test_spec_version_is_2():
    assert SPEC["version"] == 2
