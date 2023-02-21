#include "system.h"

#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_chip_info.h>
#include <esp_app_desc.h>
#include <esp_mac.h>
#include <esp_console.h>
#include <argtable3/argtable3.h>

#include "../commands.h"

static struct {
    struct arg_int* caps;
    struct arg_end* end;
} dump_heaps_args;

static struct {
    struct arg_str* interface;
    struct arg_end* end;
} get_mac_args;

static int system_restart(int argc, char* argv[]);
static int system_get_free_heap_size(int argc, char* argv[]);
static int system_dump_heaps_info(int argc, char* argv[]);
static int system_dump_tasks(int argc, char* argv[]);
static int system_dump_chip_info(int argc, char* argv[]);
static int system_dump_firmware_info(int argc, char* argv[]);
static int system_get_time(int argc, char* argv[]);
static int system_get_mac(int argc, char* argv[]);

void register_system_commands(void)
{
    dump_heaps_args.caps = arg_intn(NULL, NULL, "<caps>", 0, 1, "Heap caps to print. Default: MALLOC_CAP_DEFAULT");
    dump_heaps_args.end = arg_end(NARGS(dump_heaps_args));

    get_mac_args.interface = arg_strn(NULL, NULL, "<interface>", 0, 1, "Either \"wifi_station\", \"wifi_ap\", \"bluetooth\" or \"ethernet\"");
    get_mac_args.end = arg_end(NARGS(get_mac_args));

    // register commands
    const esp_console_cmd_t commands[] = {
        {
            .command = "system_restart",
            .help = "Restarts the system.",
            .hint = NULL,
            .func = &system_restart
        },
        {
            .command = "free_heap_size",
            .help = "Returns the free heap size.",
            .hint = NULL,
            .func = &system_get_free_heap_size
        },
        {
            .command = "dump_heaps_info",
            .help = "Dumps heap information of all heaps matching the capability.",
            .hint = NULL,
            .func = &system_dump_heaps_info,
            .argtable = &dump_heaps_args
        },
        {
            .command = "dump_tasks",
            .help = "Dumps all tasks with their names, current state and stack usage.",
            .hint = NULL,
            .func = &system_dump_tasks
        },
        {
            .command = "chip_info",
            .help = "Dumps chip information.",
            .hint = NULL,
            .func = &system_dump_chip_info
        },
        {
            .command = "firmware_info",
            .help = "Dumps information about the ESP-IDF and the firmware.",
            .hint = NULL,
            .func = &system_dump_firmware_info
        },
        {
            .command = "get_time",
            .help = "Returns the time in microseconds since boot.",
            .hint = NULL,
            .func = &system_get_time
        },
        {
            .command = "get_mac",
            .help = "Returns the MAC address for the given interface or the pre-programmed base address.",
            .hint = NULL,
            .func = &system_get_mac,
            .argtable = &get_mac_args
        }
    };

    for (size_t i = 0; i < ARRAY_SIZE(commands); ++i)
        ESP_ERROR_CHECK(esp_console_cmd_register(&commands[i]));
}

static int system_restart(int argc, char* argv[])
{
    esp_restart();
    // unreachable
}

static int system_get_free_heap_size(int argc, char* argv[])
{
    printf("Free heap size: %lu bytes.\r\n", esp_get_free_heap_size());
    return 0;
}

static int system_dump_heaps_info(int argc, char* argv[])
{
    HANDLE_ARGUMENTS(dump_heaps_args);

    uint32_t caps = dump_heaps_args.caps->count == 1
        ? dump_heaps_args.caps->ival[0]
        : MALLOC_CAP_DEFAULT;

    heap_caps_print_heap_info(caps);

    return 0;
}

static int system_dump_tasks(int argc, char* argv[])
{
    const unsigned num_tasks = uxTaskGetNumberOfTasks();
    char* buffer = calloc(num_tasks, 40); // approx. 40 bytes per task.

    puts("Name          State   Priority  Stack   Num\r\n"
         "-------------------------------------------");

    vTaskList(buffer);
    puts(buffer);

    puts("Legend:\r\n"
         "\tB: Blocked\r\n"
         "\tR: Ready\r\n"
         "\tD: Deleted (waiting clean up)\r\n"
         "\tS: Suspended, or Blocked without a timeout");

    free(buffer);
    return 0;
}

