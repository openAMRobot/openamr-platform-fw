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
