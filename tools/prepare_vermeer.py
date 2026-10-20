"""Prepare the first 18 reference patches for a storage-free vermeer kernel.

Keep the upstream patch series untouched. Rewrite matching added/removed/context
lines consistently so subsequent patches still apply. This profile intentionally
does not include the later UFS, USB, touch, battery or iOS GUI drivers.
"""
from pathlib import Path
import re
import shutil
import sys

source, target = map(Path, sys.argv[1:3])
target.mkdir(parents=True, exist_ok=True)
if any(target.iterdir()):
    raise SystemExit('Destination must be empty')
patches = sorted(p for p in source.glob('*.patch') if int(p.name[:4]) <= 18)
assert len(patches) == 18, 'Expected complete reference series 0001..0018'
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
extra = source.parent / 'vermeer' / '0019-vermeer-early-io-budget.patch'
shutil.copyfile(extra, target / extra.name)
print(f'vermeer early profile: {len(patches)} reference patches plus vermeer I/O budget patch; changes: {counts}')
