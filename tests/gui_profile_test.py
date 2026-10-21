"""Check profile isolation and the hardware-facing device-tree contract."""
from pathlib import Path
import tempfile,subprocess,sys,struct
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'tools'))
import adt,mkvermeeradt
with tempfile.TemporaryDirectory() as folder:
    folder=Path(folder);profile=folder/'profile'
    subprocess.run([sys.executable,str(root/'tools/prepare_vermeer.py'),
                    str(root/'xnu/patches'),str(profile),'--ios-gui'],check=True)
    assert not any(p.name.startswith(('0028-','0030-','0034-','0047-')) for p in profile.iterdir())
    all_patches=''.join(p.read_text(errors='surrogateescape') for p in profile.glob('*.patch'))
    assert '+++ b/bsd/dev/sweet_ufs.c' not in all_patches
    assert 'SWEETLOG' not in all_patches
    for name in ('0046-','0050-','0058-','0059-','0060-','0061-','0062-','0063-','0064-','0065-','0066-','0067-','0068-'):
        assert any(p.name.startswith(name) for p in profile.iterdir())
    assert '+++ b/bsd/dev/vermeer_touch.c' in all_patches
    for name in ('vermeer_geni_dma.h','vermeer_touch_route.h'):
        assert '+++ b/bsd/dev/'+name in all_patches
    assert '+++ b/bsd/dev/sweet_touch.c' not in all_patches
    nongui=folder/'nongui'
    subprocess.run([sys.executable,str(root/'tools/prepare_vermeer.py'),
                    str(root/'xnu/patches'),str(nongui),'--ios'],check=True)
    assert not any('vermeer_touch.c' in p.read_text(errors='surrogateescape') for p in nongui.glob('*.patch'))
    firmware=mkvermeeradt.node('device-tree',children=[mkvermeeradt.node('product',[
        ('product-name',b'iPhone\0')])])
    product=folder/'generic-product.adt';product.write_bytes(adt.serialize(firmware))
    tree=mkvermeeradt.build(True,True,True,product)
    blob=adt.serialize(tree);parsed,used=adt.parse(blob);assert used==len(blob)
    child=lambda node,name:next(c for c in node.children if c.name==name)
    chosen=child(parsed,'chosen');armio=child(parsed,'arm-io');fb=child(parsed,'sweet-fbcon')
    assert struct.unpack('<QQ',fb.prop('reg'))==(0xb8000000,1440*3200*4)
    assert fb.prop('width')==struct.pack('<I',1440) and fb.prop('stride')==struct.pack('<I',5760)
    assert chosen.prop('dram-base')==struct.pack('<Q',0x8c0000000)
    assert chosen.prop('dram-size')==struct.pack('<Q',0x80000000)
    assert chosen.prop('display-scale')==struct.pack('<I',3)
    assert len(chosen.prop('unique-chip-id'))==8
    assert child(armio,'disp0').prop('canvas-width')==struct.pack('<I',1125)
    assert child(armio,'disp0').prop('canvas-height')==struct.pack('<I',2436)
    assert child(armio,'wdt').prop('no-pmu')==struct.pack('<I',1)
    assert child(chosen,'asmb').prop('lp-sip0')==struct.pack('<Q',2)
    assert parsed.prop('model')==b'iPhone10,3\0'
    assert parsed.prop('physical-model')==b'Redmi K70\0'
    diagnostic=mkvermeeradt.build()
    assert diagnostic.prop('model')==b'Redmi K70\0'
    assert child(diagnostic,'chosen').prop('display-scale') is None
    assert not any(n.name=='disp0' for n in child(diagnostic,'arm-io').children)
    try:mkvermeeradt.build(True,True,True)
    except ValueError:pass
    else:raise AssertionError('GUI ADT must not silently omit product metadata')
print('GUI profile excludes reference storage/log writes and preserves K70 memory/display bounds: PASS')
