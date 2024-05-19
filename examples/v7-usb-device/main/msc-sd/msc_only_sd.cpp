/*
 * Original MSC code: 2022-2023 Espressif Systems (Shanghai) CO LTD
 * Adapted to open images and render them with epdiy displays: Fasani Corp.
 * Please check original Readme in their tinyusb examples for MSC:
 * https://github.com/espressif/esp-idf/tree/master/examples/peripherals/usb/device/tusb_msc
 * 
 * 
 * IMPORTANT: Using flash SPI to save / read files is slow and it's actually not working correctly in this
 *            example. I can see only the first 20Kb or so from the image rendered.
 *            An SD Card should be used to get the proper speed IMHO.
 */

/* DESCRIPTION:
 * This example contains code to make ESP32-S3 based device recognizable by USB-hosts as a USB Mass Storage Device.
 * It either allows the embedded application i.e. example to access the partition or Host PC accesses the partition over USB MSC.
 * They can't be allowed to access the partition at the same time.
 * For different scenarios and behaviour, Refer to README of this example.
 * All images should be placed in the root of SDCard and be valid, non-progressive, JPG images
 * This example uses a JPEG decoder from Larry Bank
 */


// Gallery mode will loop through all JPG images in the SDCard
#define GALLERY_MODE true
// Seconds wait till reading next picture
#define GALLERY_WAIT_SEC 5
// Clean mode will do a full clean refresh before loading a new image (slower)
#define GALLERY_CLEAN_MODE false

#define FRONT_LIGHT_ENABLE true
#define FL_PWM   GPIO_NUM_11

#define LV_TICK_PERIOD_MS 1
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO          (11) // Define the output GPIO
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
#define LEDC_DUTY               (2096) // 4096 Set duty to 50%. (2 ** 13) * 50% = 4096
#define LEDC_FREQUENCY          (4000) // Frequency in Hertz. Set frequency at 4 kHz


#include <errno.h>
#include <dirent.h>
#include "esp_timer.h"
#include "esp_console.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "tinyusb.h"
#include "tusb_msc_storage.h"
// GPIO & PWM control of front-light
#include "driver/gpio.h"
#include "driver/ledc.h"

#ifdef CONFIG_EXAMPLE_STORAGE_MEDIA_SDMMCCARD
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#endif
#include <math.h>  // round + pow

// epdiy
extern "C" {
    void app_main();
}
int temperature = 25;

int cursor_x = 10;
int cursor_y = 30;

// JPG decoder from @bitbank2
#include "JPEGDEC.h"
JPEGDEC jpeg;
#define DEBUG_JPG_PAYLOAD false

// EXPERIMENTAL: If JPEG_CPY_FRAMEBUFFER is true the JPG is decoded directly in EPD framebuffer
// On true it looses rotation. Experimental, does not work alright yet. Hint:
// Check if an uint16_t buffer can be copied in a uint8_t buffer directly
// NO COLOR SUPPORT on true!
#define JPEG_CPY_FRAMEBUFFER false
// Dither space allocation
uint8_t* dither_space;

uint32_t time_decomp = 0;
uint32_t time_render = 0;

static const char *TAG = "MSC example";

#define EPNUM_MSC       1
#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)

enum {
    ITF_NUM_MSC = 0,
    ITF_NUM_TOTAL
};

enum {
    EDPT_CTRL_OUT = 0x00,
    EDPT_CTRL_IN  = 0x80,

    EDPT_MSC_OUT  = 0x01,
    EDPT_MSC_IN   = 0x81,
};

static uint8_t const desc_configuration[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, EP Out & EP In address, EP size
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EDPT_MSC_OUT, EDPT_MSC_IN, TUD_OPT_HIGH_SPEED ? 512 : 64),
};

static tusb_desc_device_t descriptor_config = {
    .bLength = sizeof(descriptor_config),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A, // This is Espressif VID. This needs to be changed according to Users / Customers
    .idProduct = 0x4002,
    .bcdDevice = 0x100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01
};

static char const *string_desc_arr[] = {
    (const char[]) { 0x09, 0x04 },  // 0: is supported language is English (0x0409)
    "TinyUSB",                      // 1: Manufacturer
    "epdiy USB Device",             // 2: Product
    "123456",                       // 3: Serials
    "Example MSC",                  // 4. MSC
};
/*********************************************************************** TinyUSB descriptors*/

