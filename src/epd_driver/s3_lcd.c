#include "s3_lcd.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "soc/rmt_periph.h"
#include "soc/lcd_periph.h"
#include "driver/rmt_types.h"
#include "driver/rmt_types_legacy.h"
#include "hal/rmt_types.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "driver/rmt_tx.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/gpio.h"
#include "hal/gpio_hal.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
//#include "driver/rmt.h"
#include "soc/rmt_struct.h"
#include "hal/rmt_ll.h"
#include "hal/lcd_ll.h"
#include "soc/lcd_periph.h"
#include "hal/lcd_hal.h"
#include "hal/lcd_ll.h"
#include "hal/dma_types.h"
#include "esp_private/gdma.h"
#include "hal/gdma_ll.h"
#include "rom/cache.h"
#include "lut.h"
#include "esp_private/periph_ctrl.h"

#define TAG "epdiy_s3"

inline int min(int x, int y) { return x < y ? x : y; }
inline int max(int x, int y) { return x > y ? x : y; }

#define S3_LCD_PIXEL_CLOCK             (10 * 1000 * 1000)
#define S3_LCD_PIN_NUM_BK_LIGHT       -1
#define S3_LCD_PIN_NUM_HSYNC          46
#define S3_LCD_PIN_NUM_VSYNC          3
#define S3_LCD_PIN_NUM_CKV            1
#define S3_LCD_PIN_NUM_DE             0
#define S3_LCD_PIN_NUM_PCLK           9
#define S3_LCD_PIN_NUM_MODE           4

#define S3_LCD_PIN_NUM_DATA0          48 // G3
#define S3_LCD_PIN_NUM_DATA1          47 // G4
#define S3_LCD_PIN_NUM_DATA2         21 // G5
#define S3_LCD_PIN_NUM_DATA3         17  // R0
#define S3_LCD_PIN_NUM_DATA4         2  // R1
#define S3_LCD_PIN_NUM_DATA5         42 // R2
#define S3_LCD_PIN_NUM_DATA6         41 // R3
#define S3_LCD_PIN_NUM_DATA7         40 // R4
#define S3_LCD_PIN_NUM_DATA8          14 // B0
#define S3_LCD_PIN_NUM_DATA9          13 // B1
#define S3_LCD_PIN_NUM_DATA10          12 // B2
#define S3_LCD_PIN_NUM_DATA11          11 // B3
#define S3_LCD_PIN_NUM_DATA12          10 // B4
#define S3_LCD_PIN_NUM_DATA13          39 // G0
#define S3_LCD_PIN_NUM_DATA14          38 // G1
#define S3_LCD_PIN_NUM_DATA15         45 // G2

// The pixel number in horizontal and vertical
#define DATA_WIDTH                     16
#define S3_LCD_RES_H              EPD_WIDTH / 8
#define LINE_BYTES                     (EPD_WIDTH / 4)
#define S3_LCD_RES_V               (((EPD_HEIGHT  + 7) / 8) * 8)
#define LINE_BATCH                     800

#define RMT_CKV_CHAN                   RMT_CHANNEL_1

// the RMT channel configuration object
static esp_lcd_panel_handle_t panel_handle = NULL;

/** unset appropriate callback functions for data output in next VSYNC */
static void (*frame_prepare_cb)(void) = NULL;
static bool (*line_source_cb)(uint8_t*) = NULL;
/// line source to be used from the next vsync onwards
static bool (*next_line_source)(uint8_t*) = NULL;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
// The extern line is declared in esp-idf/components/driver/deprecated/rmt_legacy.c. It has access to RMTMEM through the rmt_private.h header
// which we can't access outside the sdk. Declare our own extern here to properly use the RMTMEM smybol defined in components/soc/[target]/ld/[target].peripherals.ld
// Also typedef the new rmt_mem_t struct to the old rmt_block_mem_t struct. Same data fields, different names
typedef rmt_mem_t rmt_block_mem_t ;
extern rmt_block_mem_t RMTMEM;
#endif



