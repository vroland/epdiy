#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "power_timing.h"

typedef int esp_err_t;
typedef int i2c_master_dev_handle_t;
typedef unsigned TickType_t;
#define ESP_OK 0
#define ESP_ERR_TIMEOUT 1
#define ESP_ERR_INVALID_SIZE 2
#define ESP_ERR_NOT_SUPPORTED 3
#define BUS_ERROR 4
#define ESP_LOGE(...) ((void)0)
#define ESP_ERROR_CHECK(x) assert((x) == ESP_OK)
#define pdMS_TO_TICKS(ms) ((ms) * TICK_HZ / 1000)
#define CFG_PIN_OE 1
#define CFG_PIN_MODE 2
#define CFG_PIN_PWRUP 8
#define CFG_PIN_VCOM_CTRL 16
#define CFG_PIN_WAKEUP 32
#define CFG_PIN_PWRGOOD 64
#define DISPLAY_UPSEQ_MC2 1
#define TPS_REG_ENABLE 1
#define TPS_REG_VCOM1 3
#define TPS_REG_VCOM2 4
#define TPS_REG_UPSEQ0 9
#define TPS_REG_UPSEQ1 10
#define TPS_REG_PG 15

typedef struct {
    bool ep_latch_enable, ep_output_enable, ep_sth, ep_mode, ep_stv;
} epd_ctrl_state_t;
typedef struct { unsigned display_type; } EpdDisplay_t;
static EpdDisplay_t display;
static struct {
    struct { i2c_master_dev_handle_t pca, tps; } i2c;
    bool pwrup, vcom_ctrl, wakeup;
} config_reg;
static int vcom = 1600;
static int64_t now_us, pca_ready, tps_ready;
static int transfers, fail_at, bus_time_us, off_attempts;
static bool bus_stuck;
static uint8_t last_port, registers[16];
static const EpdDisplay_t* epd_get_display(void) { return &display; }
static int64_t esp_timer_get_time(void) { return now_us; }
static void vTaskDelay(TickType_t ticks) { assert(ticks > 0); now_us += ticks * (1000000 / TICK_HZ); }
static esp_err_t transfer(int timeout_ms) {
    assert(timeout_ms > 0 && timeout_ms <= EPD_POWER_I2C_TIMEOUT_MS);
    ++transfers;
    if (bus_stuck) { now_us += timeout_ms * 1000; return ESP_ERR_TIMEOUT; }
    assert(bus_time_us <= timeout_ms * 1000);
    now_us += bus_time_us;
    return transfers == fail_at ? BUS_ERROR : ESP_OK;
}
static esp_err_t i2c_master_transmit_receive(int dev, const uint8_t* reg, size_t n,
                                           uint8_t* out, size_t len, int timeout) {
    assert(n == 1 && len == 1);
    esp_err_t err = transfer(timeout);
    if (err != ESP_OK) return err;
    if (dev == 1) { assert(*reg == 1); *out = now_us >= pca_ready ? CFG_PIN_PWRGOOD : 0; }
    else { assert(dev == 2 && *reg == TPS_REG_PG); *out = now_us >= tps_ready ? 0xFA : 0; }
    return ESP_OK;
}
static esp_err_t i2c_master_transmit(int dev, const uint8_t* data, size_t len, int timeout) {
    assert(len == 2);
    if (dev == 1 && data[1] == 0) ++off_attempts;
    esp_err_t err = transfer(timeout);
    if (err != ESP_OK) return err;
    if (dev == 1) { assert(data[0] == 3); last_port = data[1]; }
    else { assert(dev == 2 && data[0] < 16); registers[data[0]] = data[1]; }
    return ESP_OK;
}
// INJECT_HELPERS
// INJECT_BOARD

typedef struct {
    void (*poweron)(epd_ctrl_state_t*);
    void (*poweroff)(epd_ctrl_state_t*);
    esp_err_t (*poweron_checked)(epd_ctrl_state_t*);
    esp_err_t (*poweroff_checked)(epd_ctrl_state_t*);
} EpdBoardDefinition;
static EpdBoardDefinition board;
static epd_ctrl_state_t state;
static const EpdBoardDefinition* epd_current_board(void) { return &board; }
static epd_ctrl_state_t* epd_ctrl_state(void) { return &state; }
// INJECT_API

