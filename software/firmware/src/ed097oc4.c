#include "ed097oc4.h"

#include <string.h>

#include "driver/periph_ctrl.h"
#include "esp_intr.h"
#include "soc/i2s_struct.h"
#include "soc/i2s_reg.h"
#include "soc/rtc.h"
#include "rom/lldesc.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// compensate for big-endian ordering in output data.
int I2S_GPIO_BUS[] = {D6, D7, D4, D5, D2, D3, D0, D1, -1, -1, -1, -1, -1, -1, -1, -1};

typedef struct {
    volatile lldesc_t *dma_desc_a;
    volatile lldesc_t *dma_desc_b;
} i2s_parallel_state_t;

static i2s_parallel_state_t i2s_state;

uint8_t buf_a[EPD_LINE_BYTES];
uint8_t buf_b[EPD_LINE_BYTES];

int current_buffer = 0;

volatile bool output_done = true;
static intr_handle_t gI2S_intr_handle = NULL;

inline void gpio_set_hi(gpio_num_t gpio_num)
{
        gpio_set_level(gpio_num, 1);
}


inline void gpio_set_lo(gpio_num_t gpio_num)
{
        gpio_set_level(gpio_num, 0);
}

/*
Write bits directly using the registers.  Won't work for some signals
(>= 32). May be too fast for some signals.
*/
inline void fast_gpio_set_hi(gpio_num_t gpio_num)
{
    GPIO.out_w1ts = (1 << gpio_num);
}


inline void fast_gpio_set_lo(gpio_num_t gpio_num)
{

    GPIO.out_w1tc = (1 << gpio_num);
}

void IRAM_ATTR busy_delay(uint32_t cycles) {
    volatile unsigned long counts = xthal_get_ccount() + cycles;
    while (xthal_get_ccount() < counts) {};
}

static void fill_dma_desc(volatile lldesc_t *dmadesc, uint8_t *buf) {
    dmadesc->size=EPD_LINE_BYTES;
    dmadesc->length=EPD_LINE_BYTES;
    dmadesc->buf=buf;
    dmadesc->eof=1;
    dmadesc->sosf=1;
    dmadesc->owner=1;
    dmadesc->qe.stqe_next=0;
    dmadesc->offset=0;
}

static void gpio_setup_out(int gpio, int sig, bool invert) {
    if (gpio==-1) return;
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[gpio], PIN_FUNC_GPIO);
    gpio_set_direction(gpio, GPIO_MODE_DEF_OUTPUT);
    gpio_matrix_out(gpio, sig, invert, false);
}

volatile int empty_count = 0;

//ISR handler. Call callback to refill buffer that was just finished.
static void IRAM_ATTR i2s_int_hdl(void *arg) {
    i2s_dev_t* dev = &I2S1;
    if (dev->int_st.out_done) {
        //dev->int_ena.tx_rempty=1;
        //empty_count = 0;
        // Clear the interrupt. Otherwise, the whole device would hang.
        fast_gpio_set_hi(STH);
        output_done = true;
    }
    dev->int_clr.val = dev->int_raw.val;
}

uint8_t* get_current_buffer() {
    return current_buffer ? buf_b : buf_a;
}

uint32_t dma_desc_addr() {
    return (uint32_t)(current_buffer ? i2s_state.dma_desc_b : i2s_state.dma_desc_a);
}

void switch_buffer() {
    current_buffer = !current_buffer;
}