static const char* chip_model_str(esp_chip_model_t model)
{
    switch (model)
    {
        case CHIP_ESP32:
            return "ESP32";
        case CHIP_ESP32S2:
            return "ESP32S2";
        case CHIP_ESP32S3:
            return "ESP32S3";
        case CHIP_ESP32C3:
            return "ESP32C3";
        case CHIP_ESP32H2:
            return "ESP32H2";
        case CHIP_ESP32C2:
            return "ESP32C2";
        default:
            return "Unknown";
    }
}

static int system_dump_chip_info(int argc, char* argv[])
{
    esp_chip_info_t info;
    esp_chip_info(&info);

    printf("model: %s\r\n"
           "features (0x%lX):\r\n"
           "\tEMB_FLASH: %d\r\n"
           "\tWIFI_BGN: %d\r\n"
           "\tBLE:\t%d\r\n"
           "\tBT:\t%d\r\n"
           "\tIEEE802154: %d\r\n"
           "\tEMB_PSRAM: %d\r\n"
           "revision: %d.%d\r\n"
           "cores: %d\r\n",
        chip_model_str(info.model), info.features,
        (info.features & CHIP_FEATURE_EMB_PSRAM) != 0,
        (info.features & CHIP_FEATURE_WIFI_BGN) != 0,
        (info.features & CHIP_FEATURE_BLE) != 0,
        (info.features & CHIP_FEATURE_BT) != 0,
        (info.features & CHIP_FEATURE_IEEE802154) != 0,
        (info.features & CHIP_FEATURE_EMB_PSRAM) != 0,
        (info.revision / 100), (info.revision % 100),
        info.cores);

    return 0;
}

static int system_dump_firmware_info(int argc, char* argv[])
{
    char hash[64];
    esp_app_get_elf_sha256(hash, 64);

    const esp_app_desc_t* app = esp_app_get_description();

    printf("ESP-IDF version: %s\r\n"
           "Firmware info:\r\n"
           "\tmagic: 0x%lX\r\n"
           "\tsecure_version: 0x%lX\r\n"
           "\tversion: %s\r\n"
           "\tproject_name: %s\r\n"
           "\tcompile time/date: %s %s\r\n"
           "\telf sha256: %s\r\n",
           esp_get_idf_version(),
           app->magic_word,
           app->secure_version, app->version,
           app->project_name, app->time, app->date,
           hash);

    return 0;
}

static int system_get_time(int argc, char* argv[])
{
    printf("Time in microseconds since boot: %llu\r\n", esp_timer_get_time());
    return 0;
}

static void print_mac(uint8_t mac[8])
{
    // only print MAC-48 types
    printf("%02X:%02X:%02X:%02X:%02X:%02X\r\n",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static int system_get_mac(int argc, char* argv[])
{
    HANDLE_ARGUMENTS(get_mac_args);

    uint8_t mac[8];

    if (get_mac_args.interface->count == 0)
    {
        esp_base_mac_addr_get(mac);
        print_mac(mac);

        return 0;
    }

    esp_mac_type_t type;
    const char* arg = get_mac_args.interface->sval[0];
    if (!strcmp(arg, "wifi_station"))
        type = ESP_MAC_WIFI_STA;
    else if (!strcmp(arg, "wifi_ap"))
        type = ESP_MAC_WIFI_SOFTAP;
    else if (!strcmp(arg, "bluetooth"))
        type = ESP_MAC_BT;
    else if (!strcmp(arg, "ethernet"))
        type = ESP_MAC_ETH;
    else
    {
        printf("Invalid interface: \"%s\". Must be one of \"wifi_station\", \"wifi_ap\", \"bluetooth\" or \"ethernet\".\r\n", arg);
        return 1;
    }

    esp_read_mac(mac, type);
    print_mac(mac);

    return 0;
}
