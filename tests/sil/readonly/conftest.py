"""Fixtures of the read-only chain proof. The host gate is imported from a
checkout of platform-sw PR #137 outside this repository (OAMR_GATE_DIR)."""
import importlib.util
import os

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))


@pytest.fixture(scope="session")
def sil_bin():
    path = os.environ.get("OAMR_SIL_BIN", os.path.join(REPO, "build", "sil-readonly", "oamr_sil_readonly"))
    if not os.path.exists(path):
        pytest.fail("build the chain first: cmake -S tests/sil/readonly -B build/sil-readonly && "
                    "cmake --build build/sil-readonly (missing %s)" % path)
    return path


@pytest.fixture(scope="session")
def gate_core():
    gate_dir = os.environ.get("OAMR_GATE_DIR")
    if not gate_dir:
        pytest.skip("OAMR_GATE_DIR not set: point it at the openamrobot_drivers package "
                    "directory of a platform-sw PR #137 checkout")
    path = os.path.join(gate_dir, "base_status_core.py")
    if os.path.commonpath([os.path.abspath(path), REPO]) == REPO:
        pytest.fail("base_status_core.py must come from a checkout outside this repository")
    spec = importlib.util.spec_from_file_location("base_status_core", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod
