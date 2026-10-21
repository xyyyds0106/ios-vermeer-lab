/* MiCode clk-branch.c: an AHB branch in HWCG mode can report CLK_OFF
 * while idle; the vote allows bus accesses to wake it. SE4 has no HWCG bit.
 * A missing vote must never be accepted as a ready clock.
 */
#ifndef VERMEER_TOUCH_CLOCK_H
#define VERMEER_TOUCH_CLOCK_H
#include <stdint.h>
#define VTC_XO_HZ 19200000u /* kalama xo_board76.8MHz / xo_pad2 / bi_tcxo2 */
#define VTC_VOTES ((1u<<18)|(1u<<19)|(1u<<20)|(1u<<21)|(1u<<26))
static inline int vtc_ready(uint32_t votes,uint32_t core2x,uint32_t core,uint32_t master,uint32_t slave,uint32_t se)
{
    if((votes&VTC_VOTES)!=VTC_VOTES)return 0;
    if((core2x|core)&0x80000000u)return 0;
    if((master&0x80000000u) && !(master&2u))return 0;
    if((slave&0x80000000u) && !(slave&2u))return 0;
    return !(se&0x80000000u);
}
/* Ordinary AP GPI selects a hardware DFS level, not the LE-VM-only serial
 * selector read. This deliberately supports only an observed XO bypass
 * entry: no PLL, M/N, rate programming or invented level-zero fallback.
 * Caller first qualifies THIS boot's fixed clock/provider chain and holds
 * SE4/GPI ownership, clocks, RPMh and the PM inhibit until reboot. */
struct vtc_dfs_snapshot {uint32_t control,cfg[8];};
struct vtc_gpi_clock {unsigned level,divider;uint32_t source_hz,spi_hz;};
struct vtc_dfs_ops {
    void *ctx;int (*valid)(void *);
    int (*read)(void *,unsigned,uint32_t *); /* offsets relative to SE4 RCG */
    int (*now_us)(void *,uint64_t *);
};
enum {VTC_DFS_OK,VTC_DFS_ARGUMENT,VTC_DFS_LEASE,VTC_DFS_IO,VTC_DFS_TIME,VTC_DFS_CHANGED};
struct vtc_dfs_cursor {const struct vtc_dfs_ops *io;uint64_t begin,last;};
static inline int vtc_dfs_guard(struct vtc_dfs_cursor *c)
{
    uint64_t now;
    if(c->io->valid(c->io->ctx)!=1)return VTC_DFS_LEASE;
    if(c->io->now_us(c->io->ctx,&now)!=1 || now<c->last || now-c->begin>=20000)return VTC_DFS_TIME;
    if(c->io->valid(c->io->ctx)!=1)return VTC_DFS_LEASE;
    c->last=now;return VTC_DFS_OK;
}
static inline int vtc_dfs_collect(const struct vtc_dfs_ops *io,struct vtc_dfs_snapshot *out)
{
    if(!io || !out || !io->valid || !io->read || !io->now_us)return VTC_DFS_ARGUMENT;
    struct vtc_dfs_cursor c={.io=io};
    if(io->valid(io->ctx)!=1)return VTC_DFS_LEASE;
    if(io->now_us(io->ctx,&c.begin)!=1)return VTC_DFS_TIME;
    c.last=c.begin;
    struct vtc_dfs_snapshot s={0};
    for(unsigned pass=0;pass<2;pass++)for(unsigned i=0;i<9;i++) {
        unsigned off=i?0x1c+4*(i-1):0x14;uint32_t value;int error=vtc_dfs_guard(&c);
        if(error)return error;
        if(io->read(io->ctx,off,&value)!=1)return VTC_DFS_IO;
        if((error=vtc_dfs_guard(&c)))return error;
        if(!pass){if(i)s.cfg[i-1]=value;else s.control=value;}
        else if(i?value!=s.cfg[i-1]:(value&~0x1eu)!=(s.control&~0x1eu))return VTC_DFS_CHANGED;
        /* The active level may move, but the enable/control and every entry
         * must stay stable. Native validation repeats the selected entry. */
        if(!i && (!(value&1u) || ((value>>1)&15u)>=8))return VTC_DFS_CHANGED;
    }
    *out=s;return VTC_DFS_OK;
}
static inline int vtc_dfs_choose(const struct vtc_dfs_snapshot *s,struct vtc_gpi_clock *out)
{
    if(!s || !out || !(s->control&1u) || ((s->control>>1)&15u)>=8)return 0;
    for(unsigned level=0;level<8;level++) {
        uint32_t cfg=s->cfg[level];
        /* GCC parent-map0: selector0 is bi_tcxo. Mode0 bypasses M/N.
         * HID0 (no division) and HID1 (/1) both retain the proven XO rate. */
        if((cfg&~((1u<<20)|0x1fu)) || (cfg&31u)>1)continue;
        *out=(struct vtc_gpi_clock){level,128,VTC_XO_HZ,VTC_XO_HZ/128};return 1;
    }
    return 0;
}
static inline int vtc_dfs_live(const struct vtc_dfs_snapshot *s,const struct vtc_gpi_clock *p,
    uint32_t control,uint32_t selected)
{
    return s && p && p->level<8 && (control&1u) && ((control>>1)&15u)<8 &&
        (control&~0x1eu)==(s->control&~0x1eu) && selected==s->cfg[p->level] &&
        !(selected&~((1u<<20)|0x1fu)) && (selected&31u)<=1 &&
        p->divider==128 && p->source_hz==VTC_XO_HZ && p->spi_hz==VTC_XO_HZ/128;
}
#endif
