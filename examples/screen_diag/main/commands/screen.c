#include "screen.h"

#include <stdio.h>
#include <string.h>

#include <esp_console.h>
#include <argtable3/argtable3.h>

#include "../epd.h"
#include "../commands.h"

static struct {
    struct arg_str* rotation;
    struct arg_lit* inverted;
    struct arg_end* end;
} set_rotation_args;

static struct {
    struct arg_int *posx, *posy;
    struct arg_end* end;
} get_pixel_args;

static struct {
    struct arg_int *posx, *posy, *color;
    struct arg_end* end;
} set_pixel_args;

static int get_rotation(int argc, char* argv[]);
static int set_rotation(int argc, char* argv[]);
static int get_width(int argc, char* argv[]);
static int get_height(int argc, char* argv[]);
static int get_pixel(int argc, char* argv[]);
static int set_pixel(int argc, char* argv[]);
static int update_screen(int argc, char* argv[]);
static int clear_screen(int argc, char* argv[]);

void register_screen_commands(void)
{
    // setup arguments
    set_rotation_args.rotation = arg_strn(NULL, NULL, "<rotation>", 1, 1, "screen rotation: \"horizontal\" or \"portrait\"");
    set_rotation_args.inverted = arg_litn(NULL, "inverted", 0, 1, "");
    set_rotation_args.end = arg_end(NARGS(set_rotation_args));

    get_pixel_args.posx = arg_intn(NULL, NULL, "<posx>", 1, 1, "x position");
    get_pixel_args.posy = arg_intn(NULL, NULL, "<posy>", 1, 1, "y position");
    get_pixel_args.end = arg_end(NARGS(get_pixel_args));

    set_pixel_args.posx = arg_intn(NULL, NULL, "<posx>", 1, 1, "x position");
    set_pixel_args.posy = arg_intn(NULL, NULL, "<posy>", 1, 1, "y position");
    set_pixel_args.color = arg_intn(NULL, NULL, "<color>", 0, 1, "color. default value: 0 (0x00)");
    set_pixel_args.end = arg_end(NARGS(set_pixel_args));

    const esp_console_cmd_t commands[] = {
        {
            .command = "get_rotation",
            .help = "Get current screen rotation.",
            .hint = NULL,
            .func = &get_rotation
        },
        {
            .command = "set_rotation",
            .help = "Changes screen rotation.",
            .hint = NULL,
            .func = &set_rotation,
            .argtable = &set_rotation_args
        },
        {
            .command = "get_width",
            .help = "Print screen width.",
            .hint = NULL,
            .func = &get_width
        },
        {
            .command = "get_height",
            .help = "Print screen height.",
            .hint = NULL,
            .func = &get_height
        },
        {
            .command = "get_pixel",
            .help = "Get pixel color in front buffer.",
            .hint = NULL,
            .func = &get_pixel,
            .argtable = &get_pixel_args
        },
        {
            .command = "set_pixel",
            .help = "Set pixel color in front buffer.",
            .hint = NULL,
            .func = &set_pixel,
            .argtable = &set_pixel_args
        },
        {
            .command = "update_screen",
            .help = "Flush the front buffer onto the screen.",
            .hint = NULL,
            .func = &update_screen
        },
        {
            .command = "clear_screen",
            .help = "Clear the entire screen and reset the front buffer to white.",
            .hint = NULL,
            .func = &clear_screen
        }
    };

    for (size_t i = 0; i < ARRAY_SIZE(commands); ++i)
        ESP_ERROR_CHECK(esp_console_cmd_register(&commands[i]));
}

static int get_rotation(int argc, char* argv[])
{
    enum EpdRotation rot = epd_get_rotation();
    if (rot == EPD_ROT_INVERTED_LANDSCAPE || rot == EPD_ROT_INVERTED_PORTRAIT)
        printf("inverted ");

    if (rot == EPD_ROT_LANDSCAPE)
        printf("landscape\r\n");
    else if (rot == EPD_ROT_PORTRAIT)
        printf("portrait\r\n");

    return 0;
}

