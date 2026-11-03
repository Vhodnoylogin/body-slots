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


class RegionIntegration(C.Structure):
    _fields_ = [('size', C.c_uint32), ('version', C.c_uint32), ('connected', C.c_uint32), ('reserved', C.c_uint32),
                ('client', C.c_uint64), ('epoch', C.c_uint64), ('save', C.c_uint64), ('observation', C.c_uint64)]


CopyRegionsIntegration = C.CFUNCTYPE(C.c_uint32, C.c_uint64, C.POINTER(RegionIntegration))


class RegionActionsAPI(C.Structure):
    _fields_ = [('size', C.c_uint32), ('version', C.c_uint32), ('capabilities', C.c_uint64),
                ('register', C.c_void_p), ('remove', C.c_void_p), ('anchor', C.c_void_p),
                ('copy', CopyRegionsIntegration), ('observation', C.c_void_p)]


def main():
    dll = C.CDLL(str(Path(sys.argv[1]).resolve()))
    for symbol in ('SKSEPlugin_Query', 'SKSEPlugin_Load', 'BodySlots_QueryInterface', 'BodySlots_QueryRuntimeInterface',
                   'BodySlots_QueryInstanceInterface', 'BodySlots_QueryInstanceConfigurationInterface', 'BodySlots_QueryGameInterface',
                   'BodySlots_QueryZoneInterface', 'BodySlots_QueryManipulationInterface', 'BodySlots_QueryEquipmentInterface',
                   'BodySlots_QueryContentPolicyInterface', 'BodySlots_QueryBehaviorInterface',
                   'BodySlots_QueryContentObservationInterface', 'BodySlots_QueryRegionsInterface',
                   'BodySlots_QueryRegionActionsInterface'):
        getattr(dll, symbol)
    class Header(C.Structure):
        _fields_ = [('size', C.c_uint32), ('version', C.c_uint32), ('capabilities', C.c_uint64)]
    for symbol, size, capabilities in (
        ('BodySlots_QueryZoneInterface', 48, 7), ('BodySlots_QueryManipulationInterface', 48, 3),
        ('BodySlots_QueryEquipmentInterface', 40, 3), ('BodySlots_QueryContentPolicyInterface', 48, 3),
        ('BodySlots_QueryBehaviorInterface', 24, 1),
        ('BodySlots_QueryContentObservationInterface', 24, 1),
        ('BodySlots_QueryRegionsInterface', 56, 3),
        ('BodySlots_QueryRegionActionsInterface', 56, 1),
    ):
        extension = getattr(dll, symbol)
        extension.argtypes = [C.c_uint32, C.c_uint32]
        extension.restype = C.POINTER(Header)
        table = extension(1, size)
        if extension(2, size) or extension(1, size - 1) or not table or (
            table.contents.size, table.contents.version, table.contents.capabilities
        ) != (size, 1, capabilities):
            raise RuntimeError(f'{symbol} negotiation/layout differs')
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
    class Registry(C.Structure):
        _fields_ = [('size', C.c_uint32), ('version', C.c_uint32), ('capabilities', C.c_uint64),
                    ('functions', C.c_void_p * 7),
                    ('observer', C.CFUNCTYPE(C.c_uint32, C.POINTER(C.c_uint64)))]
    # The new action table is discoverable without initializing SKSE or HIGGS;
    # its readiness observation must still report disconnected, with no grant.
    registry = dll.BodySlots_QueryInterface
    registry.argtypes = [C.c_uint32, C.c_uint32]
    registry.restype = C.POINTER(Registry)
    base = registry(1, C.sizeof(Registry))
    if not base:
        raise RuntimeError('base registry negotiation failed')
    reader = C.c_uint64()
    if base.contents.observer(C.byref(reader)):
        raise RuntimeError('observer registration failed')
    region_query = dll.BodySlots_QueryRegionActionsInterface
    region_query.argtypes = [C.c_uint32, C.c_uint32]
    region_query.restype = C.POINTER(RegionActionsAPI)
    regions = region_query(1, C.sizeof(RegionActionsAPI))
    region_status = RegionIntegration()
    region_status.size = C.sizeof(region_status)
    region_status.version = 1
    if regions.contents.copy(reader.value, C.byref(region_status)) or region_status.connected or region_status.client or region_status.epoch:
        raise RuntimeError('uninitialized region actions pretend to be connected')
    release_reader = C.CFUNCTYPE(C.c_uint32, C.c_uint64)(base.contents.functions[1])
    if release_reader(reader.value):
        raise RuntimeError('observer cleanup failed')
    print('PASS actual native DLL exports, copied game-table negotiation and disconnected observation; no SKSE/game initialization')


if __name__ == '__main__':
    main()
