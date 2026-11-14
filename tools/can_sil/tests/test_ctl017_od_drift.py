"""Drift test: the CTL-017 emulator must follow the object dictionary document byte for byte.

Reference: the generated object table of docs/ctl017_object_dictionary.md in openamr-upperbody-fw
PR #15 (proposal v1), kept as a snapshot in tests/data/ctl017_od_table.md. The test compares, for
every row of the table: index, sub-index, type (size and signedness), access, default; and it
fails on any object the emulator has that the table does not, or the other way round. It also
compares the committed ctl017_od_v1.py with a fresh generation from the snapshot, and the PDO
layouts the emulator puts on the wire with the mapping objects of the table.

When the owner changes the dictionary: replace the table in the snapshot, regenerate
ctl017_od_v1.py with gen_ctl017_od.py, then make the emulator follow. This test goes red
until all three agree. Set CTL017_OD_DOC to a path of the live document to compare it too.
"""
import os
import re
import struct

import pytest

import ctl017_emulator as c
import ctl017_od_v1
import gen_ctl017_od as gen
from canopen_common import ObjectDict

HERE = os.path.dirname(__file__)
SNAPSHOT = os.path.join(HERE, "data", "ctl017_od_table.md")
EXPECTED_ROWS = 126

SIZES = {"UNSIGNED8": (1, False), "BOOLEAN": (1, False), "UNSIGNED16": (2, False),
         "UNSIGNED32": (4, False), "INTEGER16": (2, True), "INTEGER32": (4, True)}
ACCESS = {"const": "ro", "ro": "ro", "rw": "rw", "rw (PRE-OP only)": "preop",
          "rww (RPDO only)": "local"}


def read_rows(text):
    """Independent of gen_ctl017_od.parse: a plain split of every table line."""
    rows = []
    for line in text.splitlines():
        if not line.startswith("| 0x"):
            continue
        cells = [x.strip() for x in line.strip().strip("|").split("|")]
        index, sub, name, typ, access, pdo, default = cells
        rows.append((int(index, 16), int(sub), name, typ, access, pdo == "yes", int(default, 0)))
    return rows


def diff_od(rows, od):
    """Differences between the document rows and an ObjectDict. Empty list means no drift."""
    problems = []
    seen = set()
    for index, sub, name, typ, access, _pdo, default in rows:
        key = (index, sub)
        seen.add(key)
        entry = od.entries.get(key)
        if entry is None:
            problems.append("missing %04Xh:%02X %s" % (index, sub, name))
            continue
        size, signed = SIZES[typ]
        for what, got, want in (("size", entry["size"], size), ("signed", entry["signed"], signed),
                                ("access", entry["rw"], ACCESS[access]),
                                ("default", entry["value"], default)):
            if got != want:
                problems.append("%04Xh:%02X %s: emulator %r, document %r (%s)" % (
                    index, sub, what, got, want, name))
    for key in sorted(set(od.entries) - seen):
        problems.append("extra %04Xh:%02X not in the document" % key)
    return problems


@pytest.fixture(scope="module")
def doc_rows():
    return read_rows(open(SNAPSHOT, encoding="utf-8").read())


def test_snapshot_is_the_whole_table(doc_rows):
    assert len(doc_rows) == EXPECTED_ROWS
    assert len({(r[0], r[1]) for r in doc_rows}) == EXPECTED_ROWS  # no duplicate object


def test_generator_and_independent_reader_agree_on_the_snapshot(doc_rows):
    assert gen.parse(open(SNAPSHOT, encoding="utf-8").read()) == doc_rows


def test_committed_od_module_equals_generation_from_the_snapshot(doc_rows):
    assert ctl017_od_v1.OD_V1 == doc_rows


def test_built_dictionary_equals_the_document(doc_rows):
    assert diff_od(doc_rows, c.build_od()) == []


LIVE_OBJECTS = (0x2000, 0x2001, 0x2002, 0x2300)  # carry model state, not a default


def test_running_emulator_dictionary_equals_the_document(doc_rows, make_bus):
    """The emulator's own dictionary right after (re)initialisation, before any frame. Live
    status objects may differ in value only (model state); everything else must be equal."""
    n = c.Ctl017Emulator(make_bus(), start_pos_mm=c.SENSOR_POS_MM[1])
    problems = [p for p in diff_od(doc_rows, n.od)
                if not (" default:" in p and int(p[:4], 16) in LIVE_OBJECTS)]
    assert problems == []


def test_detector_catches_each_kind_of_drift(doc_rows):
    base = c.build_od()
    assert diff_od(doc_rows, base) == []
    mutations = {
        "default": lambda od: od.set(0x1017, 0, 101),
        "size": lambda od: od.entries[(0x2000, 3)].update(size=4),
        "signed": lambda od: od.entries[(0x2001, 4)].update(signed=False),
        "access": lambda od: od.entries[(0x2010, 1)].update(rw="rw"),
        "extra": lambda od: od.add(0x2012, 0, 1, 0),
        "missing": lambda od: od.entries.pop((0x2101, 32)),
    }
    for kind, mutate in mutations.items():
        od = c.build_od()
        mutate(od)
        problems = diff_od(doc_rows, od)
        assert problems, "drift of kind %s not detected" % kind
    # and a change on the document side
    changed = [r for r in doc_rows]
    i = [k for k, r in enumerate(changed) if (r[0], r[1]) == (0x1400, 5)][0]
    changed[i] = changed[i][:6] + (250,)
    assert diff_od(changed, c.build_od())


