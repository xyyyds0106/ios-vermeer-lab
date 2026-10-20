"""Prepare bounded vermeer early/RAM-root/opt-in UFS-test kernel profiles.

Keep the upstream patch series untouched. Rewrite matching added/removed/context
lines consistently so subsequent patches still apply. This profile intentionally
does not include the later reference UFS, USB, touch, battery or iOS GUI drivers.
The UFS-test profile adds only our guarded read self-test beside RAM-root.
"""
from pathlib import Path
import re
import shutil
import sys
import difflib

source, target = map(Path, sys.argv[1:3])
ufs_probe = '--ufs-probe' in sys.argv[3:]
ramroot = '--ramroot' in sys.argv[3:] or ufs_probe
last_patch = 25 if ramroot else 18
target.mkdir(parents=True, exist_ok=True)
if any(target.iterdir()):
    raise SystemExit('Destination must be empty')
patches = sorted(p for p in source.glob('*.patch') if int(p.name[:4]) <= last_patch)
assert len(patches) == last_patch, 'Incomplete reference patch series'
counts = {'framebuffer': 0, 'width': 0, 'height': 0, 'stride': 0, 'marker': 0, 'timer': 0}
for patch in patches:
    text = patch.read_text()
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
    (target / patch.name).write_text(text)
assert counts == {'framebuffer': 6, 'width': 2, 'height': 2, 'stride': 4, 'marker': 1, 'timer': 1}, counts
extras = ('0019-vermeer-early-io-budget.patch', '0020-vermeer-boot-cpu-match.patch')
if ufs_probe:
    extras += ('0021-vermeer-ufs-read-probe-hook.patch',)
for name in extras:
    extra = source.parent / 'vermeer' / name
    shutil.copyfile(extra, target / extra.name)
if ufs_probe:
    generated = ''
    for name in ('vermeer_ufs_probe.c', 'vermeer_ufs_response.h'):
        data = (source.parent / 'vermeer' / name).read_text()
        path = 'bsd/dev/' + name
        generated += f'diff --git a/{path} b/{path}\nnew file mode 100644\n'
        generated += ''.join(difflib.unified_diff([], data.splitlines(True),
                                                fromfile='/dev/null', tofile='b/' + path))
    (target / '0022-vermeer-ufs-read-probe-source.patch').write_text(generated)
if ramroot:
    stages = sorted(p for p in source.glob('*.sh') if int(p.name[:4]) <= last_patch)
    assert len(stages) == 2, 'Expected libpthread and BearSSL staging scripts'
    for stage in stages:
        shutil.copyfile(stage, target / stage.name)
print(f'vermeer early profile: {len(patches)} reference patches plus {len(extras)} vermeer patches; changes: {counts}')
