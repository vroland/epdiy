#include "ed097oc4.h"

#include "driver/periph_ctrl.h"
#include "esp_intr.h"
#include "soc/i2s_struct.h"
#include "soc/i2s_reg.h"
#include "soc/rtc.h"
#include "rom/lldesc.h"

#define DMA_MAX (4096-4)

int I2S_GPIO_BUS[] = {D0, D1, D2, D3, D4, D5, D6, D7, -1, -1, -1, -1, -1, -1, -1, -1};

typedef struct {
    void *memory;
    size_t size;
} i2s_parallel_buffer_desc_t;

typedef struct {
    volatile lldesc_t *dma_desc_a;
    volatile lldesc_t *dma_desc_b;
} i2s_parallel_state_t;

static i2s_parallel_state_t i2s_state;

i2s_parallel_buffer_desc_t buf_a;
i2s_parallel_buffer_desc_t buf_b;

int current_buffer = 0;

volatile bool output_done = true;
static intr_handle_t gI2S_intr_handle = NULL;


inline void gpio_set_hi(gpio_num_t gpio_num)
{
        digitalWrite(gpio_num, HIGH);
}


inline void gpio_set_lo(gpio_num_t gpio_num)
{
        digitalWrite(gpio_num, LOW);
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


static void fill_dma_desc(volatile lldesc_t *dmadesc, i2s_parallel_buffer_desc_t *bufdesc) {
    if (bufdesc->size>DMA_MAX) {
        assert("buffer too large!");
    }
    dmadesc->size=bufdesc->size;
    dmadesc->length=bufdesc->size;
    dmadesc->buf=bufdesc->memory;
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

i2s_parallel_buffer_desc_t* get_current_buffer() {
    return current_buffer ? &buf_b : &buf_a;
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
    fill_dma_desc(i2s_state.dma_desc_a, &buf_a);
    fill_dma_desc(i2s_state.dma_desc_b, &buf_b);

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
    pinMode(POS_CTRL, OUTPUT);
    digitalWrite(POS_CTRL, LOW);
    pinMode(NEG_CTRL, OUTPUT);
    digitalWrite(NEG_CTRL, LOW);
    pinMode(SMPS_CTRL, OUTPUT);
    digitalWrite(SMPS_CTRL, HIGH);

    /* Edges/Clocks */
    pinMode(CKH, OUTPUT);
    digitalWrite(CKH, LOW);
    pinMode(LEH, OUTPUT);
    digitalWrite(LEH, LOW);

    /* Control Lines */
    pinMode(MODE, OUTPUT);
    digitalWrite(MODE, LOW);
    pinMode(STH, OUTPUT);
    digitalWrite(STH, LOW);
    pinMode(CKV, OUTPUT);
    digitalWrite(CKV, LOW);
    pinMode(STV, OUTPUT);
    digitalWrite(STV, LOW);
    pinMode(OEH, OUTPUT);
    digitalWrite(OEH, LOW);

    /* Output lines are set up in i2s_setup */

    // malloc the DMA linked list descriptors that i2s_parallel will need
    buf_a.memory = malloc(300);
    buf_a.size = 300;
    buf_b.memory = malloc(300);
    buf_b.size = 300;

    // Setup I2S
    i2s_setup(&I2S1);
}

void epd_poweron() {
    // POWERON
    gpio_set_lo(SMPS_CTRL);
    delayMicroseconds(100);
    gpio_set_hi(NEG_CTRL);
    delayMicroseconds(500);
    gpio_set_hi(POS_CTRL);
    delayMicroseconds(100);
    gpio_set_hi(STV);
    gpio_set_hi(STH);
    // END POWERON
}

void epd_poweroff() {
    // POWEROFF
    gpio_set_lo(POS_CTRL);
    delayMicroseconds(10);
    gpio_set_lo(NEG_CTRL);
    delayMicroseconds(100);
    gpio_set_hi(SMPS_CTRL);
    // END POWEROFF
}

void start_frame() {
    // VSCANSTART
    gpio_set_hi(MODE);
    delayMicroseconds(10);

    gpio_set_hi(STV);
    gpio_set_lo(CKV);
    delayMicroseconds(1);
    gpio_set_hi(CKV);

    gpio_set_lo(STV);
    gpio_set_lo(CKV);
    delayMicroseconds(1);
    gpio_set_hi(CKV);

    gpio_set_hi(STV);
    gpio_set_lo(CKV);
    delayMicroseconds(1);
    gpio_set_hi(CKV);


    gpio_set_hi(OEH);
    // END VSCANSTART
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
    unsigned counts = xthal_get_ccount() + output_time_us * 240;
    while (xthal_get_ccount() < counts) {}
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

void skip(uint16_t width) {
    gpio_set_lo(STH);
    memset(get_current_buffer()->memory, 0, 300);
    gpio_set_hi(STH);
    gpio_set_hi(CKV);
    unsigned counts = xthal_get_ccount() + 480;
    while (xthal_get_ccount() < counts) {}    gpio_set_lo(CKV);
    gpio_set_lo(CKV);
}

void output_row(uint32_t output_time_us, uint8_t* data, uint16_t width)
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

    wait_line(output_time_us);

    if (data != NULL) {
        memcpy(get_current_buffer()->memory, data, 300);

        output_done = false;
        gpio_set_lo(STH);
        start_line_output();

        switch_buffer();

        // sth is pulled up through peripheral interrupt
    }
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
