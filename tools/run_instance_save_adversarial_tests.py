#!/usr/bin/env python3
"""Build public actual-DLL save-failure probes in a disposable synthetic SKSE host."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True
from build import vs_environment

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-root", type=Path, required=True)
    parser.add_argument("--body-slots-dll", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output == ROOT or ROOT in output.parents:
        parser.error("output must be outside the source repository")
    if output.exists() and any(output.iterdir()):
        parser.error("output must be new or empty to preserve prior evidence")
    sdk = args.sdk_root.resolve(strict=True)
    sdk_files = [sdk / "common/ITypes.h", sdk / "sksevr/skse64/PluginAPI.h", sdk / "sksevr/skse64_common/skse_version.h"]
    for path in sdk_files:
        if not path.is_file():
            parser.error(f"public SDK header not found: {path}")
    dll = args.body_slots_dll.resolve(strict=True)
    output.mkdir(parents=True, exist_ok=True)
    pinned = output / "BodySlots.dll"
    shutil.copy2(dll, pinned)
    env = vs_environment()
    compiler = shutil.which("cl.exe", path=env.get("Path", env.get("PATH")))
    if not compiler:
        raise RuntimeError("x64 compiler unavailable after environment setup")
    source = ROOT / "tests/instance_save_adversarial_tests.cpp"
    executable = output / "instance_save_adversarial_tests.exe"
    command = [compiler, "/nologo", "/std:c++17", "/EHsc", "/MD", "/O2", "/W4", "/permissive-",
               "/D_CRT_SECURE_NO_WARNINGS", "/I" + str(ROOT / "include"), "/I" + str(sdk),
               "/I" + str(sdk / "sksevr"), "/Fo" + str(output) + os.sep,
               "/Fe:" + str(executable), str(source), "/link", "/INCREMENTAL:NO", "/MACHINE:X64", "/STACK:8388608"]
    compiled = subprocess.run(command, cwd=output, env=env, capture_output=True)
    (output / "build.log").write_bytes(compiled.stdout + compiled.stderr)
    print((compiled.stdout + compiled.stderr).decode(errors="replace"), end="")
    if compiled.returncode:
        return compiled.returncode
    fixture = output / "game-fixture"
    fixture.mkdir()
    host = fixture / "SkyrimVR.exe"
    shutil.copy2(executable, host)  # name satisfies the public SDK guard; never the installed game
    run = subprocess.run([str(host), str(pinned)], cwd=output, env=env, capture_output=True, timeout=120)
    (output / "run.log").write_bytes(run.stdout + run.stderr)
    files = [Path(__file__).resolve(), ROOT / "tools/build.py", source, pinned, executable, *sdk_files]
    files += [ROOT / "include" / name for name in ("body_slots_api.h", "body_slots_configuration_api.h",
              "body_slots_instance_api.h", "body_slots_instance_configuration_api.h")]
    (output / "manifest.json").write_text(json.dumps({"exitCode": run.returncode, "fullVrikDllLoads": 0,
        "gameStarts": 0, "syntheticHost": str(host), "compileCommand": command,
        "sha256": {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in files}}, indent=2) + "\n", encoding="utf-8")
    print((run.stdout + run.stderr).decode(errors="replace"), end="")
    return run.returncode


if __name__ == "__main__":
    raise SystemExit(main())
