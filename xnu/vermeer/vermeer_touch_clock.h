/* MiCode clk-branch.c: an AHB branch in HWCG mode can report CLK_OFF
 * while idle; the vote allows bus accesses to wake it. SE4 has no HWCG bit.
 * A missing vote must never be accepted as a ready clock.
 */
#ifndef VERMEER_TOUCH_CLOCK_H
#define VERMEER_TOUCH_CLOCK_H
#include <stdint.h>
#define VTC_VOTES ((1u<<18)|(1u<<19)|(1u<<20)|(1u<<21)|(1u<<26))
static inline int vtc_ready(uint32_t votes,uint32_t core2x,uint32_t core,uint32_t master,uint32_t slave,uint32_t se)
{
    if((votes&VTC_VOTES)!=VTC_VOTES)return 0;
    if((core2x|core)&0x80000000u)return 0;
    if((master&0x80000000u) && !(master&2u))return 0;
    if((slave&0x80000000u) && !(slave&2u))return 0;
    return !(se&0x80000000u);
}
#endif