void i2s_setup(i2s_dev_t *dev) {

    // Use I2S1 with no signal offset (for some reason the offset seems to be needed
    // in 16-bit mode, but not in 8 bit mode.
    int signal_base = I2S1O_DATA_OUT0_IDX;

    // Setup and route GPIOS
    for (int x=0; x<8; x++) {
        gpio_setup_out(I2S_GPIO_BUS[x], signal_base+x, false);
    }
    // Invert word select signal
    gpio_setup_out(CKH, I2S1O_WS_OUT_IDX, true);

    periph_module_enable(PERIPH_I2S1_MODULE);

    // Initialize device
    dev->conf.tx_reset=1; dev->conf.tx_reset=0;

    // Reset DMA
    dev->lc_conf.in_rst=1; dev->lc_conf.in_rst=0;
    dev->lc_conf.out_rst=1; dev->lc_conf.out_rst=0;

    //////////// Setup I2S config. See section 12 of Technical Reference Manual //////////
    // Enable LCD mode
    dev->conf2.val=0;
    dev->conf2.lcd_en=1;

    // Enable FRAME1-Mode (See technical reference manual)
    dev->conf2.lcd_tx_wrx2_en=1;
    dev->conf2.lcd_tx_sdx2_en=0;

    // Set to 8 bit parallel output
    dev->sample_rate_conf.val=0;
    dev->sample_rate_conf.tx_bits_mod=8;

    // Half speed of bit clock in LCD mode.
    dev->sample_rate_conf.tx_bck_div_num=2;

    // Initialize Audio Clock (APLL) for 48 Mhz.
    rtc_clk_apll_enable(1,0,0,8,3);

    // Enable undivided Audio Clock
    dev->clkm_conf.val=0;
    dev->clkm_conf.clka_en=1;
    dev->clkm_conf.clkm_div_a=1;
    dev->clkm_conf.clkm_div_b=0;

    // The smallest stable divider for the internal PLL is 6 (13.33 MHz),
    // While with the APLL, the display is stable up to 48 MHz!
    // (Which is later halved by bck_div, so we use 24 MHz)
    dev->clkm_conf.clkm_div_num=1;

    // Set up FIFO
    dev->fifo_conf.val=0;
    dev->fifo_conf.tx_fifo_mod_force_en=1;
    dev->fifo_conf.tx_fifo_mod=1;
    dev->fifo_conf.tx_data_num=32;
    dev->fifo_conf.dscr_en=1;

    // Stop after transmission complete
    dev->conf1.val=0;
    dev->conf1.tx_stop_en=1;
    dev->conf1.tx_pcm_bypass=1;

    // Configure TX channel
    dev->conf_chan.val=0;
    dev->conf_chan.tx_chan_mod=1;
    dev->conf.tx_right_first=1;

    dev->timing.val=0;

    //Allocate DMA descriptors
    i2s_state.dma_desc_a=heap_caps_malloc(sizeof(lldesc_t), MALLOC_CAP_DMA);
    i2s_state.dma_desc_b=heap_caps_malloc(sizeof(lldesc_t), MALLOC_CAP_DMA);

    //and fill them
    fill_dma_desc(i2s_state.dma_desc_a, buf_a);
    fill_dma_desc(i2s_state.dma_desc_b, buf_b);

    // enable "done" interrupt
    SET_PERI_REG_BITS(I2S_INT_ENA_REG(1), I2S_OUT_DONE_INT_ENA_V, 1, I2S_OUT_DONE_INT_ENA_S);
    // register interrupt
    esp_intr_alloc(ETS_I2S1_INTR_SOURCE, 0, i2s_int_hdl, 0, &gI2S_intr_handle);

    // Reset FIFO/DMA
    dev->lc_conf.in_rst=1; dev->lc_conf.out_rst=1; dev->lc_conf.ahbm_rst=1; dev->lc_conf.ahbm_fifo_rst=1;
    dev->lc_conf.in_rst=0; dev->lc_conf.out_rst=0; dev->lc_conf.ahbm_rst=0; dev->lc_conf.ahbm_fifo_rst=0;
    dev->conf.tx_reset=1; dev->conf.tx_fifo_reset=1; dev->conf.rx_fifo_reset=1;
    dev->conf.tx_reset=0; dev->conf.tx_fifo_reset=0; dev->conf.rx_fifo_reset=0;


    // Start dma on front buffer
    dev->lc_conf.val=I2S_OUT_DATA_BURST_EN | I2S_OUTDSCR_BURST_EN |I2S_OUT_DATA_BURST_EN;
    dev->out_link.addr=((uint32_t)(i2s_state.dma_desc_a));
    dev->out_link.start=1;

    dev->int_clr.val = dev->int_raw.val;
    dev->int_ena.out_dscr_err=1;

    dev->int_ena.val=0;
    dev->int_ena.out_done=1;

    dev->conf.tx_start=1;
}

/*
 * Pulses the horizontal clock to advance the horizontal latch 2 pixels.
 */
inline void next_pixel() {
    fast_gpio_set_hi(CKH);
    fast_gpio_set_lo(CKH);
}

void init_gpios() {

    /* Power Control Output/Off */
    gpio_set_direction(POS_CTRL, GPIO_MODE_OUTPUT);
    gpio_set_lo(POS_CTRL);
    gpio_set_direction(NEG_CTRL, GPIO_MODE_OUTPUT);
    gpio_set_lo(NEG_CTRL);
    gpio_set_direction(SMPS_CTRL, GPIO_MODE_OUTPUT);
    gpio_set_hi(SMPS_CTRL);

    /* Edges/Clocks */
    gpio_set_direction(CKH, GPIO_MODE_OUTPUT);
    gpio_set_lo(CKH);
    gpio_set_direction(LEH, GPIO_MODE_OUTPUT);
    gpio_set_lo(LEH);

    /* Control Lines */
    gpio_set_direction(MODE, GPIO_MODE_OUTPUT);
    gpio_set_lo(MODE);
    gpio_set_direction(STH, GPIO_MODE_OUTPUT);
    gpio_set_lo(STH);
    gpio_set_direction(CKV, GPIO_MODE_OUTPUT);
    gpio_set_lo(CKV);
    gpio_set_direction(STV, GPIO_MODE_OUTPUT);
    gpio_set_lo(STV);
    gpio_set_direction(OEH, GPIO_MODE_OUTPUT);
    gpio_set_lo(OEH);

    /* Output lines are set up in i2s_setup */

    // Setup I2S
    i2s_setup(&I2S1);
}

