/* Based on the console example app, which is licensed under CC0.
 * @link https://github.com/espressif/esp-idf/blob/v4.4.3/examples/system/console/basic/main/console_example_main.c
 */

#include <string.h>
#include <esp_log.h>
#include <esp_console.h>
#include <esp_vfs_fat.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "epd.h"
#include "commands/screen.h"
#include "commands/graphics.h"
#include "commands/system.h"
#include "commands/tests.h"

#define DEFINE_FONTS
#include "fonts.h"

static const char* TAG = "screen_diag";

static void initialize_filesystem(void)
{
    static wl_handle_t wl_handle;
    const esp_vfs_fat_mount_config_t mount_config = {
            .max_files = 4,
            .format_if_mount_failed = true
    };

    esp_err_t err = esp_vfs_fat_spiflash_mount("/screen_diag", "storage", &mount_config, &wl_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
        return;
    }
}

static void initialize_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    initialize_nvs();
    initialize_filesystem();
    initialize_screen();

    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "diag>";
    repl_config.history_save_path = "/screen_diag/history.txt";

    /* Register commands */
    esp_console_register_help_command();
    register_system_commands();
    register_screen_commands();
    register_graphics_commands();
    register_tests_commands();

    esp_console_repl_t *repl;
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
