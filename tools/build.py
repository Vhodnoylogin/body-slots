#!/usr/bin/env python3
"""Build Body Slots with installed x64 Visual Studio tools; no CMake/downloads."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def vs_environment():
    if shutil.which("cl.exe"):
        return os.environ.copy()
    vswhere = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    if not vswhere.is_file():
        raise RuntimeError("Visual Studio locator not installed; run from an x64 VS tools prompt")
    location = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"], text=True).strip()
    if not location:
        raise RuntimeError("installed Visual Studio x64 C++ tools not found")
    setup = Path(location) / "VC/Auxiliary/Build/vcvars64.bat"
    # The only shell command sets the installed compiler environment; builds use argv.
    result = subprocess.run(f'cmd.exe /d /s /c "call "{setup}" >nul && set"', capture_output=True, text=True, check=True)
    env = os.environ.copy()
    for line in result.stdout.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            if key:
                env[key] = value
    return env


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-root", type=Path, required=True, help="directory containing public common/ and sksevr/ headers")
    parser.add_argument("--output", type=Path, required=True, help="build/evidence directory outside the source repository")
    parser.add_argument("--skip-tests", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    if output == ROOT or ROOT in output.parents:
        parser.error("--output must be outside the Git source repository")
    sdk = args.sdk_root.resolve()
    for file in (sdk / "common/ITypes.h", sdk / "sksevr/skse64/PluginAPI.h", sdk / "sksevr/skse64_common/skse_version.h"):
        if not file.is_file():
            parser.error(f"public SDK header not found: {file}")
    output.mkdir(parents=True, exist_ok=True)
    log = output / "build.log"
    env = vs_environment()
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    compiler = shutil.which("cl.exe", path=env.get("Path", env.get("PATH")))
    if not compiler:
        raise RuntimeError("x64 compiler not found after environment setup")
    log.write_text("Body Slots standalone authored definitions and saved configuration build\n", encoding="utf-8")

    def run(command):
        result = subprocess.run([str(value) for value in command], cwd=output, env=env, capture_output=True, text=True)
        text = result.stdout + result.stderr
        with log.open("a", encoding="utf-8") as stream:
            stream.write("\n$ " + subprocess.list2cmdline([str(v) for v in command]) + "\n" + text)
        print(text, end="")
        if result.returncode:
            raise RuntimeError(f"command failed ({result.returncode}); see {log}")

    def compile_target(name, sources, dll=False):
        obj = output / "obj" / name
        obj.mkdir(parents=True, exist_ok=True)
        extension = ".dll" if dll else ".exe"
        target = output / (name + extension)
        command = [compiler, "/nologo", "/std:c++17", "/EHsc", "/MD", "/O2", "/W4", "/permissive-", "/DUNICODE", "/D_UNICODE", "/D_CRT_SECURE_NO_WARNINGS",
                   "/I" + str(ROOT / "include"), "/I" + str(ROOT / "src"), "/I" + str(sdk), "/I" + str(sdk / "sksevr"), "/Fo" + str(obj) + os.sep, "/Fe:" + str(target)]
        if dll:
            command.append("/LD")
        command.extend(ROOT / source for source in sources)
        command.extend(["/link", "/INCREMENTAL:NO", "/MACHINE:X64"])
        if dll:
            command.append("/IMPLIB:" + str(output / (name + ".lib")))
        else:
            # Test code deliberately holds several maximum-size snapshots simultaneously.
            command.append("/STACK:8388608")
        run(command)
        return target

    dll = compile_target("BodySlots", ["src/registry.cpp", "src/instances.cpp", "src/validation.cpp", "src/configuration_serialization.cpp", "src/native_placement.cpp", "src/native_policy.cpp", "src/package.cpp", "src/plugin.cpp"], True)
    package = output / "examples/potion-belt.bslots"
    run([sys.executable, "-B", ROOT / "tools/author.py", ROOT / "examples/potion-belt.json", "--output", package])
    if not args.skip_tests:
        compile_target("consumer_fixture", ["tests/consumer_fixture.cpp"], True)
        native = compile_target("native_tests", ["tests/native_tests.cpp", "src/validation.cpp", "src/package.cpp"])
        configuration = compile_target("configuration_tests", ["tests/configuration_tests.cpp", "src/registry.cpp", "src/instances.cpp", "src/validation.cpp", "src/configuration_serialization.cpp"])
        instances = compile_target("instance_tests", ["tests/instance_tests.cpp"])
        native_placement = compile_target("native_placement_tests", ["tests/native_placement_tests.cpp"])
        native_policy = compile_target("native_policy_tests", ["tests/native_policy_tests.cpp"])
        run([sys.executable, "-B", ROOT / "tests/test_author.py"])
        run([native, dll, package, output])
        run([configuration])
        run([instances, dll])
        run([native_placement, dll])
        run([native_policy, dll])
        # A disposable synthetic host proves SDK guards/phase ordering without Skyrim.
        fixture = output / "game-fixture"
        fixture.mkdir(exist_ok=True)
        host = fixture / "SkyrimVR.exe"
        shutil.copy2(native, host)
        packages = fixture / "Data/SKSE/Plugins/BodySlots"
        packages.mkdir(parents=True, exist_ok=True)
        shutil.copy2(package, packages / "example.bslots")
        (packages / "invalid.bslots").write_bytes(b"not a valid package")
        run([host, "--skse", dll, package])
    artifacts = {}
    for path in (dll, package):
        artifacts[path.name] = {"path": str(path), "bytes": path.stat().st_size, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
    (output / "artifacts.json").write_text(json.dumps({"apiVersion": 1, "configurationApiVersion": 1, "instanceApiVersion": 1, "instanceSpecSize": 280, "instanceRecordSize": 784, "instanceViewSize": 48, "instanceApiSizeX64": 48, "instanceCapabilities": 3, "nativePlacementApiVersion": 1, "nativePlacementSnapshotSize": 688, "nativePlacementApiSizeX64": 48, "nativePolicyApiVersion": 1, "nativePolicyFieldCount": 9, "nativePolicySize": 72, "nativePolicySnapshotSize": 1024, "nativePolicyApiSizeX64": 48, "nativePolicyCapability": 2, "definitionSize": 408, "slotRecordSize": 424, "apiSizeX64": 80, "configurationApiSizeX64": 72, "effectiveSlotRecordSize": 440, "testsRun": not args.skip_tests, "artifacts": artifacts}, indent=2) + "\n", encoding="utf-8")
    print(f"BUILD COMPLETE: {dll}\nEVIDENCE: {log}")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        print(f"BUILD FAILED: {error}", file=sys.stderr)
        raise SystemExit(1)
