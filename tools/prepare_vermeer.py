"""Prepare bounded vermeer early/RAM-root/opt-in UFS-test kernel profiles.

Keep the upstream patch series untouched. Rewrite matching added/removed/context
lines consistently so subsequent patches still apply. This profile intentionally
does not include the later reference UFS, USB, touch, battery or iOS GUI drivers.
The iOS profile adds the independent guarded K70 CDC ACM debug transport.
The UFS-test profile adds only our guarded read self-test beside RAM-root.
"""
from pathlib import Path
import re
import shutil
import sys
import difflib

source, target = map(Path, sys.argv[1:3])
gui = '--ios-gui' in sys.argv[3:]
ios = '--ios' in sys.argv[3:] or gui
ufs_block = '--ufs-block' in sys.argv[3:] or ios
hfs_ramroot = '--hfs-ramroot' in sys.argv[3:] or ufs_block
ufs_probe = '--ufs-probe' in sys.argv[3:] or hfs_ramroot
ramroot = '--ramroot' in sys.argv[3:] or ufs_probe
last_patch = 59 if gui else (45 if ios else (27 if hfs_ramroot else (25 if ramroot else 18)))
target.mkdir(parents=True, exist_ok=True)
if any(target.iterdir()):
    raise SystemExit('Destination must be empty')
patches = sorted(p for p in source.glob('*.patch') if int(p.name[:4]) <= last_patch)
assert len(patches) == last_patch, 'Incomplete reference patch series'
if ios:
    # Replace the Sweet UFS transport entirely with the guarded K70 driver.
    patches = [p for p in patches if int(p.name[:4]) not in (28, 30, 34, 47)]
counts = {'framebuffer': 0, 'width': 0, 'height': 0, 'stride': 0, 'marker': 0, 'timer': 0}
for patch in patches:
    # Some original Apple HID header comments contain legacy byte encodings.
    # Preserve those bytes exactly while editing ASCII patch context.
    text = patch.read_text(errors='surrogateescape')
    if int(patch.name[:4]) > 20:
        # 0020-vermeer already changed the CPU personality. git apply checks
        # every context line atomically: leaving the reference name in0038
        # silently dropped AKS and all dependent patches in the old dxb build.
        text = text.replace('qcom,kryo470', 'arm,cortex-a510')
    if ios and patch.name.startswith('0031-'):
        # Retain writable-root selection, but never bypass dirty nonjournaled
        # HFS rejection. Integrated root requires a journaled HFSX header.
        text = text.split('--- a/bsd/sweet_hfs/')[0]
    if gui and patch.name.startswith('0054-'):
        # The reference sampler is started by its UFS/persistent-log driver.
        # Keep only generic sampler/trace code. Our0060 supplies the hook;
        # no reference storage source or partition-tail writes are included.
        text = ''.join(section for section in re.split(r'(?=^--- (?:a/|/dev/null))', text, flags=re.M)
                       if not section.startswith(('--- a/bsd/conf/files', '--- a/bsd/dev/sweet_ufs.c')))
    if gui and patch.name.startswith('0046-'):
        text = text.replace('#define SWEET_FB_DOT_PITCH              395',
                            '#define SWEET_FB_DOT_PITCH              526')
        text = text.replace('/* ppi, 6.67" 1080x2400 */', '/* K70 panel density */')
    if gui and patch.name.startswith('0058-'):
        for old, new in [('x > 1079 ? 1079', 'x > 1439 ? 1439'),
                         ('y > 2399 ? 2399', 'y > 3199 ? 3199'),
                         (' / 1080)', ' / 1440)'), (' / 2400)', ' / 3200)')]:
            assert text.count(old) == 1, old
            text = text.replace(old, new)
    for key, old, new in (
        ('framebuffer', '0x9c000000', '0xb8000000'),
        ('width', '1080u', '1440u'),
        ('height', '2400u', '3200u'),
        ('stride', '4320', '5760'),
        ('marker', '2368', '3168'),
    ):
        counts[key] += text.count(old)
        text = text.replace(old, new)
    text, n = re.subn(r'(\+\s*#define SWEET_VTIMER_INTID\s+)19\b', r'\g<1>27', text)
    counts['timer'] += n
    (target / patch.name).write_text(text, errors='surrogateescape')
assert counts == {'framebuffer': 6, 'width': 2, 'height': 2, 'stride': 4, 'marker': 1, 'timer': 1}, counts
extras = ('0019-vermeer-early-io-budget.patch', '0020-vermeer-boot-cpu-match.patch')
if ufs_probe:
    extras += (('0021-vermeer-ios-root-hook.patch' if ios else '0021-vermeer-ufs-read-probe-hook.patch'),)
for name in extras:
    extra = source.parent / 'vermeer' / name
    if ufs_block and not ios and name == '0021-vermeer-ufs-read-probe-hook.patch':
        data = extra.read_text()
        data = data.replace('bsd/dev/vermeer_ufs_probe.c', 'bsd/dev/vermeer_ufs_block.c')
        data = data.replace('vermeer_ufs_probe', 'vermeer_ufs_block_init')
        data = data.replace('self-test returned', 'block adapter returned')
        # The opt-in bootarg is short and separate from the established probe.
        data = data.replace('"vermeer_ufs_block_init"', '"vermeer_ufs_block"')
        (target / extra.name).write_text(data)
    else:
        shutil.copyfile(extra, target / extra.name)
if ufs_probe:
    generated = ''
    names = ('vermeer_ufs_probe.c', 'vermeer_ufs_response.h', 'vermeer_ufs_dma.h')
    if ufs_block:
        names += ('vermeer_ufs_block.c', 'vermeer_gpt.h')
    if ios:
        names += ('vermeer_ufs_ios.c', 'vermeer_media_policy.h', 'vermeer_ios_bootstrap.c',
                  'vermeer_log_ring.h', 'vermeer_debug.c',
                  'vermeer_usb_protocol.h', 'vermeer_usb_debug.c')
    for name in names:
        data = (source.parent / 'vermeer' / name).read_text()
        path = 'bsd/dev/' + name
        generated += f'diff --git a/{path} b/{path}\nnew file mode 100644\n'
        generated += ''.join(difflib.unified_diff([], data.splitlines(True),
                                                fromfile='/dev/null', tofile='b/' + path))
    (target / '0022-vermeer-ufs-read-probe-source.patch').write_text(generated)
if ios:
    shutil.copyfile(source.parent / 'vermeer/0023-vermeer-ios-bootstrap.patch',
                    target / '0023-vermeer-ios-bootstrap.patch')
    shutil.copyfile(source.parent / 'vermeer/0024-vermeer-usb-debug-hook.patch',
                    target / '0024-vermeer-usb-debug-hook.patch')
if gui:
    shutil.copyfile(source.parent / 'vermeer/0060-vermeer-gui-guards.patch',
                    target / '0060-vermeer-gui-guards.patch')
if ramroot:
    stages = sorted(p for p in source.glob('*.sh') if int(p.name[:4]) <= last_patch)
    assert len(stages) == (3 if hfs_ramroot else 2), 'Unexpected staging scripts'
    for stage in stages:
        shutil.copyfile(stage, target / stage.name)
print(f'vermeer early profile: {len(patches)} reference patches plus {len(extras)} vermeer patches; changes: {counts}')
