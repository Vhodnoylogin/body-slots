#!/usr/bin/env python3
"""Check copied exports from the real BodySlots DLL without initializing Skyrim."""
import ctypes as C
from pathlib import Path
import sys


class Integration(C.Structure):
    _fields_ = [('size', C.c_uint32), ('version', C.c_uint32), ('connected', C.c_uint32), ('reserved', C.c_uint32),
                ('client', C.c_uint64), ('epoch', C.c_uint64), ('frame', C.c_uint64), ('body', C.c_uint64)]


Copy = C.CFUNCTYPE(C.c_uint32, C.POINTER(Integration))


class GameAPI(C.Structure):
    _fields_ = [('size', C.c_uint32), ('version', C.c_uint32), ('copy', Copy)]


def main():
    dll = C.CDLL(str(Path(sys.argv[1]).resolve()))
    for symbol in ('SKSEPlugin_Query', 'SKSEPlugin_Load', 'BodySlots_QueryInterface', 'BodySlots_QueryRuntimeInterface',
                   'BodySlots_QueryInstanceInterface', 'BodySlots_QueryInstanceConfigurationInterface', 'BodySlots_QueryGameInterface'):
        getattr(dll, symbol)
    query = dll.BodySlots_QueryGameInterface
    query.argtypes = [C.c_uint32, C.c_uint32]
    query.restype = C.POINTER(GameAPI)
    if query(99, C.sizeof(GameAPI)) or query(1, 1):
        raise RuntimeError('unsupported table accepted')
    api = query(1, C.sizeof(GameAPI))
    if not api or api.contents.version != 1 or api.contents.size != C.sizeof(GameAPI):
        raise RuntimeError('game table layout differs')
    status = Integration()
    status.size = C.sizeof(status)
    if api.contents.copy(C.byref(status)) or status.connected or status.client or status.frame:
        raise RuntimeError('uninitialized game adapter pretends to be connected')
    print('PASS actual native DLL exports, copied game-table negotiation and disconnected observation; no SKSE/game initialization')


if __name__ == '__main__':
    main()