void s3_set_line_source(bool(*line_source)(uint8_t*)) {
    line_source_cb = line_source;
}

void s3_set_frame_prepare_cb(void (*cb)(void)) {
    frame_prepare_cb = cb;
}

void s3_delete_frame_prepare_cb() {
    frame_prepare_cb = NULL;
}

typedef struct {
    lcd_hal_context_t hal;
    intr_handle_t vsync_intr;
    intr_handle_t done_intr;

    uint8_t *bounce_buffer[2];
    // size of a single bounce buffer
    size_t bb_size;
    size_t batches;

    // Number of DMA descriptors that used to carry the frame buffer
    size_t num_dma_nodes;
    // DMA channel handle
    gdma_channel_handle_t dma_chan;
    // DMA descriptors pool
    dma_descriptor_t* dma_nodes;
} s3_lcd_t;

static s3_lcd_t lcd;


static bool IRAM_ATTR epd_on_vsync_event() {

    //rmt_ll_tx_stop(&RMT, RMT_CKV_CHAN);
    //gpio_set_level(15, 0);

    //rmt_ll_tx_reset_loop_count(&RMT, RMT_CKV_CHAN);
    //rmt_ll_tx_reset_pointer(&RMT, RMT_CKV_CHAN);

    //rmt_ll_tx_start(&RMT, RMT_CKV_CHAN);

    //gpio_set_level(15, 1);

    return pdFALSE;
}

static IRAM_ATTR bool fill_bounce_buffer(uint8_t* buffer) {
    bool task_awoken = false;
    for (int i=0; i < lcd.bb_size / LINE_BYTES; i++) {
        if (line_source_cb != NULL)  {
            task_awoken |= line_source_cb(&buffer[i * LINE_BYTES]);
        } else {
            memset(&buffer[i * LINE_BYTES], 0x00, LINE_BYTES);
        }
    }
    return task_awoken;
}


static void IRAM_ATTR rmt_interrupt_handler(void *args) {
    uint32_t intr_status = rmt_ll_tx_get_interrupt_status(&RMT, RMT_CKV_CHAN);
    rmt_ll_clear_interrupt_status(&RMT, intr_status);
}

void start_ckv_cycles(int cycles) {

    rmt_ll_tx_enable_loop_count(&RMT, RMT_CKV_CHAN, true);
    rmt_ll_tx_enable_loop_autostop(&RMT, RMT_CKV_CHAN, true);
    rmt_ll_tx_set_loop_count(&RMT, RMT_CKV_CHAN, cycles);
    rmt_ll_tx_reset_pointer(&RMT, RMT_CKV_CHAN);
    rmt_ll_tx_start(&RMT, RMT_CKV_CHAN);
}

void s3_start_transmission() {
    int initial_lines = min(LINE_BATCH, S3_LCD_RES_V);

    // hsync: pulse with, back porch, active width, front porch
    lcd_ll_set_horizontal_timing(lcd.hal.dev, 6, 10, S3_LCD_RES_H, 10);
    lcd_ll_set_vertical_timing(lcd.hal.dev, 1, 5, initial_lines, 1);

    // reset FIFO of DMA and LCD, incase there remains old frame data
    gdma_reset(lcd.dma_chan);
    lcd_ll_stop(lcd.hal.dev);
    lcd_ll_fifo_reset(lcd.hal.dev);
    lcd_ll_enable_auto_next_frame(lcd.hal.dev, true);

    lcd.batches = 0;
    fill_bounce_buffer(lcd.bounce_buffer[0]);

    gpio_set_level(S3_LCD_PIN_NUM_VSYNC, 0);
    // the start of DMA should be prior to the start of LCD engine
    gdma_start(lcd.dma_chan, (intptr_t)&lcd.dma_nodes[0]);
    // delay 1us is sufficient for DMA to pass data to LCD FIFO
    // in fact, this is only needed when LCD pixel clock is set too high
    esp_rom_delay_us(1);
    // start LCD engine
    start_ckv_cycles(initial_lines + 7);
    esp_rom_delay_us(12);
    lcd_ll_start(lcd.hal.dev);
    gpio_set_level(S3_LCD_PIN_NUM_MODE, 1);
    esp_rom_delay_us(3);
    gpio_set_level(S3_LCD_PIN_NUM_VSYNC, 1);
}

