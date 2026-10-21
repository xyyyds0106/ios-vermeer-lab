/* SPDX-License-Identifier: MIT. Bounded fixed-board FDT gate, no hardware I/O.
 * Input must be the wired copy of THIS boot's FDT, not the saved host fixture. */
#ifndef VERMEER_RPMH_BOARD_DT_H
#define VERMEER_RPMH_BOARD_DT_H
#include <stddef.h>
#include <stdint.h>
#include "vermeer_rpmh_board_contract.h"
static inline uint32_t vrb_be32(const uint8_t *p)
{return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static inline int vrb_string(const uint8_t *p,size_t n,const char *q)
{size_t i=0;while(i<n && q[i] && p[i]==(uint8_t)q[i])i++;return i==n && !q[i];}
static inline int vrb_ancestor(const char *path,size_t n,const char *child)
{size_t i=0;while(i<n && path[i]==child[i] && child[i])i++;return i==n && (n==1 || !child[i] || child[i]=='/');}
static inline int vrb_dt_check(const uint8_t *f,size_t available)
{
    if(!f || available<40 || vrb_be32(f)!=0xd00dfeed)return 1;
    size_t total=vrb_be32(f+4),at=vrb_be32(f+8),strings=vrb_be32(f+12);
    size_t sn=vrb_be32(f+32),size=vrb_be32(f+36);
    if(total<40 || total>available || total>0x200000 || at<40 || (at&3) ||
       at>total || size>total-at || strings<40 || strings>total || sn>total-strings ||
       (at<strings+sn && strings<at+size) || vrb_be32(f+20)!=17 || vrb_be32(f+24)>17)return 2;
    uint8_t seen[VRB_RULE_COUNT]={0},node_seen[VRB_RULE_COUNT]={0};
    char path[512];size_t lengths[64],used=0,limit=at+size;unsigned depth=0,roots=0;
    unsigned gpio_phandles=0,avdd_phandles=0,iovdd_phandles=0,gic_phandles=0;
    while(at<limit) {
        if(limit-at<4)return 3;
        uint32_t token=vrb_be32(f+at);at+=4;
        if(token==1) {
            size_t start=at;while(at<limit && f[at])at++;
            if(at==limit || depth>=64 || at-start>255 || (!depth && (roots++ || at!=start)))return 4;
            lengths[depth]=used;
            if(!depth){path[0]='/';used=1;}
            else {
                if(depth>1){if(used>=sizeof(path)-1)return 4;path[used++]='/';}
                if(at-start>=sizeof(path)-used)return 4;
                for(size_t j=start;j<at;j++){if(f[j]=='/')return 4;path[used++]=(char)f[j];}
            }
            path[used]=0;depth++;at=(at+4)&~(size_t)3;
            for(size_t r=0;r<VRB_RULE_COUNT;r++)if(vrb_string((const uint8_t *)path,used,vrb_rules[r].path)) {
                if(node_seen[r])return 5;
                node_seen[r]=1;
            }
        } else if(token==2) {
            if(!depth)return 6;
            used=lengths[--depth];path[used]=0;
        } else if(token==3) {
            if(!depth || limit-at<8)return 7;
            size_t len=vrb_be32(f+at),no=vrb_be32(f+at+4);at+=8;
            if(len>limit-at || no>=sn)return 8;
            size_t nn=0;while(nn<sn-no && f[strings+no+nn])nn++;
            if(nn==sn-no)return 8;
            if(vrb_string(f+strings+no,nn,"phandle") && len==4) {
                uint32_t v=vrb_be32(f+at);gpio_phandles+=v==0x79;
                avdd_phandles+=v==0x2ec;iovdd_phandles+=v==0x2a;
                gic_phandles+=v==1;
            }
            /* Related IRQs inherit the verified GIC parent directly. A
             * child override or IRQ nexus could invalidate the fixed SPI
             * indexing even while the interrupts byte arrays still match. */
            int irq_ancestor=0;
            for(size_t r=0;r<VRB_RULE_COUNT;r++)
                if(vrb_string((const uint8_t *)vrb_rules[r].name,10,"interrupts") &&
                   vrb_ancestor(path,used,vrb_rules[r].path))irq_ancestor=1;
            if(irq_ancestor && vrb_string(f+strings+no,nn,"interrupt-parent") &&
               (len!=4 || vrb_be32(f+at)!=1))return 18;
            if(irq_ancestor && (vrb_string(f+strings+no,nn,"interrupts-extended") ||
               vrb_string(f+strings+no,nn,"interrupt-map") || vrb_string(f+strings+no,nn,"interrupt-map-mask")))return 18;
            int relevant=0;
            if(vrb_string(f+strings+no,nn,"status"))for(size_t r=0;r<VRB_RULE_COUNT;r++)
                if(vrb_ancestor(path,used,vrb_rules[r].path) &&
                   !((len==3 && vrb_string(f+at,2,"ok") && !f[at+2]) ||
                     (len==5 && vrb_string(f+at,4,"okay") && !f[at+4])))return 9;
            for(size_t r=0;r<VRB_RULE_COUNT;r++)if(vrb_string((const uint8_t *)path,used,vrb_rules[r].path)) {
                relevant=1;
                if(vrb_string(f+strings+no,nn,"status") &&
                   !((len==3 && vrb_string(f+at,2,"ok") && !f[at+2]) ||
                     (len==5 && vrb_string(f+at,4,"okay") && !f[at+4])))return 9;
                if(!vrb_string(f+strings+no,nn,vrb_rules[r].name))continue;
                if(seen[r] || len!=vrb_rules[r].size)return 10;
                for(size_t j=0;j<len;j++)if(f[at+j]!=vrb_rules[r].bytes[j])return 11;
                seen[r]=1;
            }
            /* This profile has regulators only, no extra supply-enable GPIO.
             * Never inherit the vendor driver's missing-GPIO fallback to0. */
            if(relevant && (vrb_string(f+strings+no,nn,"goodix,avdd-gpio") ||
                vrb_string(f+strings+no,nn,"goodix,iovdd-gpio")))return 12;
            at=(at+len+3)&~(size_t)3;
        } else if(token==9) {
            if(depth || roots!=1 || gpio_phandles!=1 || avdd_phandles!=1 || iovdd_phandles!=1 || gic_phandles!=1)return 13;
            for(size_t r=0;r<VRB_RULE_COUNT;r++)if(!seen[r])return 14;
            return 0;
        } else if(token!=4)return 15;
        if(at>limit)return 16;
    }
    return 17;
}
#endif
