"""Minimal single-core vermeer ADT for the early, storage-free XNU build."""
import os
import itertools
from pathlib import Path
import struct
import sys
import adt

handles = itertools.count(1)

def u32(*v): return struct.pack('<' + 'I' * len(v), *v)
def u64(*v): return struct.pack('<' + 'Q' * len(v), *v)
def string(v): return v.encode() + b'\0'
def node(name, props=(), children=()):
    result = adt.Node()
    result.props = [('name', 0, string(name)), ('AAPL,phandle', 0, u32(next(handles)))] + [(k, 0, v) for k, v in props]
    result.children = list(children)
    return result

def build():
    cpu = node('cpu0', [
        ('device_type', string('cpu')),
        ('compatible', string('arm,cortex-a510') + string('ARM,v8')),
        ('reg', u32(0)), ('cpu-id', u32(0)), ('cluster-id', u32(0)),
        ('cluster-type', u32(0)), ('die-cluster-id', u32(0)),
        ('cluster-core-id', u32(0)), ('l2-cache-id', u32(0)), ('l3-cache-id', u32(0)),
        ('state', string('running')), ('timebase-frequency', u32(19200000)),
        ('clock-frequency', u32(2000000000)), ('bus-frequency', u32(100000000)),
        ('memory-frequency', u32(100000000)), ('peripheral-frequency', u32(19200000)),
        ('fixed-frequency', u32(19200000)),
    ])
    return node('device-tree', [
        ('compatible', string('xiaomi,vermeer') + string('xiaomi,sweet') + string('qcom,sm8550') + string('AppleARM')),
        ('model', string('Redmi K70')), ('target-type', string('vermeer')),
        ('manufacturer', string('Xiaomi')), ('#address-cells', u32(2)), ('#size-cells', u32(2)),
    ], [
        node('chosen', [('dram-base', u64(0xc0000000)), ('dram-size', u64(0x10000000)),
                        ('random-seed', os.urandom(64)), ('debug-enabled', u32(1)),
                        ('firmware-version', string('vermeer-shim-early'))],
             [node('memory-map')]),
        node('defaults'),
        node('cpus', [('#address-cells', u32(1)), ('#size-cells', u32(0))], [cpu]),
        node('arm-io', [('device_type', string('sweet-io')),
                       ('compatible', string('sweet-io') + string('arm-io')),
                       ('#address-cells', u32(2)), ('#size-cells', u32(2)),
                       ('ranges', u64(0, 0x01000000, 0xff000000))],
             # pe_init_fiq adds pe_arm_get_soc_base_phys(), obtained from the
             # arm-io ranges parent base above (0x01000000). These are offsets:
             # 0x16100000 + 0x01000000 = physical GICD 0x17100000, likewise GICR.
             [node('gic', [('compatible', string('arm,gic-v3')), ('interrupt-controller', b''),
                           ('virtual-timer-intid', u32(27)),
                           ('reg', u64(0x16100000, 0x10000, 0x16180000, 0x200000))])]),
        node('sweet-fbcon', [('reg', u64(0xb8000000, 1440 * 3200 * 4)),
                            ('width', u32(1440)), ('height', u32(3200)), ('stride', u32(5760))]),
    ])

if __name__ == '__main__':
    tree = build()
    blob = adt.serialize(tree)
    parsed, used = adt.parse(blob)
    assert used == len(blob)
    assert parsed.name == 'device-tree'
    Path(sys.argv[1]).write_bytes(blob)
    print(f'vermeer ADT: {len(blob)} bytes, one CPU, RAM c0000000-d0000000; no storage nodes')
