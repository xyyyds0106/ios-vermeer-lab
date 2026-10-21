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
    # Fixed interrupt cells alone cannot authorize a different IRQ parent or
    # nexus. Exercise the production gate with well-formed alternate trees.
    irq_paths=('/', '/soc', '/soc/apps-smmu@15000000',
        '/soc/qcom,gpi-dma@a00000', '/soc/qcom,qupv3_1_geni_se@ac0000',
        '/soc/qcom,qupv3_1_geni_se@ac0000/spi@a90000')
    cases=0
    for path in irq_paths:
        for key,value,expected in (
            ('interrupt-parent',bytes.fromhex('00000001'),0),
            ('interrupt-parent',bytes.fromhex('00000002'),18),
            ('interrupt-parent',b'',18),
            ('interrupts-extended',bytes(16),18),
            ('interrupt-map',b'',18),
            ('interrupt-map-mask',bytes(4),18)):
            changed=out/'irq-override.dtb'
            changed.write_bytes(fixture(((path,key,value),)))
            subprocess.run([str(out/'native'),str(changed),str(expected)],check=True,
                env={**os.environ,'UBSAN_OPTIONS':'halt_on_error=1'})
            cases+=1
    print(f'Native FDT IRQ ancestry: {cases} parent/nexus override cases PASS',flush=True)
    clock_cases=[]
    for path in ('/','/soc','/soc/qcom,qupv3_1_geni_se@ac0000',
                 '/soc/qcom,qupv3_1_geni_se@ac0000/spi@a90000'):
        for key in ('qcom,le-vm','qcom,la-vm','qcom,shared_se','qcom,shared_ee'):
            clock_cases.append((path,key,b'',19))
    clock_cases += [
        ('/soc/clocks/xo_board','clock-frequency',bytes.fromhex('0124f800'),11),
        ('/soc/clocks/xo_board','clock-output-names',b'other_xo\0',11),
        ('/soc/clock-controller@100000','clocks',bytes(40),11),
        ('/soc/qcom,qupv3_1_geni_se@ac0000/spi@a90000','clocks',bytes(8),11),
        ('/soc','phandle',bytes.fromhex('00000037'),13),
        ('/soc','phandle',bytes.fromhex('00000038'),13),
        ('/soc','phandle',bytes.fromhex('000003ab'),13)]
    for path,key,value,expected in clock_cases:
        changed=out/'clock-override.dtb';changed.write_bytes(fixture(((path,key,value),)))
        subprocess.run([str(out/'native'),str(changed),str(expected)],check=True,
            env={**os.environ,'UBSAN_OPTIONS':'halt_on_error=1'})
    print(f'Native AP clock chain: {len(clock_cases)} VM/shared/oscillator/provider/alias refusal cases PASS',flush=True)