void stop_transfer() {
    ESP_ERROR_CHECK(esp_lcd_panel_del(panel_handle));
}


void init_ckv_rmt() {
  periph_module_reset(rmt_periph_signals.groups[0].module);
  periph_module_enable(rmt_periph_signals.groups[0].module);

  rmt_ll_enable_periph_clock(&RMT, true);
  rmt_ll_set_group_clock_src(&RMT, RMT_CKV_CHAN, (rmt_clock_source_t)RMT_BASECLK_DEFAULT, 1, 0, 0);
  rmt_ll_tx_set_channel_clock_div(&RMT, RMT_CKV_CHAN, 8);
  rmt_ll_tx_set_mem_blocks(&RMT, RMT_CKV_CHAN, 2);
  rmt_ll_enable_mem_access_nonfifo(&RMT, true);
  rmt_ll_tx_fix_idle_level(&RMT, RMT_CKV_CHAN, RMT_IDLE_LEVEL_LOW, true);
  rmt_ll_tx_enable_carrier_modulation(&RMT, RMT_CKV_CHAN, false);

  rmt_ll_tx_enable_loop(&RMT, RMT_CKV_CHAN, true);
  //rmt_ll_tx_enable_loop_count(&RMT, RMT_CKV_CHAN, true);
  //rmt_ll_tx_enable_loop_autostop(&RMT, RMT_CKV_CHAN, true);
  //rmt_ll_tx_set_loop_count(&RMT, RMT_CKV_CHAN, S3_LCD_RES_V / 2);
  //rmt_ll_tx_enable_loop(&RMT, RMT_CKV_CHAN, true);

  volatile rmt_item32_t *rmt_mem_ptr =
      &(RMTMEM.chan[RMT_CKV_CHAN].data32[0]);
    rmt_mem_ptr->duration0 = 120;
    rmt_mem_ptr->level0 = 1;
    rmt_mem_ptr->duration1 = 140;
    rmt_mem_ptr->level1 = 0;
  //rmt_mem_ptr[1] = rmt_mem_ptr[0];
  rmt_mem_ptr[1].val = 0;

  // Divide 80MHz APB Clock by 8 -> .1us resolution delay

  gpio_hal_iomux_func_sel(GPIO_PIN_MUX_REG[S3_LCD_PIN_NUM_CKV], PIN_FUNC_GPIO);
  gpio_set_direction(S3_LCD_PIN_NUM_CKV, GPIO_MODE_OUTPUT);
  esp_rom_gpio_connect_out_signal(S3_LCD_PIN_NUM_CKV, rmt_periph_signals.groups[0].channels[RMT_CKV_CHAN].tx_sig, false, 0);
  rmt_ll_enable_interrupt(&RMT, RMT_LL_EVENT_TX_LOOP_END(RMT_CKV_CHAN), true);
  intr_handle_t rmt_intr_handle;
  ESP_ERROR_CHECK(esp_intr_alloc(ETS_RMT_INTR_SOURCE, ESP_INTR_FLAG_SHARED | ESP_INTR_FLAG_IRAM,
                 rmt_interrupt_handler, NULL, &rmt_intr_handle));

}

