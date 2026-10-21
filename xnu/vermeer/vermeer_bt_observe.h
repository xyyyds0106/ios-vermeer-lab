/* Read-only observations for the K70 Bluetooth lane. No MMIO implementation.
 * Caller must prove each mapping/domain is accessible BEFORE enabling reads;
 * ARM64 ml_probe_read is not a fault-safe fallback on this XNU.
 * Source facts: MiCode 53ef33eacdc4f7ac737451e4cfc91963d59e3956
 * msm_geni_serial.c/qcom-geni-se.h/pinctrl-kalama.c, and LineageOS modules
 * 92a9fde479d3262da080cfd28bab0775b0da500a bt-kernel/pwr/btpower.c.
 * CNSS keeps its first selected type (possibly set by WLAN), so this strap
 * hint alone does not establish even the reference driver's selected child.
 * A hint/selected DT compatible is NOT a silicon or firmware identity.
 */
#ifndef VERMEER_BT_OBSERVE_H
#define VERMEER_BT_OBSERVE_H
#include <stdint.h>
#define VBT_OBS_TLMM 0u /* saved K70 DT: f000000, GPIO bank offset 100000 */
#define VBT_OBS_UART 1u /* hsuart0: wrapper2 SE6, 898000; not debug a9c000 */
enum vbt_obs_field {
    VBT_STRAP_CFG_A, VBT_STRAP_IO_A, VBT_STRAP_CFG_B, VBT_STRAP_IO_B,
    VBT_ENABLE_CFG, VBT_ENABLE_IO, VBT_SW_CFG, VBT_SW_IO,
    VBT_FW_M, VBT_FW_S, VBT_IF_DISABLE, VBT_DMA_MODE, VBT_STATUS,
    VBT_CLK_M, VBT_CLK_S, VBT_HW_TX, VBT_HW_RX, VBT_TX_COUNT, VBT_RX_COUNT,
    VBT_TX_PACK0, VBT_TX_PACK1, VBT_RX_PACK0, VBT_RX_PACK1,
    VBT_TX_WORD, VBT_RX_WORD, VBT_TX_STOP, VBT_TX_CONFIG, VBT_RX_CONFIG,
    VBT_CLK_SEL, VBT_OBS_COUNT
};
struct vbt_observation { uint32_t values[VBT_OBS_COUNT], valid; };
struct vbt_obs_register { unsigned bank, offset; };
/* All addresses are offsets in separately verified mappings. There are NO
 * FIFO-data reads (which consume bytes), IRQ clears or command accesses.
 */
static const struct vbt_obs_register vbt_obs_registers[VBT_OBS_COUNT]={
    {0,0x153000},{0,0x153004},{0,0x153000},{0,0x153004}, /* strap83 twice */
    {0,0x151000},{0,0x151004},{0,0x152000},{0,0x152004}, /* EN81/SW82 */
    {1,0x68},{1,0x6c},{1,0x64},{1,0x258},{1,0x40},
    {1,0x48},{1,0x4c},{1,0xe24},{1,0xe28},{1,0x800},{1,0x804},
    {1,0x260},{1,0x264},{1,0x284},{1,0x288},
    {1,0x268},{1,0x28c},{1,0x26c},{1,0x25c},{1,0x280},{1,0x7c}
};
static const char *const vbt_obs_names[VBT_OBS_COUNT]={
    "strap_cfg_a","strap_io_a","strap_cfg_b","strap_io_b",
    "bt_enable_cfg","bt_enable_io","bt_sw_cfg","bt_sw_io",
    "fw_m","fw_s","if_disable","dma_mode","status",
    "clk_m","clk_s","hw_tx","hw_rx","tx_count","rx_count",
    "tx_pack0","tx_pack1","rx_pack0","rx_pack1",
    "tx_word","rx_word","tx_stop","tx_config","rx_config","clk_sel"
};
static inline void vbt_observation_init(struct vbt_observation *s)
{
    if (!s) return;
    s->valid=0;
    for (unsigned i=0;i<VBT_OBS_COUNT;++i) s->values[i]=0;
}
/* Return 1 with a value on success; any other return value stops collection. */
typedef int (*vbt_observe_fn)(void *, unsigned, unsigned, uint32_t *);
/* Separate gates allow TLMM-only collection when UART clocks/power are unknown.
 * Gates are attestations supplied by a real platform owner, not permission to
 * map guessed addresses. A failed read stops the requested bank immediately.
 */
static inline int vbt_observe_bank(struct vbt_observation *out, unsigned bank,
                                 unsigned access_proven, vbt_observe_fn read,
                                 void *ctx)
{
    if (!out || bank>1) return 0;
    for (unsigned i=0;i<VBT_OBS_COUNT;++i)
        if (vbt_obs_registers[i].bank==bank) out->valid &= ~(1u<<i);
    if (!access_proven || !read) return 0;
    for (unsigned i=0;i<VBT_OBS_COUNT;++i) {
        if (vbt_obs_registers[i].bank!=bank) continue;
        uint32_t value;
        if (read(ctx,bank,vbt_obs_registers[i].offset,&value)!=1) return 0;
        out->values[i]=value; out->valid |= 1u<<i;
    }
    return 1;
}
enum vbt_strap_hint { VBT_STRAP_UNKNOWN, VBT_STRAP_KIWI, VBT_STRAP_QCA6490 };
static inline enum vbt_strap_hint vbt_strap_hint(const struct vbt_observation *s)
{
    if (!s || (s->valid&15u)!=15u) return VBT_STRAP_UNKNOWN;
    uint32_t a=s->values[VBT_STRAP_CFG_A], b=s->values[VBT_STRAP_CFG_B];
    /* GPIO mux=0, OE=0, same control state and same input on both samples.
     * Otherwise an output/alternate function may mimic a selection strap.
     */
    if (a!=b || (a&((15u<<2)|(1u<<9))) ||
        ((s->values[VBT_STRAP_IO_A]^s->values[VBT_STRAP_IO_B])&1u))
        return VBT_STRAP_UNKNOWN;
    return (s->values[VBT_STRAP_IO_A]&1u) ? VBT_STRAP_QCA6490 : VBT_STRAP_KIWI;
}
enum vbt_uart_observed_mode {
    VBT_UART_UNKNOWN, VBT_UART_OTHER_PROTOCOL, VBT_UART_GSI_REQUIRED,
    VBT_UART_SE_DMA_OBSERVED, VBT_UART_FIFO_OBSERVED
};
static inline enum vbt_uart_observed_mode vbt_uart_observed_mode(
    const struct vbt_observation *s)
{
    const uint32_t needed=(1u<<VBT_FW_M)|
                           (1u<<VBT_IF_DISABLE)|(1u<<VBT_DMA_MODE);
    if (!s || (s->valid&needed)!=needed) return VBT_UART_UNKNOWN;
    /* Downstream protocol detection uses the master's protocol field. Log
     * secondary revision too, without inventing an additional acceptance rule.
     */
    if (((s->values[VBT_FW_M]>>8)&255u)!=2u) return VBT_UART_OTHER_PROTOCOL;
    /* Exact downstream HS driver: IF_DISABLE bit0 => gsi_mode => GPI DMA.
     * Do not reinterpret that as authorization for the touch SE-DMA path.
     */
    if (s->values[VBT_IF_DISABLE]&1u) return VBT_UART_GSI_REQUIRED;
    return (s->values[VBT_DMA_MODE]&1u) ? VBT_UART_SE_DMA_OBSERVED : VBT_UART_FIFO_OBSERVED;
}
#endif