void epd_poweron() {
    // POWERON
    gpio_set_lo(SMPS_CTRL);
    busy_delay(100 * 240);
    gpio_set_hi(NEG_CTRL);
    busy_delay(500 * 240);
    gpio_set_hi(POS_CTRL);
    busy_delay(100 * 240);
    gpio_set_hi(STV);
    gpio_set_hi(STH);
    // END POWERON
}

void epd_poweroff() {
    // POWEROFF
    gpio_set_lo(POS_CTRL);
    busy_delay(10 * 240);
    gpio_set_lo(NEG_CTRL);
    busy_delay(100 * 240);
    gpio_set_hi(SMPS_CTRL);
    // END POWEROFF
}

void start_frame() {
    // VSCANSTART
    gpio_set_hi(MODE);
    busy_delay(10 * 240);

    gpio_set_hi(STV);
    gpio_set_lo(CKV);
    busy_delay(240);
    gpio_set_hi(CKV);

    gpio_set_lo(STV);
    gpio_set_lo(CKV);
    busy_delay(240);
    gpio_set_hi(CKV);

    gpio_set_hi(STV);
    gpio_set_lo(CKV);
    busy_delay(240);
    gpio_set_hi(CKV);


    gpio_set_hi(OEH);
    // END VSCANSTART

    skip();
    skip();
    skip();
}

inline void latch_row()
{
    gpio_set_hi(LEH);
    gpio_set_hi(CKH);
    gpio_set_lo(CKH);
    gpio_set_hi(CKH);
    gpio_set_lo(CKH);

    gpio_set_lo(LEH);
    gpio_set_hi(CKH);
    gpio_set_lo(CKH);
    gpio_set_hi(CKH);
    gpio_set_lo(CKH);
}


// This needs to be in IRAM, otherwise we get weird delays!
void IRAM_ATTR wait_line(uint32_t output_time_us) {
    taskDISABLE_INTERRUPTS();
    fast_gpio_set_hi(CKV);
    busy_delay(output_time_us * 240);
    fast_gpio_set_lo(CKV);
    taskENABLE_INTERRUPTS();
}

/*
 * Start shifting out the current buffer via I2S.
 */
void start_line_output() {
    i2s_dev_t* dev = &I2S1;
    dev->conf.tx_start = 0;
    dev->conf.tx_reset=1; dev->conf.tx_fifo_reset=1; dev->conf.rx_fifo_reset=1;
    dev->conf.tx_reset=0; dev->conf.tx_fifo_reset=0; dev->conf.rx_fifo_reset=0;
    dev->out_link.addr = dma_desc_addr();
    dev->out_link.start=1;
    dev->conf.tx_start = 1;
}

void skip() {
    latch_row();

    fast_gpio_set_hi(CKV);
    busy_delay(100);
    fast_gpio_set_lo(CKV);
}

void output_row(uint32_t output_time_us, uint8_t* data)
{

    // wait for dma to be done with the line
    while (!output_done) {};
    // now wait until the fifo buffer is empty as well.
    while (!I2S1.state.tx_idle) {};

    gpio_set_hi(CKH);
    gpio_set_lo(CKH);
    gpio_set_hi(CKH);
    gpio_set_lo(CKH);

    latch_row();


    if (data != NULL) {
        memcpy(get_current_buffer(), data, EPD_LINE_BYTES);
        // switch lower and upper 16 bits to account for I2S fifo order
        uint32_t* rp = (uint32_t*)get_current_buffer();
        for (uint32_t i = 0; i < EPD_LINE_BYTES/4; i++) {
            uint32_t val = *rp;
            *(rp++) = val >> 16 | ((val & 0x0000FFFF) << 16);
        }

        output_done = false;
        gpio_set_lo(STH);
        start_line_output();

        switch_buffer();

        // sth is pulled up through peripheral interrupt
    }

    wait_line(output_time_us);
}

void end_frame() {
    gpio_set_lo(OEH);
    gpio_set_lo(MODE);
}

void enable_output() {
    gpio_set_hi(OEH);
}

void disable_output() {
    gpio_set_lo(OEH);
}