def pdo_fields(doc_rows, mapping_index):
    rows = {(r[0], r[1]): r for r in doc_rows}
    count = rows[(mapping_index, 0)][6]
    fields = []
    for sub in range(1, count + 1):
        value = rows[(mapping_index, sub)][6]
        fields.append((value >> 16, (value >> 8) & 0xFF, value & 0xFF))  # index, sub, bits
    return fields


@pytest.mark.parametrize("mapping,cob_obj,frame_fn,size_ok", [
    (0x1A00, c.TPDO1, "status1", 8), (0x1A01, c.TPDO2, "status2", 8)])
def test_tpdo_wire_layout_matches_the_mapping_objects(doc_rows, make_bus, mapping, cob_obj,
                                                      frame_fn, size_ok):
    n = c.Ctl017Emulator(make_bus(), start_pos_mm=c.SENSOR_POS_MM[1])
    fields = pdo_fields(doc_rows, mapping)
    assert sum(bits for _i, _s, bits in fields) == size_ok * 8
    n.counter = 5
    frame = n.status1(n.counter) if frame_fn == "status1" else n.status2()
    assert len(frame) == size_ok
    fmt = {8: "B", 16: "H"}
    rows = {(r[0], r[1]): r for r in doc_rows}
    pos = 0
    for index, sub, bits in fields:
        typ = rows[(index, sub)][3]
        size, signed = SIZES[typ]
        assert size * 8 == bits, "mapping of %04Xh:%02X disagrees with its type" % (index, sub)
        raw = bytes(frame[pos:pos + size])
        value = struct.unpack("<" + (fmt[bits].lower() if signed else fmt[bits]), raw)[0]
        n._update_od()
        assert n.od.get(index, sub) == value, "%04Xh:%02X is not at byte %d" % (index, sub, pos)
        pos += size


def test_rpdo_layout_and_cob_ids_match_the_table(doc_rows):
    rows = {(r[0], r[1]): r for r in doc_rows}
    assert rows[(0x1400, 1)][6] == c.RPDO1 and rows[(0x1401, 1)][6] == c.RPDO2
    assert rows[(0x1800, 1)][6] & 0x7FF == c.TPDO1 and rows[(0x1801, 1)][6] & 0x7FF == c.TPDO2
    assert rows[(0x1014, 0)][6] == c.EMCY
    assert [b for _i, _s, b in pdo_fields(doc_rows, 0x1600)] == [8, 8, 8]  # 3 bytes, no permit
    assert [b for _i, _s, b in pdo_fields(doc_rows, 0x1601)] == [8, 8, 8]
    assert [(i, s) for i, s, _b in pdo_fields(doc_rows, 0x1600)] == [(0x2010, 1), (0x2010, 2),
                                                                     (0x2010, 3)]


def test_every_mapped_object_is_marked_pdo_mappable(doc_rows):
    mappable = {(r[0], r[1]) for r in doc_rows if r[5]}
    for mapping in (0x1600, 0x1601, 0x1A00, 0x1A01):
        for index, sub, _bits in pdo_fields(doc_rows, mapping):
            assert (index, sub) in mappable


@pytest.mark.skipif(not os.environ.get("CTL017_OD_DOC"), reason="CTL017_OD_DOC not set")
def test_snapshot_equals_the_live_document():
    live = open(os.environ["CTL017_OD_DOC"], encoding="utf-8").read()
    assert gen.parse(live) == read_rows(open(SNAPSHOT, encoding="utf-8").read())


def test_tpdo_fields_carry_the_model_value_named_in_the_table(doc_rows, make_bus):
    """The mapping test above reads the dictionary back from the frame, so it cannot see two
    fields swapped. This one sets distinct model values and checks each byte by the field name
    given in the table."""
    n = c.Ctl017Emulator(make_bus(), start_pos_mm=c.SENSOR_POS_MM[1])
    n.spare_in[1] = True
    n.set_estop2(True)
    n.target_stop = 2
    n.current_ma = 1234  # tick() would recompute it from the model, status2() reads it
    n.velocity = -77
    n.faults = 0x0044
    n.counter = 9
    names = {(r[0], r[1]): r[2] for r in doc_rows}
    f2 = {names[(i, s)].split(" (")[0]: v for (i, s, _b), v in zip(
        pdo_fields(doc_rows, 0x1A01), struct.unpack("<BBHhBB", n.status2()))}
    assert f2["inputs"] == n.inputs_byte() and n.inputs_byte() & 0x08
    assert f2["target_stop_id"] == 2
    assert f2["motor current"] == 1234
    assert f2["velocity"] == -77
    assert f2["faults F01..F08"] == 0x44
    assert f2["aux flags"] == n.aux_flags()
    f1 = {names[(i, s)].split(" (")[0]: v for (i, s, _b), v in zip(
        pdo_fields(doc_rows, 0x1A00), struct.unpack("<BBHBBBB", n.status1(9)))}
    assert f1["state"] == n.state and f1["flags"] == n.flags(n._now_ms())
    assert f1["position"] == n._pos_raw() and f1["status_counter"] == 9
    assert f1["last_seq"] == n.last_seq and f1["cmd_result"] == n.cmd_result == 0xFF
