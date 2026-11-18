#!/usr/bin/env python3
"""Build only the test consumer; reuse a completed RC4 CommonLib Release library."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--rc4-native', type=Path, required=True)
    p.add_argument('--higgs-root', type=Path, required=True)
    p.add_argument('--planck-root', type=Path, required=True)
    p.add_argument('--json-include', type=Path, required=True)
    p.add_argument('--cmake', type=Path)
    args = p.parse_args()
    output = args.output.resolve()
    if ROOT == output or ROOT in output.parents:
        p.error('output must be outside the repository')
    output.mkdir(parents=True, exist_ok=True)
    cmake = args.cmake or shutil.which('cmake')
    if not cmake:
        vswhere = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        vs = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-property', 'installationPath'], text=True).strip()
        cmake = Path(vs) / 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    commands = [
        [str(cmake), '-S', str(ROOT), '-B', str(output), '-G', 'Visual Studio 18 2026', '-A', 'x64',
         f'-DRC4_NATIVE={args.rc4_native.resolve()}', f'-DHIGGS_ROOT={args.higgs_root.resolve()}',
         f'-DPLANCK_ROOT={args.planck_root.resolve()}', f'-DJSON_INCLUDE={args.json_include.resolve()}'],
        [str(cmake), '--build', str(output), '--config', 'Release', '--parallel', '4'],
    ]
    inputs = [f for f in ROOT.iterdir() if f.is_file()]
    inputs += [args.higgs_root / 'include' / f for f in ('higgsinterface001.h','higgsabitypes.h','higgshandapi.h','higgsinput.h')]
    inputs += [args.planck_root / 'include/planck_synergy.h', args.json_include / 'nlohmann/json.hpp']
    inputs += [args.rc4_native / 'CMakeCache.txt']
    inputs += [args.rc4_native / f'_deps/{directory}/Release/{library}.lib' for directory, library in
               (('commonlibsse-build','CommonLibSSE'),('fmt-build','fmt'),('spdlog-build','spdlog'))]
    inputs += list((ROOT.parents[2] / 'include').glob('*.h'))
    pins = {str(f.resolve()): sha(f) for f in inputs}
    executions = []
    for name, argv in zip(('configure', 'build'), commands):
        result = subprocess.run(argv, capture_output=True)
        (output / f'{name}.log').write_bytes(result.stdout + result.stderr)
        executions.append({'argv': argv, 'exitCode': result.returncode})
        if result.returncode:
            raise RuntimeError(f'{name} failed; see {output / (name + ".log")}')
    dll = output / 'Release/test_VRInteractionFramework.dll'
    # No SKSE entrypoint is invoked: this proves loader imports and exported ABI only.
    probe = ctypes.CDLL(str(dll))
    for symbol in ('SKSEPlugin_Query', 'SKSEPlugin_Load', 'VRInteractionFrameworkTest_Version', 'VRInteractionFrameworkTest_CopyStatus'):
        getattr(probe, symbol)
    probe.VRInteractionFrameworkTest_Version.restype = ctypes.c_uint32
    assert probe.VRInteractionFrameworkTest_Version() == 1
    copy = probe.VRInteractionFrameworkTest_CopyStatus
    copy.argtypes = [ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)]
    copy.restype = ctypes.c_uint32
    needed = ctypes.c_uint32()
    assert copy(1, None, 0, ctypes.byref(needed)) == 5 and needed.value > 1
    buffer = ctypes.create_string_buffer(needed.value)
    assert copy(1, buffer, len(buffer), ctypes.byref(needed)) == 0
    status = json.loads(buffer.value)
    assert status['state'] == 'loading' and status['schemaVersion'] == 1
    assert copy(2, None, 0, ctypes.byref(needed)) == 2
    for path, digest in pins.items():
        assert sha(Path(path)) == digest, f'input changed: {path}'
    manifest = {'dll': {'path': str(dll), 'bytes': dll.stat().st_size, 'sha256': sha(dll)},
                'sourceSha256': pins, 'commands': executions, 'gameStarts': 0,
                'qualification': 'Compiled and linked; exported version and unavailable pre-SKSE status copied. No gameplay qualification.'}
    (output / 'fixture-build-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(manifest['dll'], indent=2))


if __name__ == '__main__':
    main()
