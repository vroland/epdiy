/**
 * Backported functions to make the LCD-based driver compile with IDF < 5.0
 */

#define RMT_LL_EVENT_TX_DONE(channel)     (1 << (channel))
#define RMT_LL_EVENT_TX_THRES(channel)    (1 << ((channel) + 8))
#define RMT_LL_EVENT_TX_LOOP_END(channel) (1 << ((channel) + 12))
#define RMT_LL_EVENT_TX_ERROR(channel)    (1 << ((channel) + 4))
#define RMT_LL_EVENT_TX_MASK(channel)     (RMT_LL_EVENT_TX_DONE(channel) | RMT_LL_EVENT_TX_THRES(channel) | RMT_LL_EVENT_TX_LOOP_END(channel))

#define RMT_BASECLK_DEFAULT RMT_BASECLK_APB
typedef int rmt_clock_source_t;


__attribute__((always_inline))
static inline uint32_t rmt_ll_tx_get_interrupt_status(rmt_dev_t *dev, uint32_t channel)
{
    return dev->int_st.val & RMT_LL_EVENT_TX_MASK(channel);
}

__attribute__((always_inline))
static inline void rmt_ll_clear_interrupt_status(rmt_dev_t *dev, uint32_t mask)
{
    dev->int_clr.val = mask;
}

static inline void rmt_ll_enable_periph_clock(rmt_dev_t *dev, bool enable)
{
    dev->sys_conf.clk_en = enable; // register clock gating
    dev->sys_conf.mem_clk_force_on = enable; // memory clock gating
}

static inline void rmt_ll_enable_mem_access_nonfifo(rmt_dev_t *dev, bool enable)
{
    dev->sys_conf.apb_fifo_mask = enable;
}

__attribute__((always_inline))
static inline void rmt_ll_tx_fix_idle_level(rmt_dev_t *dev, uint32_t channel, uint8_t level, bool enable)
{
    dev->chnconf0[channel].idle_out_en_n = enable;
    dev->chnconf0[channel].idle_out_lv_n = level;
}