IRAM_ATTR static void lcd_isr_vsync(void *args)
{
    bool need_yield = false;

    uint32_t intr_status = lcd_ll_get_interrupt_status(lcd.hal.dev);
    lcd_ll_clear_interrupt_status(lcd.hal.dev, intr_status);

    gpio_set_level(15, 0);
    gpio_set_level(15, 1);

    if (intr_status & LCD_LL_EVENT_VSYNC_END) {
        int batches_needed = S3_LCD_RES_V / LINE_BATCH ;
        if (lcd.batches >= batches_needed) {
            lcd_ll_stop(lcd.hal.dev);
            gpio_set_level(S3_LCD_PIN_NUM_MODE, 0);
            rmt_ll_tx_stop(&RMT, RMT_CKV_CHAN);
            if (frame_prepare_cb != NULL) {
                (*frame_prepare_cb)();
            }

        } else {
            int ckv_cycles = 0;
            // last batch
            if (lcd.batches == batches_needed - 1) {
                lcd_ll_enable_auto_next_frame(lcd.hal.dev, false);
                lcd_ll_set_vertical_timing(lcd.hal.dev, 1, 0, S3_LCD_RES_V % LINE_BATCH, 3);
                ckv_cycles = S3_LCD_RES_V % LINE_BATCH + 1;
            } else {
                lcd_ll_set_vertical_timing(lcd.hal.dev, 1, 0, LINE_BATCH, 1);
                ckv_cycles = LINE_BATCH + 1;
            }
            // apparently, this is needed for the new timing to take effect.
            lcd_ll_start(lcd.hal.dev);
            // ensure we skip "vsync" with CKV, so we don't skip a line.
            esp_rom_delay_us(26);
            start_ckv_cycles(ckv_cycles);
        }

        lcd.batches += 1;
    }


    if (intr_status & LCD_LL_EVENT_TRANS_DONE) {
        if (frame_prepare_cb != NULL) {
            (*frame_prepare_cb)();
        }
    }

    //lcd_rgb_panel_restart_transmission_in_isr(rgb_panel);

    if (need_yield) {
        portYIELD_FROM_ISR();
    }
}

IRAM_ATTR static void lcd_isr_trans_done(void *args)
{
    bool need_yield = false;

    uint32_t intr_status = lcd_ll_get_interrupt_status(lcd.hal.dev);
    lcd_ll_clear_interrupt_status(lcd.hal.dev, intr_status);


    // do something

    if (need_yield) {
        portYIELD_FROM_ISR();
    }
}

// ISR handling bounce buffer refill
static IRAM_ATTR bool lcd_rgb_panel_eof_handler(gdma_channel_handle_t dma_chan, gdma_event_data_t *event_data, void *user_data)
{
    dma_descriptor_t *desc = (dma_descriptor_t *)event_data->tx_eof_desc_addr;
    // Figure out which bounce buffer to write to.
    // Note: what we receive is the *last* descriptor of this bounce buffer.
    int bb = (desc == &lcd.dma_nodes[0]) ? 0 : 1;

    //gpio_set_level(15, 0);
    //gpio_set_level(15, 1);

    //if (bb) {
    //    gpio_set_level(15, 0);
    //    gpio_set_level(15, 1);
    //}

    bool need_yield = fill_bounce_buffer(lcd.bounce_buffer[bb]);

    return need_yield;
}

void lcd_com_mount_dma_data(dma_descriptor_t *desc_head, const void *buffer, size_t len)
{
    size_t prepared_length = 0;
    uint8_t *data = (uint8_t *)buffer;
    dma_descriptor_t *desc = desc_head;
    while (len > DMA_DESCRIPTOR_BUFFER_MAX_SIZE) {
        desc->dw0.suc_eof = 0; // not the end of the transaction
        desc->dw0.size = DMA_DESCRIPTOR_BUFFER_MAX_SIZE;
        desc->dw0.length = DMA_DESCRIPTOR_BUFFER_MAX_SIZE;
        desc->dw0.owner = DMA_DESCRIPTOR_BUFFER_OWNER_DMA;
        desc->buffer = &data[prepared_length];
        desc = desc->next; // move to next descriptor
        prepared_length += DMA_DESCRIPTOR_BUFFER_MAX_SIZE;
        len -= DMA_DESCRIPTOR_BUFFER_MAX_SIZE;
    }
    if (len) {
        desc->dw0.suc_eof = 1; // end of the transaction
        desc->dw0.size = len;
        desc->dw0.length = len;
        desc->dw0.owner = DMA_DESCRIPTOR_BUFFER_OWNER_DMA;
        desc->buffer = &data[prepared_length];
        desc = desc->next; // move to next descriptor
        prepared_length += len;
    }
}