static void reset(void) {
    now_us = pca_ready = tps_ready = 0;
    transfers = fail_at = bus_time_us = off_attempts = 0;
    bus_stuck = false; last_port = 0;
    memset(registers, 0, sizeof(registers));
    memset(&state, 0, sizeof(state));
    memset(&config_reg, 0, sizeof(config_reg));
    config_reg.i2c.pca = 1; config_reg.i2c.tps = 2;
    display.display_type = 0;
    board = (EpdBoardDefinition){epd_board_poweron, epd_board_poweroff,
                               epd_board_poweron_checked, epd_board_poweroff_checked};
}
static void assert_off(void) {
    assert(!config_reg.pwrup && !config_reg.vcom_ctrl && !config_reg.wakeup);
    assert(!state.ep_output_enable && !state.ep_sth && last_port == 0);
    assert(off_attempts > 0);
}
int main(void) {
    reset(); assert(epd_poweron_checked() == ESP_OK);
    assert(last_port == (CFG_PIN_OE | CFG_PIN_PWRUP | CFG_PIN_VCOM_CTRL | CFG_PIN_WAKEUP));
    assert(registers[TPS_REG_ENABLE] == 0x3F && registers[TPS_REG_VCOM1] == 160);
    assert(epd_poweroff_checked() == ESP_OK); assert_off();

    reset(); pca_ready = 100000; tps_ready = 200000;
    assert(epd_poweron_checked() == ESP_OK && now_us >= 200000 && now_us < 230000);
    assert(epd_poweroff_checked() == ESP_OK); assert_off();

    // Each phase uses elapsed time: roughly 500 ms at either tick rate.
    for (int phase = 0; phase < 2; ++phase) {
        reset(); if (phase == 0) pca_ready = INT64_MAX; else tps_ready = INT64_MAX;
        assert(epd_poweron_checked() == ESP_ERR_TIMEOUT);
        assert(now_us >= 500000 && now_us <= 550000); assert_off();
    }
    // The bus itself can time out, even on the first control write. Cleanup
    // attempts both writes and returns promptly without claiming physical off.
    reset(); bus_stuck = true;
    assert(epd_poweron_checked() == ESP_ERR_TIMEOUT);
    assert(transfers == 3 && off_attempts == 1 && now_us <= 160000);

    // A success arriving after the deadline is rejected.
    reset(); bus_time_us = 50000; pca_ready = 690000;
    assert(epd_poweron_checked() == ESP_ERR_TIMEOUT); assert_off();

    // Fail every individual bus operation for normal, Carta and VCOM setup.
    for (int mode = 0; mode < 3; ++mode) {
        reset(); display.display_type = mode == 1 ? DISPLAY_UPSEQ_MC2 : 0;
        assert(epd_board_enable_power(&state, mode == 2) == ESP_OK);
        int count = transfers;
        for (int operation = 1; operation <= count; ++operation) {
            reset(); display.display_type = mode == 1 ? DISPLAY_UPSEQ_MC2 : 0;
            fail_at = operation;
            assert(epd_board_enable_power(&state, mode == 2) == BUS_ERROR);
            assert_off();
        }
    }
    // Even an unsuccessful first shutdown write must not skip the all-off one.
    reset(); assert(epd_poweron_checked() == ESP_OK); fail_at = transfers + 1;
    assert(epd_poweroff_checked() == BUS_ERROR); assert_off();
    reset(); assert(epd_poweron_checked() == ESP_OK); fail_at = transfers + 2;
    assert(epd_poweroff_checked() == BUS_ERROR && off_attempts == 1);
    assert(epd_poweroff_checked() == ESP_OK); assert_off();

    reset(); board.poweron_checked = NULL; board.poweroff_checked = NULL;
    assert(epd_poweron_checked() == ESP_ERR_NOT_SUPPORTED);
    assert(epd_poweroff_checked() == ESP_ERR_NOT_SUPPORTED && transfers == 0);
    epd_poweron(); epd_poweroff(); assert_off();
    printf("PASS: V7 power deadlines, bus faults, cleanup and API at %d Hz\n", TICK_HZ);
}
