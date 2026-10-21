from pathlib import Path
import os, re, subprocess, sys
root=Path(__file__).resolve().parent.parent
out=root.parent/'build/tmp'
out.mkdir(parents=True,exist_ok=True)
def function(source,signature):
    begin=source.index(signature);at=source.index('{',begin);depth=1;end=at+1
    while depth:
        if source[end]=='{':depth+=1
        elif source[end]=='}':depth-=1
        end+=1
    return source[begin:end]
touch=(root/'xnu/vermeer/vermeer_touch.c').read_text()
usb=(root/'xnu/vermeer/vermeer_usb_debug.c').read_text()
enum=re.search(r'enum \{VT_MANUAL_OFF,[\s\S]*?\};',touch).group()
header='''#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
static int quiet(const char *format,...){(void)format;return 0;}
#define printf quiet
#define os_atomic_load(p,o) atomic_load_explicit(p,memory_order_##o)
#define os_atomic_store(p,v,o) atomic_store_explicit(p,v,memory_order_##o)
static int exchange(_Atomic unsigned *p,unsigned old,unsigned value){return atomic_compare_exchange_strong(p,&old,value);}
#define os_atomic_cmpxchg(p,a,b,o) exchange(p,a,b)
static struct {bool failed,configured,dtr;}u;
static unsigned _Atomic usb_ready;
static struct {unsigned manual_mode;unsigned _Atomic manual_phase;}touch;
'''
code=header+enum+'\n'+function(usb,'int vermeer_usb_debug_ready(void)\n{')+'\n'+function(usb,'static void update_ready(void)')+'\n'+function(usb,'static void fail(const char *why)')+'\n'+function(touch,'static int vt_manual_request(const char *text,unsigned n)')+r'''
int main(void){
  for(unsigned bits=0;bits<8;bits++){
    u.failed=bits&1;u.configured=bits&2;u.dtr=bits&4;update_ready();
    assert(vermeer_usb_debug_ready()==(bits==6));
  }
  u.failed=false;u.configured=u.dtr=true;update_ready();
  assert(vermeer_usb_debug_ready());fail("test");assert(!vermeer_usb_debug_ready());
  touch.manual_mode=2;touch.manual_phase=VT_MANUAL_POWER_WAIT;
  assert(vt_manual_request("touch-power",11) && touch.manual_phase==VT_MANUAL_POWER_WAIT);
  u.failed=false;update_ready();assert(vermeer_usb_debug_ready());
  assert(!vt_manual_request(NULL,11) && !vt_manual_request("touch-power!",12));
  assert(!vt_manual_request("touch-start!",12) && !vt_manual_request("poke-mmio",9));
  assert(vt_manual_request("touch-start",11) && touch.manual_phase==VT_MANUAL_POWER_WAIT);
  touch.manual_mode=1;assert(vt_manual_request("touch-power",11) && touch.manual_phase==VT_MANUAL_POWER_WAIT);
  touch.manual_mode=2;assert(vt_manual_request("touch-power",11) && touch.manual_phase==VT_MANUAL_POWER_QUEUED);
  assert(vt_manual_request("touch-power",11) && touch.manual_phase==VT_MANUAL_POWER_QUEUED);
  assert(vt_manual_request("touch-start",11) && touch.manual_phase==VT_MANUAL_POWER_QUEUED);
  touch.manual_phase=VT_MANUAL_POWERING;
  assert(vt_manual_request("touch-start",11) && touch.manual_phase==VT_MANUAL_POWERING);
  touch.manual_phase=VT_MANUAL_START_WAIT;u.dtr=false;update_ready();
  assert(vt_manual_request("touch-start",11) && touch.manual_phase==VT_MANUAL_START_WAIT);
  u.dtr=true;update_ready();
  assert(vt_manual_request("touch-start",11) && touch.manual_phase==VT_MANUAL_START_QUEUED);
  assert(vt_manual_request("touch-start",11) && touch.manual_phase==VT_MANUAL_START_QUEUED);
  touch.manual_phase=VT_MANUAL_RUNNING;
  assert(vt_manual_request("touch-power",11) && touch.manual_phase==VT_MANUAL_RUNNING);
  touch.manual_phase=VT_MANUAL_STOPPED;
  assert(vt_manual_request("touch-start",11) && touch.manual_phase==VT_MANUAL_STOPPED);
  puts("Manual touch: actual queue callback and software USB readiness, exact commands, no automatic start, ordered one-shot power/start, disconnect/failure refusal PASS");
}
'''
source=out/'manual-touch-actual-test.c';binary=out/'manual-touch-actual-test'
source.write_text(code)
env=dict(os.environ,TMPDIR=str(out),XDG_CACHE_HOME=str(root.parent/'build/cache'),PYTHONDONTWRITEBYTECODE='1',UBSAN_OPTIONS='halt_on_error=1')
subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',*(['-fno-pie','-no-pie'] if sys.platform=='linux' else []),str(source),'-o',str(binary)],env=env,check=True)
subprocess.run([str(binary)],env=env,check=True)