static esp_err_t init_dma_trans_link() {

    ESP_LOGI(TAG, "size: %d max: %d", lcd.bb_size, DMA_DESCRIPTOR_BUFFER_MAX_SIZE);

    lcd.dma_nodes[0].dw0.suc_eof = 1;
    lcd.dma_nodes[0].dw0.size = lcd.bb_size;
    lcd.dma_nodes[0].dw0.length = lcd.bb_size;
    lcd.dma_nodes[0].dw0.owner = DMA_DESCRIPTOR_BUFFER_OWNER_CPU;
    lcd.dma_nodes[0].buffer = lcd.bounce_buffer[0];

    lcd.dma_nodes[1].dw0.suc_eof = 1;
    lcd.dma_nodes[1].dw0.size = lcd.bb_size;
    lcd.dma_nodes[1].dw0.length = lcd.bb_size;
    lcd.dma_nodes[1].dw0.owner = DMA_DESCRIPTOR_BUFFER_OWNER_CPU;
    lcd.dma_nodes[1].buffer = lcd.bounce_buffer[1];

    // loop end back to start
    lcd.dma_nodes[0].next = &lcd.dma_nodes[1];
    lcd.dma_nodes[1].next = &lcd.dma_nodes[0];

    // alloc DMA channel and connect to LCD peripheral
    gdma_channel_alloc_config_t dma_chan_config = {
        .direction = GDMA_CHANNEL_DIRECTION_TX,
    };
    ESP_RETURN_ON_ERROR(gdma_new_channel(&dma_chan_config, &lcd.dma_chan), TAG, "alloc DMA channel failed");
    gdma_connect(lcd.dma_chan, GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_LCD, 0));
    gdma_transfer_ability_t ability = {
        .psram_trans_align = 64,
        .sram_trans_align = 4,
    };
    gdma_set_transfer_ability(lcd.dma_chan, &ability);

    gdma_tx_event_callbacks_t cbs = {
        .on_trans_eof = lcd_rgb_panel_eof_handler,
    };
    gdma_register_tx_event_callbacks(lcd.dma_chan, &cbs, NULL);

    return ESP_OK;
}

const int DATA_LINES[16] = {

    S3_LCD_PIN_NUM_DATA6,
    S3_LCD_PIN_NUM_DATA7,

    S3_LCD_PIN_NUM_DATA4,
    S3_LCD_PIN_NUM_DATA5,

    S3_LCD_PIN_NUM_DATA2,
    S3_LCD_PIN_NUM_DATA3,

    S3_LCD_PIN_NUM_DATA0,
    S3_LCD_PIN_NUM_DATA1,

    S3_LCD_PIN_NUM_DATA8,
    S3_LCD_PIN_NUM_DATA9,

    S3_LCD_PIN_NUM_DATA10,
    S3_LCD_PIN_NUM_DATA11,

    S3_LCD_PIN_NUM_DATA12,
    S3_LCD_PIN_NUM_DATA13,

    S3_LCD_PIN_NUM_DATA14,
    S3_LCD_PIN_NUM_DATA15,

};

