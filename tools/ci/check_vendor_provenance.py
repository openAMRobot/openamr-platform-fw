#!/usr/bin/env python3
"""Check vendored STMicroelectronics files against the matching upstream release.

For each STM32 project (boards/stm32/<name>/ with a .ioc):
  1. read the HAL version from Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal.c
     (__STM32H7xx_HAL_VERSION_MAIN/SUB1/SUB2), the CMSIS device version from
     Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h7xx.h and the CubeMX package from the .ioc
     (ProjectManager.FirmwarePackage, e.g. "STM32Cube FW_H7 V1.13.0");
  2. resolve the STM32CubeH7 tag of that package and the commits it pins for the HAL driver,
     and the CMSIS device (git submodules), and fail when the vendored HAL or
     CMSIS version is not the one that package pins;
  3. compare every vendored file, after normalising line endings, with:
       Drivers/STM32H7xx_HAL_Driver/**     github.com/STMicroelectronics/stm32h7xx-hal-driver  tag v<HAL>
       Drivers/CMSIS/Device/ST/STM32H7xx/** github.com/STMicroelectronics/cmsis-device-h7     tag v<CMSIS>
       Drivers/CMSIS/Include/**, Drivers/CMSIS/LICENSE.txt   STM32CubeH7 at the package tag
     Any difference, or a vendored file that upstream does not have, is an error naming the file.

Reported as NOT CHECKED (warning), never PASS: files outside the HAL driver and CMSIS (for
example Drivers/BSP, which the ST zip package ships with a version line GitHub does not have),
and the component LICENSE.txt files that exist only in the ST zip package (the GitHub
repositories carry LICENSE.md instead).

Upstream repositories are fetched with git (shallow, read-only) into --cache, or taken from
local directories given with --mirror NAME=DIR (tests). Nothing in the repository is changed.
Exit 1 on any error.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

GITHUB = "https://github.com/STMicroelectronics/"
CUBE = "STM32CubeH7"
HAL = "stm32h7xx-hal-driver"
CMSIS = "cmsis-device-h7"
SUBMODULES = {HAL: "Drivers/STM32H7xx_HAL_Driver", CMSIS: "Drivers/CMSIS/Device/ST/STM32H7xx"}
# vendored prefix -> (upstream repository, path inside it); first match wins
LAYOUT = [
    ("Drivers/STM32H7xx_HAL_Driver/", HAL, ""),
    ("Drivers/CMSIS/Device/ST/STM32H7xx/", CMSIS, ""),
    ("Drivers/CMSIS/", CUBE, "Drivers/CMSIS/"),
]


def version_from(path, prefix):
    text = Path(path).read_text(encoding="utf-8", errors="replace")
    parts = []
    for field in ("MAIN", "SUB1", "SUB2"):
        m = re.search(rf"#define\s+{prefix}_{field}\s+\(\s*0x([0-9A-Fa-f]+)U?L?\s*\)", text)
        if not m:
            raise ValueError(f"{path}: {prefix}_{field} not found")
        parts.append(str(int(m.group(1), 16)))
    return ".".join(parts)


def package_from(ioc):
    m = re.search(r"^ProjectManager\.FirmwarePackage=STM32Cube FW_H7 V(\d+\.\d+\.\d+)\s*$",
                  Path(ioc).read_text(encoding="utf-8", errors="replace"), re.M)
    if not m:
        raise ValueError(f"{ioc}: ProjectManager.FirmwarePackage is not an STM32Cube FW_H7 package")
    return m.group(1)


def versions(project):
    project = Path(project)
    iocs = sorted(project.glob("*.ioc"))
    if len(iocs) != 1:
        raise ValueError(f"{project}: expected one .ioc, found {len(iocs)}")
    return {
        "package": package_from(iocs[0]),
        "hal": version_from(project / "Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal.c", "__STM32H7xx_HAL_VERSION"),
        "cmsis": version_from(project / "Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h7xx.h",
                              "__STM32H7xx_CMSIS_DEVICE_VERSION"),
    }


def git(*args, cwd=None):
    return subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True, text=True).stdout.strip()


def fetch(cache, name, ref, paths=None):
    """Shallow, read-only checkout of STMicroelectronics/<name> at a tag or commit."""
    dest = Path(cache) / f"{name}@{ref}"
    if (dest / ".git").is_dir():
        return dest
    dest.mkdir(parents=True, exist_ok=True)
    git("init", "-q", str(dest))
    git("remote", "add", "origin", GITHUB + name, cwd=dest)
    git("fetch", "-q", "--depth", "1", "--filter=blob:none", "origin", ref, cwd=dest)
    if paths:
        git("sparse-checkout", "set", "--no-cone", *paths, cwd=dest)
    git("checkout", "-q", "FETCH_HEAD", cwd=dest)
    return dest


def peeled(name, tag):
    out = git("ls-remote", GITHUB + name, f"refs/tags/{tag}", f"refs/tags/{tag}^{{}}")
    refs = dict(reversed(line.split("\t")) for line in out.splitlines())
    return refs.get(f"refs/tags/{tag}^{{}}") or refs.get(f"refs/tags/{tag}")


def submodule_commits(cube_dir):
    out = {}
    for name, path in SUBMODULES.items():
        line = git("ls-tree", "HEAD", path, cwd=cube_dir)
        out[name] = line.split()[2] if line else None
    return out


def normalise(data):
    return data.replace(b"\r\n", b"\n")


PACKAGE_ONLY = {"Drivers/STM32H7xx_HAL_Driver/LICENSE.txt", "Drivers/CMSIS/Device/ST/STM32H7xx/LICENSE.txt"}


def compare(project, roots):
    """roots maps repository name -> local directory; returns (errors, not_checked, counts)."""
    project = Path(project)
    errors, not_checked, counts = [], [], {}
    drivers = project / "Drivers"
    for path in sorted(p for p in drivers.rglob("*") if p.is_file()):
        rel = path.relative_to(project).as_posix()
        for prefix, repo, sub in LAYOUT:
            if rel.startswith(prefix):
                break
        else:
            not_checked.append(f"{rel}: outside the HAL driver and CMSIS")
            continue
        upstream = Path(roots[repo]) / (sub + rel[len(prefix):])
        if rel in PACKAGE_ONLY and not upstream.is_file():
            not_checked.append(f"{rel}: only in the ST zip package, not in {repo}")
            continue
        counts[repo] = counts.get(repo, 0) + 1
        if not upstream.is_file():
            errors.append(f"{rel}: not in {repo}")
        elif normalise(path.read_bytes()) != normalise(upstream.read_bytes()):
            errors.append(f"{rel}: differs from {repo}")
    return errors, not_checked, counts


def check_project(project, cache, mirrors):
    v = versions(project)
    print(f"{project}: STM32Cube FW_H7 V{v['package']}, HAL {v['hal']}, CMSIS device {v['cmsis']}")
    errors = []
    if CUBE in mirrors:
        roots = dict(mirrors)
    else:
        cube = fetch(cache, CUBE, f"v{v['package']}", ["/Drivers/CMSIS/Include/", "/Drivers/CMSIS/LICENSE.txt"])
        pins = submodule_commits(cube)
        for name, ver in ((HAL, v["hal"]), (CMSIS, v["cmsis"])):
            tag_commit = peeled(name, f"v{ver}")
            if tag_commit != pins[name]:
                errors.append(f"{name} v{ver} ({tag_commit}) is not the version STM32Cube FW_H7 V{v['package']} "
                              f"pins ({pins[name]})")
        roots = {CUBE: cube, HAL: fetch(cache, HAL, f"v{v['hal']}"), CMSIS: fetch(cache, CMSIS, f"v{v['cmsis']}")}
        roots.update(mirrors)
    diff, not_checked, counts = compare(project, roots)
    errors += diff
    for repo, n in sorted(counts.items()):
        print(f"  compared {n} files with {repo}")
    for item in not_checked:
        print(f"::warning title=vendor provenance::NOT CHECKED: {project}/{item}")
    return errors


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("projects", nargs="*", help="STM32 project folders (default: boards/stm32/*/ with a .ioc)")
    p.add_argument("--cache", default="build/vendor-upstream")
    p.add_argument("--mirror", action="append", default=[], help="NAME=DIR: use a local copy of an upstream repository")
    a = p.parse_args(argv)
    mirrors = dict(m.split("=", 1) for m in a.mirror)
    projects = a.projects or sorted(str(i.parent) for i in Path(".").glob("boards/stm32/*/*.ioc"))
    if not projects:
        print("::warning title=vendor provenance::NOT RUN: no STM32 project under boards/stm32/")
        return 0
    failed = 0
    for project in projects:
        try:
            errors = check_project(project, a.cache, mirrors)
        except (ValueError, subprocess.CalledProcessError) as exc:
            errors = [f"cannot check: {exc}"]
        for e in errors:
            print(f"::error file={project}/{e.split(':', 1)[0]}::vendored file {e}")
        print(f"{'FAIL' if errors else 'PASS'} {project}: {len(errors)} errors")
        failed += bool(errors)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
