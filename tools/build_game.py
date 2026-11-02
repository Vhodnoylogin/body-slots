#!/usr/bin/env python3
"""Build the independent Skyrim VR adapter and both production-core test suites.

No deployment, game launch, writable MO2 access or proprietary Havok SDK is used.
The legacy build.py command remains the SDK/foundation fixture build.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--sdk-root', type=Path, required=True)
    p.add_argument('--higgs-root', type=Path, required=True)
    p.add_argument('--dependency-sources', type=Path, help='Optional existing directory with commonlibsse-src, fmt-src, spdlog-src, rapidcsv-src')
    p.add_argument('--cmake', type=Path)
    args = p.parse_args()
    output = args.output.resolve()
    if output == ROOT or ROOT in output.parents:
        p.error('build output must be outside the Git source checkout')
    output.mkdir(parents=True, exist_ok=True)
    cmake = args.cmake or shutil.which('cmake')
    if not cmake:
        vswhere = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        vs = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-property', 'installationPath'], text=True).strip()
        cmake = Path(vs) / 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    cmake = Path(cmake).resolve()
    ctest = cmake.with_name('ctest.exe' if os.name == 'nt' else 'ctest')
    higgs = args.higgs_root.resolve()
    sources = [f for directory in ('include', 'src', 'tests', 'tools') for f in (ROOT / directory).rglob('*')
               if f.is_file() and f.suffix in ('.h', '.cpp', '.py', '.c')]
    sources.append(ROOT / 'CMakeLists.txt')
    dependencies = [higgs / 'include' / name for name in ('higgsinterface001.h', 'higgshandapi.h', 'higgshandrouting.h', 'higgshandoperations.h', 'hand_broker.h')]
    dependencies += [higgs / 'src' / name for name in ('hand_broker.cpp', 'hand_runtime.cpp')]
    pins = {str(f): sha(f) for f in sources + dependencies}
    commands = []

    def run(name, command):
        argv = [str(x) for x in command]
        result = subprocess.run(argv, capture_output=True, cwd=output, timeout=1200)
        (output / (name + '.log')).write_bytes(result.stdout + result.stderr)
        commands.append({'argv': argv, 'exitCode': result.returncode})
        if result.returncode:
            raise RuntimeError(f'{name} failed ({result.returncode}); see {output / (name + ".log")}')
        print(f'{name}: passed', flush=True)

    configure = [cmake, '-S', ROOT, '-B', output / 'native', '-G', 'Visual Studio 18 2026', '-A', 'x64',
                 f'-DBODY_SLOTS_SKSE_SDK={args.sdk_root.resolve()}', f'-DBODY_SLOTS_HIGGS_SDK={higgs}']
    if args.dependency_sources:
        for name in ('commonlibsse', 'fmt', 'spdlog', 'rapidcsv'):
            configure.append(f'-DFETCHCONTENT_SOURCE_DIR_{name.upper()}={args.dependency_sources.resolve() / (name + "-src")}')
    run('configure', configure)
    run('build', [cmake, '--build', output / 'native', '--config', 'Release', '--target',
                  'BodySlots', 'body_slots_runtime_tests', 'body_slots_higgs_integration_tests', '-j', '6'])
    run('tests', [ctest, '--test-dir', output / 'native', '-C', 'Release', '--output-on-failure'])
    run('dll-probe', [sys.executable, '-B', ROOT / 'tools/probe_game_dll.py', output / 'native/Release/BodySlots.dll'])
    for path, digest in pins.items():
        if sha(Path(path)) != digest:
            raise RuntimeError(f'input changed during build; rerun against stable inputs: {path}')
    dll = output / 'native/Release/BodySlots.dll'
    manifest = {'sourceSha256': pins, 'commands': commands, 'dll': {'path': str(dll), 'bytes': dll.stat().st_size, 'sha256': sha(dll)},
                'gameStarts': 0, 'qualification': 'Native adapter linked; production cores and copied contracts checked offline. No game, pose, scene, hardware or HIGGS/PLANCK native qualification.'}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(f'BUILD COMPLETE: {dll}\nEVIDENCE: {output / "manifest.json"}')


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f'BUILD FAILED: {error}', file=sys.stderr)
        raise SystemExit(1)
