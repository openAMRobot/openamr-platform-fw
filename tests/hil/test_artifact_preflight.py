"""Non-actuating regressions for the exact HIL workflow shell step.

Only a temporary logging flash.py and a failing Renode stub can execute here.
No device, physical flasher, CAN interface or firmware control code is used.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import yaml

ROOT = Path(__file__).resolve().parents[2]
SHA = "a" * 40
REPO = "openAMRobot/openamr-platform-fw"


def step(workflow, name):
    jobs = yaml.safe_load((ROOT / ".github/workflows" / workflow).read_text())["jobs"]
    return next(s["run"] for job in jobs.values() for s in job["steps"] if s.get("name") == name)


class ArtifactPreflight(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / "nightly/firmware").mkdir(parents=True)
        self.image = self.root / "nightly/firmware/controller-debug.hex"
        self.image.write_bytes(b":00000001FF\n")
        self.digest = hashlib.sha256(self.image.read_bytes()).hexdigest()
        self.record = {"schema_version": 1, "repository": REPO, "sha": SHA, "ref": "main",
                       "workflow": "Firmware nightly", "result": "PASS",
                       "checks": [{"name": "stm32-build-debug", "status": "PASS"}],
                       "artifacts": [{"path": "firmware/controller-debug.hex", "sha256": self.digest}]}
        helper_dir = self.root / "tools/hil"
        helper_dir.mkdir(parents=True)
        helper = ROOT / "tools/hil/verify_firmware.py"
        if helper.exists():
            (helper_dir / helper.name).symlink_to(helper)
        (helper_dir / "flash.py").write_text("from pathlib import Path\nPath('FLASH_STUB_CALLED').write_text('dummy only')\n")

    def run_step(self, expected_ok=False):
        (self.root / "nightly/ci-evidence.json").write_text(json.dumps(self.record))
        sums = self.root / "nightly/SHA256SUMS"
        if not sums.exists():
            sums.write_text(f"{self.digest}  controller-debug.hex\n")
        env = dict(os.environ, FIRMWARE_SHA=SHA, GITHUB_OUTPUT=str(self.root / "outputs"))
        result = subprocess.run(["bash", "-e", "-c", step("firmware-hil-bench.yml",
                                "Flash the STM32 Debug image recorded by the nightly")],
                                cwd=self.root, env=env, text=True, capture_output=True)
        invoked = (self.root / "FLASH_STUB_CALLED").exists()
        if expected_ok:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue(invoked)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertFalse(invoked, "mismatched evidence reached the logging flasher stub")

    def test_matching_artifact_reaches_only_the_stub(self):
        self.run_step(expected_ok=True)

    def test_different_source_sha_never_reaches_stub(self):
        self.record["sha"] = "b" * 40
        self.run_step()

    def test_different_repository_never_reaches_stub(self):
        self.record["repository"] = "other/firmware"
        self.run_step()

    def test_failed_build_never_reaches_stub(self):
        self.record["checks"][0]["status"] = "FAIL"
        self.run_step()

    def test_incomplete_evidence_never_reaches_stub(self):
        self.record["result"] = "INCOMPLETE"
        self.run_step()

    def test_unlisted_image_never_reaches_stub(self):
        self.record["artifacts"] = []
        self.run_step()

    def test_wrong_recorded_path_never_reaches_stub(self):
        self.record["artifacts"][0]["path"] = "elsewhere/controller-debug.hex"
        self.run_step()

    def test_traversal_path_never_reaches_stub(self):
        self.record["artifacts"][0]["path"] = "../firmware/controller-debug.hex"
        self.run_step()

    def test_wrong_recorded_digest_never_reaches_stub(self):
        self.record["artifacts"][0]["sha256"] = "0" * 64
        self.run_step()

    def test_altered_image_with_old_digests_never_reaches_stub(self):
        self.image.write_bytes(b"altered dummy")
        self.run_step()

    def test_ambiguous_images_never_reach_stub(self):
        other = self.image.with_name("other-debug.hex")
        other.write_bytes(self.image.read_bytes())
        self.record["artifacts"].append({"path": "firmware/other-debug.hex", "sha256": self.digest})
        self.run_step()

    def test_symlink_image_never_reaches_stub(self):
        payload = self.root / "external.hex"
        self.image.rename(payload)
        self.image.symlink_to(payload)
        self.run_step()

    def test_duplicate_checksum_never_reaches_stub(self):
        (self.root / "nightly/SHA256SUMS").write_text(f"{self.digest}  controller-debug.hex\n" * 2)
        self.run_step()


class NightlyBinding(unittest.TestCase):
    def test_firmware_checkout_uses_event_sha(self):
        data = yaml.safe_load((ROOT / ".github/workflows/firmware-nightly.yml").read_text())
        checkout = next(s for s in data["jobs"]["prepare"]["steps"] if s.get("with", {}).get("path") == "fw")
        self.assertEqual(checkout["with"]["ref"], "${{ github.sha }}")

    def test_renode_failure_survives_tee(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "stm32/firmware").mkdir(parents=True)
            (root / "stm32/firmware/dummy-debug.elf").touch()
            (root / "tools/ci").mkdir(parents=True)
            (root / "tools/ci/renode_boot.py").write_text("print('FAIL: dummy Renode error')\nraise SystemExit(7)\n")
            env = dict(os.environ, RUNNER_TEMP=tmp, GITHUB_STEP_SUMMARY=str(root / "summary"))
            result = subprocess.run(["bash", "-e", "-c", step("firmware-nightly.yml",
                                    "Boot the STM32 Debug ELF to main's idle loop")],
                                    cwd=root, env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
            self.assertIn("FAIL: dummy Renode error", (root / "renode.log").read_text())


if __name__ == "__main__":
    unittest.main()