static int set_rotation(int argc, char* argv[])
{
    HANDLE_ARGUMENTS(set_rotation_args)

    const char* rot_str = set_rotation_args.rotation->sval[0];
    const bool invert = set_rotation_args.inverted->count == 1;

    enum EpdRotation rot = EPD_ROT_LANDSCAPE;
    if (!strcmp(rot_str, "landscape"))
    {
        if (invert)
            rot = EPD_ROT_INVERTED_LANDSCAPE;
        else
            rot = EPD_ROT_LANDSCAPE;
    }
    else if (!strcmp(rot_str, "portrait"))
    {
        if (invert)
            rot = EPD_ROT_INVERTED_PORTRAIT;
        else
            rot = EPD_ROT_PORTRAIT;
    }

    epd_set_rotation(rot);

    return 0;
}

static int get_width(int argc, char* argv[])
{
    printf("%d\r\n", epd_rotated_display_width());
    return 0;
}
static int get_height(int argc, char* argv[])
{
    printf("%d\r\n", epd_rotated_display_height());
    return 0;
}

static inline void swap(int* lhs, int* rhs)
{
    const int tmp = *lhs;
    *lhs = *rhs;
    *rhs = tmp;
}

struct coords { int x, y; };

/* Basically the _rotate() function from epd_driver.c */
static struct coords map_to_screen(int x, int y) {
    switch (epd_get_rotation()) {
        case EPD_ROT_LANDSCAPE:
            break;
        case EPD_ROT_PORTRAIT:
            swap(&x, &y);
            x = EPD_WIDTH - x - 1;
            break;
        case EPD_ROT_INVERTED_LANDSCAPE:
            x = EPD_WIDTH - x - 1;
            y = EPD_HEIGHT - y - 1;
            break;
        case EPD_ROT_INVERTED_PORTRAIT:
            swap(&x, &y);
            y = EPD_HEIGHT - y - 1;
            break;
    }

    return (struct coords){ x, y };
}

/* Read the pixel data from the front buffer, because there is no function provided by the driver.
 * Most importantly, we need to adjust the rotation of the incoming coordinates.
 */
static int get_pixel_color(int x, int y)
{
    const struct coords adjusted = map_to_screen(x, y);

    if (adjusted.x < 0 || adjusted.x >= EPD_WIDTH || adjusted.y < 0 || adjusted.y >= EPD_HEIGHT)
    {
        printf("Invalid coordinates (%d,%d): Must be withing the screen size (%d,%d).\r\n", adjusted.x, adjusted.y, EPD_WIDTH, EPD_HEIGHT);
        return -1;
    }

    uint8_t pixel = g_framebuffer[adjusted.y * EPD_WIDTH / 2 + adjusted.x / 2];
    uint8_t color = (adjusted.x % 2) ? (pixel & 0xF0) : (pixel & 0x0F);

    // repeat color pattern
    color |= (adjusted.x % 2) ? (color >> 4) : (color << 4);

    return color;
}

static int get_pixel(int argc, char* argv[])
{
    HANDLE_ARGUMENTS(get_pixel_args)

    const int pos_x = get_pixel_args.posx->ival[0];
    const int pos_y = get_pixel_args.posy->ival[0];

    const int color = get_pixel_color(pos_x, pos_y);
    if (color == -1)
    {
        printf("Invalid coordinates (%d,%d): Must be withing the screen size (%d,%d).\r\n", pos_x, pos_y,
            epd_rotated_display_width(), epd_rotated_display_height());
        return 1;
    }

    printf("Pixel (%d,%d) has color %d (0x%02x)\r\n", pos_x, pos_y, color, (uint8_t) color);

    return 0;
}

static int set_pixel(int argc, char* argv[])
{
    HANDLE_ARGUMENTS(set_pixel_args)

    const int pos_x = set_pixel_args.posx->ival[0];
    const int pos_y = set_pixel_args.posy->ival[0];

    uint8_t color = 0x00;
    if (!validate_color(&color, set_pixel_args.color))
        return 1;

    epd_draw_pixel(pos_x, pos_y, color, g_framebuffer);
    printf("Set pixel (%d,%d) to color %d (0x%02x)\r\n", pos_x, pos_y, color, color);

    return 0;
}

static int update_screen(int argc, char* argv[])
{
    update_full_screen();
    printf("Updated screen.\r\n");
    return 0;
}

static int clear_screen(int argc, char* argv[])
{
    clear_full_screen();
    printf("Cleared screen.\r\n");
    return 0;
}