#define BASE_PATH "/data" // base path to mount the partition

#define PROMPT_STR CONFIG_IDF_TARGET
static int console_unmount(int argc, char **argv);
static int console_read(int argc, char **argv);
static int console_write(int argc, char **argv);
static int console_size(int argc, char **argv);
static int console_status(int argc, char **argv);
static int console_exit(int argc, char **argv);
const esp_console_cmd_t cmds[] = {
    {
        .command = "read",
        .help = "read BASE_PATH/README.MD and print its contents",
        .hint = NULL,
        .func = &console_read,
    },
    {
        .command = "write",
        .help = "create file BASE_PATH/README.MD if it does not exist",
        .hint = NULL,
        .func = &console_write,
    },
    {
        .command = "size",
        .help = "show storage size and sector size",
        .hint = NULL,
        .func = &console_size,
    },
    {
        .command = "expose",
        .help = "Expose Storage to Host",
        .hint = NULL,
        .func = &console_unmount,
    },
    {
        .command = "status",
        .help = "Status of storage exposure over USB",
        .hint = NULL,
        .func = &console_status,
    },
    {
        .command = "exit",
        .help = "exit from application",
        .hint = NULL,
        .func = &console_exit,
    }
};


void read_file(char * filename) {
    char filepath[150];
    sprintf(filepath, "%s/%s", BASE_PATH, filename);
    ESP_LOGI(TAG, "Opening file: %s", filepath);

    FILE* file = fopen(filepath, "r");
    
    fseek(file, 0, SEEK_END);
    int file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    }

// mount the partition and show all the files in BASE_PATH
static void _mount(void)
{
    ESP_LOGI(TAG, "Mount storage...");
    ESP_ERROR_CHECK(tinyusb_msc_storage_mount(BASE_PATH));

    // List all the files in this directory
    ESP_LOGI(TAG, "\nls command output:");
    struct dirent *d;
    DIR *dh = opendir(BASE_PATH);
    if (!dh) {
        if (errno == ENOENT) {
            //If the directory is not found
            ESP_LOGE(TAG, "Directory doesn't exist %s", BASE_PATH);
        } else {
            //If the directory is not readable then throw error and exit
            ESP_LOGE(TAG, "Unable to read directory %s", BASE_PATH);
        }
        return;
    }

    char strbuf[257];
    //epd_write_string(&FiraSans_20, "Directory listing:", &cursor_x, &cursor_y, fb, &font_props);
    cursor_x = 10;

    int file_cnt = 0;
    char selected_file[256];
    //While the next entry is not readable we will print directory files
    while ((d = readdir(dh)) != NULL) {
        if (strncmp(d->d_name, ".", 1) == 0) {
            printf("%s not image, discarded\n", d->d_name);
            continue;
        }
        file_cnt++;
        if (file_cnt == 1) {
            strcpy(selected_file, d->d_name);
            printf("Opening: %s\n", selected_file);
        }
        
        sprintf(strbuf, "%s", d->d_name);
        cursor_x = 10;
    }
    
}

void gallery_mode() {
    struct dirent *d;
    //While the next entry is not readable we will read each image
    while(true) {
        DIR *dh = opendir(BASE_PATH);
        if (!dh) {
            if (errno == ENOENT) {
                ESP_LOGE(TAG, "Directory doesn't exist %s", BASE_PATH);
            } else {
                //If the directory is not readable then throw error and exit
                ESP_LOGE(TAG, "Unable to read directory %s", BASE_PATH);
            }
            return;
        }
        // Loop through each image
        while ((d = readdir(dh)) != NULL) {
            if (strncmp(d->d_name, ".", 1) == 0) {
                printf("%s not image, discarded\n", d->d_name);
                continue;
            }
            read_file(d->d_name);

            for (int duty=100; duty<8000; duty+=250) {
                ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
                ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
                vTaskDelay(pdMS_TO_TICKS(100));
            }

            vTaskDelay(pdMS_TO_TICKS(GALLERY_WAIT_SEC *1000));
            ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
            ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);

        }
    }
}

// unmount storage
static int console_unmount(int argc, char **argv)
{
    if (tinyusb_msc_storage_in_use_by_usb_host()) {
        ESP_LOGE(TAG, "storage is already exposed");
        return -1;
    }
    ESP_LOGI(TAG, "Unmount storage...");
    ESP_ERROR_CHECK(tinyusb_msc_storage_unmount());
    return 0;
}

