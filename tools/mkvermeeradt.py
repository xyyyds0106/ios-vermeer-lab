"""Minimal single-core vermeer ADT for the early, storage-free XNU build."""
import os
import argparse
import hashlib
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

def build(highram=False, ios=False, gui=False, product_adt=None):
    if gui and not ios:
        raise ValueError('GUI device tree requires the iOS profile')
    # Probe011 verified sparse access to exactly this additional window.
    phys, size = (0x8c0000000, 0x80000000) if highram else (0xc0000000, 0x10000000)
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
    result = node('device-tree', [
        ('compatible', string('xiaomi,vermeer') + string('xiaomi,sweet') + string('qcom,sm8550') + string('AppleARM')),
        ('model', string('Redmi K70')), ('target-type', string('vermeer')),
        ('manufacturer', string('Xiaomi')), ('#address-cells', u32(2)), ('#size-cells', u32(2)),
    ], [
        node('chosen', [('dram-base', u64(phys)), ('dram-size', u64(size)),
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
    if ios:
        chosen = next(c for c in result.children if c.name == 'chosen')
        chosen.props.append(('boot-manifest-hash', 0, hashlib.sha384(b'vermeer experimental iOS root').digest()))
        # libimg4's cryptex boot tasks need the digest algorithm as well.
        chosen.props.append(('crypto-hash-method', 0, string('sha2-384')))
        # kern_csr.c reads lp-sip0 as u64 and lp-sip1 as u32. The copied
        # HFS root has no SF_RESTRICTED flag on dyld caches; vm_unix.c rejects
        # their shared mappings unless CSR_ALLOW_UNRESTRICTED_FS (bit1) is set.
        # Relax only filesystem policy, and declare the already-unsealed root.
        # Diagnostic trees keep their existing policy and have no asmb node.
        chosen.children.append(node('asmb', [
            ('lp-sip0', u64(0x2)), ('lp-sip1', u32(1)),
        ]))
        defaults = next(c for c in result.children if c.name == 'defaults')
        defaults.props.append(('no-effaceable-storage', 0, u32(1)))
    if gui:
        if product_adt is None:
            raise ValueError('GUI profile requires the locally extracted D22AP product ADT')
        firmware_tree, _ = adt.parse(Path(product_adt).read_bytes())
        product = next(c for c in firmware_tree.children if c.name == 'product')
        # Generic firmware product description only; skip syscfg placeholders.
        product_props = [(k, v) for k, flags, v in product.props
                         if not flags and k not in ('name', 'AAPL,phandle')]
        result.children.append(node('product', product_props))
        identity = {
            'compatible': string('D22AP') + string('iPhone10,3') + string('xiaomi,vermeer') +
                          string('xiaomi,sweet') + string('qcom,sm8550') + string('AppleARM'),
            'model': string('iPhone10,3'), 'target-type': string('D22'),
        }
        result.props = [(k, flags, identity.get(k, v)) for k, flags, v in result.props]
        result.props += [('target-sub-type', 0, string('D22AP')),
                         ('physical-model', 0, string('Redmi K70'))]
        # A nonzero supported scale prevents an invalid infinite FBS mode.
        # Keep the reference iOS16 @3x behavior; the physical framebuffer remains
        # the observed1440x3200 K70 buffer, never the iPhone's memory layout.
        chosen.props += [('display-scale', 0, u32(3)), ('display-rotation', 0, u32(0)),
                         ('unique-chip-id', 0, u64(0x5645524d45455201)),
                         ('chip-id', 0, u32(0x8015))]
        armio = next(c for c in result.children if c.name == 'arm-io')
        armio.children += [node('wdt', [('no-pmu', u32(1))]),
                          node('disp0', [('device_type', string('display-subsystem')),
                                         ('compatible', string('sweet,display')),
                                         ('canvas-width', u32(1440)), ('canvas-height', u32(3200))])]
        result.children.append(node('buttons', [
            ('button-names', string('volup') + string('voldown') + string('hold')),
            ('home-button-type', u32(2)), ('opposed-power-vol-buttons', u32(1))]))
    return result

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('out')
    parser.add_argument('--highram', action='store_true', help='probe011 window: 8c0000000..940000000, 2GiB; UFS DMA not validated here')
    parser.add_argument('--ios', action='store_true', help='experimental iOS root boot manifest/defaults; no device calibration changes')
    parser.add_argument('--ios-gui', action='store_true', help='iOS display/HID compatibility description, real K70 framebuffer')
    parser.add_argument('--product-adt', help='local generic D22AP ADT extracted from the existing IPSW; never committed')
    args = parser.parse_args()
    tree = build(args.highram, args.ios or args.ios_gui, args.ios_gui, args.product_adt)
    blob = adt.serialize(tree)
    parsed, used = adt.parse(blob)
    assert used == len(blob)
    assert parsed.name == 'device-tree'
    Path(args.out).write_bytes(blob)
    window = '8c0000000-940000000 (2GiB)' if args.highram else 'c0000000-d0000000 (256MiB)'
    print(f'vermeer ADT: {len(blob)} bytes, one CPU, RAM {window}; no storage nodes')
