#!/usr/bin/env python3
"""Host-test the production board flow with only native service boundaries replaced."""
from pathlib import Path
import os,re,subprocess,tempfile,sys
from rpmh_native_board_fixture import fixture
root=Path(__file__).resolve().parent.parent
source=(root/'xnu/vermeer/vermeer_rpmh_board.c').read_text(encoding='utf-8-sig')
# Remove only native kernel include directives, retain every local header.
source=re.sub(r'^#include <[^>]+>\n','',source,flags=re.M)
assert source.count('__asm__ volatile("dsb sy":::"memory");')==2
assert source.count('__asm__ volatile("dmb oshld":::"memory");')==1
source=source.replace('__asm__ volatile("dsb sy":::"memory");','atomic_thread_fence(memory_order_seq_cst);')
source=source.replace('__asm__ volatile("dmb oshld":::"memory");','atomic_thread_fence(memory_order_acquire);')
assert source.count('uint32_t v=*(volatile uint32_t *)va;')==1
assert source.count('*(volatile uint32_t *)va=value;')==1
source=source.replace('uint32_t v=*(volatile uint32_t *)va;','uint32_t v=model_load(va,pa);')
source=source.replace('*(volatile uint32_t *)va=value;','model_store(va,pa,value);')
with tempfile.TemporaryDirectory() as tmp:
    out=Path(tmp);(out/'board_native_model.c').write_text(source)
    boundary=(root/'tests/rpmh_native_power_if_fixture.h').read_text()
    canonical=root/'xnu/vermeer/vermeer_touch_power_if.h'
    if canonical.exists():
        def abi(text):
            text=re.sub(r'/\*.*?\*/','',text,flags=re.S)
            body=re.search(r'struct vtp_ops\s*\{[^}]*\}',text).group()
            return re.sub(r'\s+','',body)
        assert abi(boundary)==abi(canonical.read_text()),'touch power ABI changed'
    (out/'vermeer_touch_power_if.h').write_text(boundary)
    compiler=os.environ.get('CC','clang-14')
    subprocess.run([compiler,'-std=gnu11','-Wall','-Wextra','-Werror',
        '-Wno-gnu-statement-expression','-g','-O1','-no-pie','-fsanitize=address,undefined',
        '-I'+str(out),'-I'+str(root/'xnu/vermeer'),'-I'+str(root/'gpu/AdrenoFirmware'),
        str(root/'tests/rpmh_native_board_test.c'),str(root/'gpu/AdrenoFirmware/AdrenoCmdDb.c'),'-o',str(out/'native')],check=True)
    synthetic=out/'synthetic-fixed-contract.dtb';synthetic.write_bytes(fixture())
    fdt=Path(sys.argv[1]) if len(sys.argv)==2 else synthetic
    subprocess.run([str(out/'native'),str(fdt)],check=True,
        env={**os.environ,'UBSAN_OPTIONS':'halt_on_error=1'})