// read BASE_PATH/README.MD and print its contents
static int console_read(int argc, char **argv)
{
    if (tinyusb_msc_storage_in_use_by_usb_host()) {
        ESP_LOGE(TAG, "storage exposed over USB. Application can't read from storage.");
        return -1;
    }
    ESP_LOGD(TAG, "read from storage:");
    const char *filename = BASE_PATH "/README.MD";
    FILE *ptr = fopen(filename, "r");
    if (ptr == NULL) {
        ESP_LOGE(TAG, "Filename not present - %s", filename);
        return -1;
    }
    char buf[1024];
    while (fgets(buf, 1000, ptr) != NULL) {
        printf("%s", buf);
    }
    fclose(ptr);
    return 0;
}

// create file BASE_PATH/README.MD if it does not exist
static int console_write(int argc, char **argv)
{
    if (tinyusb_msc_storage_in_use_by_usb_host()) {
        ESP_LOGE(TAG, "storage exposed over USB. Application can't write to storage.");
        return -1;
    }
    ESP_LOGD(TAG, "write to storage:");
    const char *filename = BASE_PATH "/README.MD";
    FILE *fd = fopen(filename, "r");
    if (!fd) {
        ESP_LOGW(TAG, "README.MD doesn't exist yet, creating");
        fd = fopen(filename, "w");
        fprintf(fd, "Mass Storage Devices are one of the most common USB devices. It use Mass Storage Class (MSC) that allow access to their internal data storage.\n");
        fprintf(fd, "In this example, ESP chip will be recognised by host (PC) as Mass Storage Device.\n");
        fprintf(fd, "Upon connection to USB host (PC), the example application will initialize the storage module and then the storage will be seen as removable device on PC.\n");
        fclose(fd);
    }
    return 0;
}

// Show storage size and sector size
static int console_size(int argc, char **argv)
{
    if (tinyusb_msc_storage_in_use_by_usb_host()) {
        ESP_LOGE(TAG, "storage exposed over USB. Application can't access storage");
        return -1;
    }
    uint32_t sec_count = tinyusb_msc_storage_get_sector_count();
    uint32_t sec_size = tinyusb_msc_storage_get_sector_size();
    printf("Storage Capacity %lluMB\n", ((uint64_t) sec_count) * sec_size / (1024 * 1024));
    return 0;
}

// exit from application
static int console_status(int argc, char **argv)
{
    printf("storage exposed over USB: %s\n", tinyusb_msc_storage_in_use_by_usb_host() ? "Yes" : "No");
    return 0;
}

// exit from application
static int console_exit(int argc, char **argv)
{
    tinyusb_msc_storage_deinit();
    printf("Application Exiting\n");
    exit(0);
    return 0;
}

