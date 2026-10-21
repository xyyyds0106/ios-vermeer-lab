#include <assert.h>
#include <stdio.h>
#include "../xnu/vermeer/vermeer_touch_clock.h"
int main(void)
{
    assert(vtc_ready(VTC_VOTES,0,0,0,0,0));
    assert(vtc_ready(VTC_VOTES,0,0,0x80000002u,0x80000002u,0));
    assert(vtc_ready(VTC_VOTES,0,0,0x80000002u,0,0));
    assert(!vtc_ready(VTC_VOTES,0,0,0x80000000u,0,0));
    assert(!vtc_ready(VTC_VOTES,0,0,0,0x80000000u,0));
    assert(!vtc_ready(VTC_VOTES,0x80000002u,0,0,0,0));
    assert(!vtc_ready(VTC_VOTES,0,0x80000002u,0,0,0));
    /* HWCG interpretation applies ONLY to the two AHB branches. */
    assert(!vtc_ready(VTC_VOTES,0,0,0x80000002u,0x80000002u,0x80000002u));
    for(unsigned bit=0;bit<32;bit++)if(VTC_VOTES&(1u<<bit))
        assert(!vtc_ready(VTC_VOTES&~(1u<<bit),0,0,2,2,0));
    assert(vtc_ready(0xffffffffu,0,0,2,2,0));
    puts("Clock votes, hardware-gated AHB idle and strict SE4 readiness: PASS");
}
