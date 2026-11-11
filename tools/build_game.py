#!/usr/bin/env python3
"""Build the independent Skyrim VR adapter and production-core test suites.

No deployment, game launch, writable MO2 access or proprietary Havok SDK is used.
The legacy build.py command remains the SDK/foundation fixture build.
"""
import argparse
import hashlib
import json
import os
import re
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
    p.add_argument('--build-dir', type=Path, help='Reuse an external CMake build directory')
    p.add_argument('--higgs-test-manifest', type=Path, help='Production-core source list for the reviewed HIGGS branch')
    p.add_argument('--planck-root', type=Path, help='Enable the full optional PLANCK provider')
    p.add_argument('--ppb-root', type=Path, help='Enable the optional PPB revision 2 digest')
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
    sources = [f for directory in ('include', 'src', 'tests', 'tools', 'examples') for f in (ROOT / directory).rglob('*')
               if f.is_file() and f.suffix in ('.h', '.cpp', '.py', '.c')]
    sources.append(ROOT / 'CMakeLists.txt')
    native = args.build_dir.resolve() if args.build_dir else output / 'native'
    if ROOT in native.parents or native == ROOT:
        p.error('build directory must be outside the source checkout')
    closure = (args.higgs_test_manifest or (ROOT / 'cmake/HiggsServices.cmake')).resolve()
    dependencies = [closure, *(higgs / 'include' / name for name in (
        'higgsinterface001.h', 'higgsnativeregions.h', 'higgsauthorizeddispatch.h', 'higgsinputroutes.h',
        'higgshandapi.h', 'higgshandrouting.h', 'higgsgrantmutation.h', 'higgsinputcontext.h', 'higgsinventory.h', 'higgstransactions.h'))]
    roots = {'bodySlots': ROOT, 'higgs': higgs}
    if args.planck_root:
        roots['planck'] = args.planck_root.resolve()
        dependencies += [roots['planck'] / 'include/planck_synergy.h', roots['planck'] / 'src/synergy_runtime.cpp']
    if args.ppb_root:
        roots['ppb'] = args.ppb_root.resolve()
        dependencies.append(roots['ppb'] / 'src/PpbTouchAPI.h')
    dependencies += [higgs / name for name in re.findall(r'\$\{HIGGS_SERVICE_ROOT\}/([^"\n]+)',closure.read_text(encoding='utf-8'))
                     if Path(name).suffix in ('.h','.hpp','.inl','.c','.cc','.cpp','.cxx')]
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

    configure = [cmake, '-S', ROOT, '-B', native, '-G', 'Visual Studio 18 2026', '-A', 'x64',
                 f'-DBODY_SLOTS_SKSE_SDK={args.sdk_root.resolve()}', f'-DBODY_SLOTS_HIGGS_SDK={higgs}']
    configure += [f'-DBODY_SLOTS_HIGGS_TEST_MANIFEST={closure}', '-DBODY_SLOTS_PLANCK_POSE_SDK=', '-DBODY_SLOTS_HIGGS_GRIP_SDK=',
                  f"-DBODY_SLOTS_PLANCK_SDK={roots.get('planck', '')}", f"-DBODY_SLOTS_PPB_SDK={roots.get('ppb', '')}"]
    if args.dependency_sources:
        for name in ('commonlibsse', 'fmt', 'spdlog', 'rapidcsv'):
            configure.append(f'-DFETCHCONTENT_SOURCE_DIR_{name.upper()}={args.dependency_sources.resolve() / (name + "-src")}')
    run('configure', configure)
    run('build', [cmake, '--build', native, '--config', 'Release', '-j', '6'])
    run('tests', [ctest, '--test-dir', native, '-C', 'Release', '--no-tests=error', '--output-on-failure'])
    run('dll-probe', [sys.executable, '-B', ROOT / 'tools/probe_game_dll.py', native / 'Release/BodySlots.dll'])
    for path, digest in pins.items():
        if sha(Path(path)) != digest:
            raise RuntimeError(f'input changed during build; rerun against stable inputs: {path}')
    dll = native / 'Release/BodySlots.dll'
    revisions = {name: subprocess.check_output(['git', '-C', root, 'rev-parse', 'HEAD'], text=True).strip()
                 for name, root in roots.items()}
    manifest = {'sourceRevisions': revisions, 'sourceSha256': pins, 'commands': commands,
                'dll': {'path': str(dll), 'bytes': dll.stat().st_size, 'sha256': sha(dll)},
                'gameStarts': 0, 'qualification': 'Native DLL linked. Offline provider, geometry and authority checks passed. Skyrim VR gameplay is unqualified.'}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(f'BUILD COMPLETE: {dll}\nEVIDENCE: {output / "manifest.json"}')


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f'BUILD FAILED: {error}', file=sys.stderr)
        raise SystemExit(1)