#ifdef CONFIG_EXAMPLE_STORAGE_MEDIA_SPIFLASH
static esp_err_t storage_init_spiflash(wl_handle_t *wl_handle)
{
    ESP_LOGI(TAG, "Initializing wear levelling");

    const esp_partition_t *data_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, NULL);
    if (data_partition == NULL) {
        ESP_LOGE(TAG, "Failed to find FATFS partition. Check the partition table.");
        return ESP_ERR_NOT_FOUND;
    }

    return wl_mount(data_partition, wl_handle);
}
#else  // CONFIG_EXAMPLE_STORAGE_MEDIA_SPIFLASH
static esp_err_t storage_init_sdmmc(sdmmc_card_t **card)
{
    esp_err_t ret = ESP_OK;
    bool host_init = false;
    sdmmc_card_t *sd_card;

    ESP_LOGI(TAG, "Initializing SDCard");

    // By default, SD card frequency is initialized to SDMMC_FREQ_DEFAULT (20MHz)
    // For setting a specific frequency, use host.max_freq_khz (range 400kHz - 40MHz for SDMMC)
    // Example: for fixed frequency of 10MHz, use host.max_freq_khz = 10000;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    // For SD Card, set bus width to use
#ifdef CONFIG_EXAMPLE_SDMMC_BUS_WIDTH_4
    slot_config.width = 4;
#else
    slot_config.width = 1;
#endif  // CONFIG_EXAMPLE_SDMMC_BUS_WIDTH_4

    // On chips where the GPIOs used for SD card can be configured, set the user defined values
#ifdef CONFIG_SOC_SDMMC_USE_GPIO_MATRIX
    slot_config.clk = (gpio_num_t)CONFIG_EXAMPLE_PIN_CLK;
    slot_config.cmd = (gpio_num_t)CONFIG_EXAMPLE_PIN_CMD;
    slot_config.d0 = (gpio_num_t)CONFIG_EXAMPLE_PIN_D0;
#ifdef CONFIG_EXAMPLE_SDMMC_BUS_WIDTH_4
    slot_config.d1 = (gpio_num_t)CONFIG_EXAMPLE_PIN_D1;
    slot_config.d2 = (gpio_num_t)CONFIG_EXAMPLE_PIN_D2;
    slot_config.d3 = (gpio_num_t)CONFIG_EXAMPLE_PIN_D3;
#endif  // CONFIG_EXAMPLE_SDMMC_BUS_WIDTH_4

#endif  // CONFIG_SOC_SDMMC_USE_GPIO_MATRIX

    // Enable internal pullups on enabled pins. The internal pullups
    // are insufficient however, please make sure 10k external pullups are
    // connected on the bus. This is for debug / example purpose only.
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    // not using ff_memalloc here, as allocation in internal RAM is preferred
    sd_card = (sdmmc_card_t *)malloc(sizeof(sdmmc_card_t));
    ESP_GOTO_ON_FALSE(sd_card, ESP_ERR_NO_MEM, clean, TAG, "could not allocate new sdmmc_card_t");

    ESP_GOTO_ON_ERROR((*host.init)(), clean, TAG, "Host Config Init fail");
    host_init = true;

    ESP_GOTO_ON_ERROR(sdmmc_host_init_slot(host.slot, (const sdmmc_slot_config_t *) &slot_config),
                      clean, TAG, "Host init slot fail");

    while (sdmmc_card_init(&host, sd_card)) {
        ESP_LOGE(TAG, "The detection pin of the slot is disconnected(Insert uSD card). Retrying...");
        vTaskDelay(pdMS_TO_TICKS(3000));
    }

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, sd_card);
    *card = sd_card;

    return ESP_OK;

clean:
    if (host_init) {
        if (host.flags & SDMMC_HOST_FLAG_DEINIT_ARG) {
            host.deinit_p(host.slot);
        } else {
            (*host.deinit)();
        }
    }
    if (sd_card) {
        free(sd_card);
        sd_card = NULL;
    }
    return ret;
}
#endif  // CONFIG_EXAMPLE_STORAGE_MEDIA_SPIFLASH

void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "Initializing storage...");

#ifdef CONFIG_EXAMPLE_STORAGE_MEDIA_SPIFLASH
    static wl_handle_t wl_handle = WL_INVALID_HANDLE;
    ESP_ERROR_CHECK(storage_init_spiflash(&wl_handle));

    const tinyusb_msc_spiflash_config_t config_spi = {
        .wl_handle = wl_handle
    };
    ESP_ERROR_CHECK(tinyusb_msc_storage_init_spiflash(&config_spi));
#else // SD
    static sdmmc_card_t *card = NULL;
    ESP_ERROR_CHECK(storage_init_sdmmc(&card));

    const tinyusb_msc_sdmmc_config_t config_sdmmc = {
        .card = card
    };
    ESP_ERROR_CHECK(tinyusb_msc_storage_init_sdmmc(&config_sdmmc));
#endif 

    _mount();
    /* if (GALLERY_MODE) {
        gallery_mode();
    } */

    ESP_LOGI(TAG, "USB MSC initialization");
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = &descriptor_config,
        .string_descriptor = string_desc_arr,
        .string_descriptor_count = sizeof(string_desc_arr) / sizeof(string_desc_arr[0]),
        .external_phy = false,
        .configuration_descriptor = desc_configuration,
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "USB MSC initialization DONE");

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    /* Prompt to be printed before each line.
     * This can be customized, made dynamic, etc.
     */
    repl_config.prompt = PROMPT_STR ">";
    repl_config.max_cmdline_length = 64;
    esp_console_register_help_command();
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));
    for (int count = 0; count < sizeof(cmds) / sizeof(esp_console_cmd_t); count++) {
        ESP_ERROR_CHECK( esp_console_cmd_register(&cmds[count]) );
    }
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
