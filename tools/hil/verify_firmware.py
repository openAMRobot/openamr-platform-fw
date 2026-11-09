#!/usr/bin/env python3
"""Validate downloaded nightly provenance before any physical flash step.

This command only reads metadata/image bytes and writes the selected path/digest.
It never invokes a flasher or opens a device. The manual HIL gate remains separate.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import sys


class EvidenceError(ValueError):
    """Downloaded evidence does not identify the requested firmware."""


def verify(bundle, repository, sha):
    if not re.fullmatch(r"[0-9a-f]{40}", sha):
        raise EvidenceError("requested firmware SHA must be 40 lowercase hex characters")
    bundle = Path(bundle).resolve(strict=True)
    evidence = bundle / "ci-evidence.json"
    if evidence.is_symlink():
        raise EvidenceError("evidence must not be a symlink")
    record = json.loads(evidence.read_text())
    if (record.get("schema_version"), record.get("repository"), record.get("sha"), record.get("ref")) != (
            1, repository, sha, "main"):
        raise EvidenceError("nightly repository, SHA, ref or schema does not match the requested build")
    if record.get("workflow") != "Firmware nightly" or record.get("result") != "PASS":
        raise EvidenceError("nightly must be a successful Firmware nightly evidence record")
    builds = [c for c in record.get("checks", []) if c.get("name") == "stm32-build-debug"]
    if len(builds) != 1 or builds[0].get("status") != "PASS":
        raise EvidenceError("exactly one passing stm32-build-debug check is required")
    candidates = []
    for artifact in record.get("artifacts", []):
        path = PurePosixPath(artifact["path"])
        if path.is_absolute() or ".." in path.parts or "\\" in artifact["path"]:
            raise EvidenceError("artifact paths must be relative and stay inside the bundle")
        if path.parent == PurePosixPath("firmware") and path.name.endswith("-debug.hex"):
            candidates.append((path, artifact["sha256"]))
    if len(candidates) != 1:
        raise EvidenceError("exactly one recorded STM32 Debug HEX image is required")
    path, digest = candidates[0]
    if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
        raise EvidenceError("recorded image digest must be SHA-256")
    image = bundle.joinpath(*path.parts)
    if any(bundle.joinpath(*path.parts[:i]).is_symlink() for i in range(1, len(path.parts) + 1)):
        raise EvidenceError("image path must not contain symlinks")
    if not image.is_file() or not image.resolve().is_relative_to(bundle):
        raise EvidenceError("recorded image is missing or outside the bundle")
    images = list((bundle / "firmware").glob("*-debug.hex"))
    if images != [image]:
        raise EvidenceError("bundle contains unrecorded or ambiguous Debug images")
    sums = bundle / "SHA256SUMS"
    if sums.is_symlink():
        raise EvidenceError("checksum manifest must not be a symlink")
    matches = []
    for line in sums.read_text().splitlines():
        fields = line.split(maxsplit=1)
        if len(fields) == 2 and fields[1].lstrip(" *") == path.name:
            matches.append(fields[0])
    if matches != [digest] or hashlib.sha256(image.read_bytes()).hexdigest() != digest:
        raise EvidenceError("image bytes, evidence digest and unique checksum entry must all match")
    return {"image": str(image), "sha256": digest, "firmware_sha": sha, "repository": repository}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--sha", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args(argv)
    try:
        result = verify(args.bundle, args.repository, args.sha)
    except (EvidenceError, OSError, ValueError, KeyError, TypeError, AttributeError) as exc:
        print(f"REFUSED: firmware evidence preflight: {exc}", file=sys.stderr)
        return 1
    Path(args.output).write_text(json.dumps(result, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