static esp_err_t s3_lcd_configure_gpio()
{
    // connect peripheral signals via GPIO matrix
    for (size_t i = 0; i < 16; i++) {
        gpio_hal_iomux_func_sel(GPIO_PIN_MUX_REG[DATA_LINES[i]], PIN_FUNC_GPIO);
        gpio_set_direction(DATA_LINES[i], GPIO_MODE_OUTPUT);
        esp_rom_gpio_connect_out_signal(DATA_LINES[i],
                                        lcd_periph_signals.panels[0].data_sigs[i], false, false);
    }
    gpio_hal_iomux_func_sel(GPIO_PIN_MUX_REG[S3_LCD_PIN_NUM_HSYNC], PIN_FUNC_GPIO);
    gpio_set_direction(S3_LCD_PIN_NUM_HSYNC, GPIO_MODE_OUTPUT);
    esp_rom_gpio_connect_out_signal(S3_LCD_PIN_NUM_HSYNC, lcd_periph_signals.panels[0].hsync_sig, false, false);

    //gpio_hal_iomux_func_sel(GPIO_PIN_MUX_REG[S3_LCD_PIN_NUM_VSYNC], PIN_FUNC_GPIO);
    //gpio_set_direction(S3_LCD_PIN_NUM_VSYNC, GPIO_MODE_OUTPUT);
    //esp_rom_gpio_connect_out_signal(S3_LCD_PIN_NUM_VSYNC, lcd_periph_signals.panels[0].vsync_sig, false, false);

    gpio_hal_iomux_func_sel(GPIO_PIN_MUX_REG[S3_LCD_PIN_NUM_PCLK], PIN_FUNC_GPIO);
    gpio_set_direction(S3_LCD_PIN_NUM_PCLK, GPIO_MODE_OUTPUT);
    esp_rom_gpio_connect_out_signal(S3_LCD_PIN_NUM_PCLK, lcd_periph_signals.panels[0].pclk_sig, false, false);

    gpio_hal_iomux_func_sel(GPIO_PIN_MUX_REG[S3_LCD_PIN_NUM_DE], PIN_FUNC_GPIO);
    gpio_set_direction(S3_LCD_PIN_NUM_DE, GPIO_MODE_OUTPUT);
    esp_rom_gpio_connect_out_signal(S3_LCD_PIN_NUM_DE, lcd_periph_signals.panels[0].de_sig, false, false);
    return ESP_OK;
}

esp_err_t epd_lcd_init() {

    esp_err_t ret = ESP_OK;

    gpio_config_t debug_io_conf = {
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask = 1ull << 15,
    };

    gpio_config_t vsync_gpio_conf = {
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask = 1ull << S3_LCD_PIN_NUM_VSYNC,
    };

    gpio_config_t mode_gpio_conf = {
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask = 1ull << S3_LCD_PIN_NUM_MODE,
    };

    gpio_config(&debug_io_conf);
    gpio_config(&vsync_gpio_conf);
    gpio_config(&mode_gpio_conf);

    gpio_set_level(15, 1);
    gpio_set_level(S3_LCD_PIN_NUM_VSYNC, 1);
    gpio_set_level(S3_LCD_PIN_NUM_MODE, 0);

    init_ckv_rmt();

    ESP_LOGI(TAG, "using resolution %dx%d", S3_LCD_RES_H, S3_LCD_RES_V);

    // enable APB to access LCD registers
    periph_module_enable(lcd_periph_signals.panels[0].module);
    periph_module_reset(lcd_periph_signals.panels[0].module);

    // each bounce buffer holds two lines of display data
    lcd.bb_size = 4 * LINE_BYTES;
    assert(lcd.bb_size % (LINE_BYTES) == 0);
    size_t num_dma_nodes = (lcd.bb_size + DMA_DESCRIPTOR_BUFFER_MAX_SIZE - 1) / DMA_DESCRIPTOR_BUFFER_MAX_SIZE;
    ESP_LOGI(TAG, "num dma nodes: %u", num_dma_nodes);
    lcd.dma_nodes = heap_caps_calloc(1, num_dma_nodes * sizeof(dma_descriptor_t) * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_GOTO_ON_FALSE(lcd.dma_nodes, ESP_ERR_NO_MEM, err, TAG, "no mem for rgb panel");

    // alloc bounce buffer
    for (int i = 0; i < 2; i++) {
        // bounce buffer must come from SRAM
        lcd.bounce_buffer[i] = heap_caps_aligned_calloc(4, 1, lcd.bb_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        ESP_RETURN_ON_FALSE(lcd.bounce_buffer[i], ESP_ERR_NO_MEM, TAG, "no mem for bounce buffer");
    }

    lcd_hal_init(&lcd.hal, 0);
    lcd_ll_enable_clock(lcd.hal.dev, true);
    lcd_ll_select_clk_src(lcd.hal.dev, LCD_CLK_SRC_PLL240M);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "set source clock failed");

    // install interrupt service, (LCD peripheral shares the interrupt source with Camera by different mask)
    int isr_flags = (ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_INTRDISABLED) | ESP_INTR_FLAG_SHARED | ESP_INTR_FLAG_LOWMED;
    ret = esp_intr_alloc_intrstatus(lcd_periph_signals.panels[0].irq_id, isr_flags,
                                    (uint32_t)lcd_ll_get_interrupt_status_reg(lcd.hal.dev),
                                    LCD_LL_EVENT_VSYNC_END, lcd_isr_vsync, NULL, &lcd.vsync_intr);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "install interrupt failed");
    ret = esp_intr_alloc_intrstatus(lcd_periph_signals.panels[0].irq_id, isr_flags,
                                    (uint32_t)lcd_ll_get_interrupt_status_reg(lcd.hal.dev),
                                    LCD_LL_EVENT_TRANS_DONE, lcd_isr_vsync, NULL, &lcd.done_intr);

    ESP_GOTO_ON_ERROR(ret, err, TAG, "install interrupt failed");

    lcd_ll_fifo_reset(lcd.hal.dev);
    lcd_ll_reset(lcd.hal.dev);


    // install DMA service
    ret = init_dma_trans_link();
    ESP_GOTO_ON_ERROR(ret, err, TAG, "install DMA failed");

    ret = s3_lcd_configure_gpio();
    ESP_GOTO_ON_ERROR(ret, err, TAG, "configure GPIO failed");


    //uint16_t line_len = S3_LCD_RES_H + panel_config.timings.hsync_front_porch + panel_config.timings.hsync_back_porch + panel_config.timings.hsync_pulse_width;
    //ESP_LOGI(TAG, "line len: %u cycles", line_len);

    // set pclk
    int flags = 0;
    uint32_t freq = lcd_hal_cal_pclk_freq(&lcd.hal, 240000000, S3_LCD_PIXEL_CLOCK, flags);
    ESP_LOGI(TAG, "pclk freq: %lu Hz", freq);
    // pixel clock phase and polarity
    lcd_ll_set_clock_idle_level(lcd.hal.dev, false);
    lcd_ll_set_pixel_clock_edge(lcd.hal.dev, false);

    // enable RGB mode and set data width
    lcd_ll_enable_rgb_mode(lcd.hal.dev, true);
    lcd_ll_set_data_width(lcd.hal.dev, 16);
    lcd_ll_set_phase_cycles(lcd.hal.dev, 0, 0, 1); // enable data phase only

    // number of data cycles is controlled by DMA buffer size
    lcd_ll_enable_output_always_on(lcd.hal.dev, true);
    lcd_ll_set_idle_level(lcd.hal.dev, false, true, true);

    // configure blank region timing
    // RGB panel always has a front and back blank (porch region)
    lcd_ll_set_blank_cycles(lcd.hal.dev, 1, 1);

    // output hsync even in porch region?
    lcd_ll_enable_output_hsync_in_porch_region(lcd.hal.dev, false);
    // generate the hsync at the very beginning of line
    lcd_ll_set_hsync_position(lcd.hal.dev, 1);
    // send next frame automatically in stream mode
    lcd_ll_enable_auto_next_frame(lcd.hal.dev, false);

    lcd_ll_enable_interrupt(lcd.hal.dev, LCD_LL_EVENT_VSYNC_END, true);
    lcd_ll_enable_interrupt(lcd.hal.dev, LCD_LL_EVENT_TRANS_DONE, true);

    // enable intr
    esp_intr_enable(lcd.vsync_intr);
    esp_intr_enable(lcd.done_intr);

    ESP_LOGI(TAG, "LCD init done.");

    //ESP_LOGI(TAG, "Initialize RGB LCD panel");
    //s3_start_transmission();

    //Cache_WriteBack_Addr((uint32_t)buf1, EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES);
    //esp_lcd_rgb_panel_refresh(panel_handle);
    return ESP_OK;

err:
    // do some deconstruction
    return ESP_FAIL;
}
