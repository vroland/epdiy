#include "tests.h"

#include <stdio.h>

#include <esp_console.h>
#include <argtable3/argtable3.h>

#include "../epd.h"
#include "fonts.h"

static struct {
    struct arg_int *slope;
    struct arg_int *width;
    struct arg_int *color;
    struct arg_end *end;
} render_stairs_args;

static struct {
    struct arg_int *gutter;
    struct arg_int *color;
    struct arg_end *end;
} render_grid_args;

static int render_stairs_cmd(int argc, char* argv[]);
static int render_grid_cmd(int argc, char* argv[]);

void register_tests_commands(void)
{
    // setup args
    render_stairs_args.slope = arg_intn(NULL, NULL, "<slope>", 0, 1, "angle by which each diagonal line is drawn. default value: 3");
    render_stairs_args.width = arg_intn(NULL, NULL, "<width>", 0, 1, "thickness of each diagonal line. default value: 100");
    render_stairs_args.color = arg_intn(NULL, NULL, "<color>", 0, 1, "default value: 0x00");
    render_stairs_args.end = arg_end(3);

    render_grid_args.gutter = arg_intn(NULL, NULL, "<gutter>", 0, 1, "default value: 75"); // gcd(1200, 825) = 75
    render_grid_args.color = arg_intn(NULL, NULL, "<color>", 0, 1, "default value: 0x00");
    render_grid_args.end = arg_end(2);

    // register commands
    const esp_console_cmd_t commands[] = {
        {
            .command = "render_stairs",
            .help = "Render multiple diagonal lines across the screen.",
            .hint = NULL,
            .func = &render_stairs_cmd,
            .argtable = &render_stairs_args
        },
        {
            .command = "render_grid",
            .help = "Renders a grid across the whole screen. At a certain gutter size, cell info will be printed as well.",
            .hint = NULL,
            .func = &render_grid_cmd,
            .argtable = &render_grid_args
        },
    };

    for (size_t i = 0; i < (sizeof(commands) / sizeof(commands[0])); ++i)
        ESP_ERROR_CHECK(esp_console_cmd_register(&commands[i]));
}

static int validate_color(struct arg_int* arg)
{
    int color = arg->count != 0 ? arg->ival[0] : 0x00;
    if (color < 0 || color > 0xFF)
    {
        printf("Invalid color %d (0x%02x): Must be in range 0x00 to 0xFF.\r\n", color, color);
        return -1;
    }

    return color;
}

static void render_stairs(int slope, int width, uint8_t color)
{
    for(int y = 0, x = 0; y < epd_rotated_display_height(); y++) {
        epd_draw_hline(x, y, width, color, g_framebuffer);
        x += slope;
        if(x + width > epd_rotated_display_width())
            x = 0;
    }
}

static int render_stairs_cmd(int argc, char* argv[])
{
    int nerrors = arg_parse(argc, argv, (void**) &render_stairs_args);
    if (nerrors > 0)
    {
        arg_print_errors(stdout, render_stairs_args.end, "render_stairs");
        return 1;
    }

    const int color = validate_color(render_stairs_args.color);
    if (color == -1) return 1;

    const int slope = render_stairs_args.slope->count == 1
        ? render_stairs_args.slope->ival[0]
        : 3;
    const int width = render_stairs_args.width->count == 1
        ? render_stairs_args.width->ival[0]
        : 100;

    if (slope < 1 || slope > width)
    {
        printf("Slope %d is too steep: Must be between 1 and width (%d)\r\n", slope, width);
        return 1;
    }

    render_stairs(slope, width, color);

    return 0;
}

void render_grid(int gutter, uint8_t color)
{
    const int width = epd_rotated_display_width();
    const int height = epd_rotated_display_height();

    // draw lines
    for (int row = gutter; row < height; row += gutter)
        epd_draw_hline(0, row, width, color, g_framebuffer);

    for (int col = gutter; col < width; col += gutter)
        epd_draw_vline(col, 0, height, color, g_framebuffer);

    // skip printing info if it wouldn't fit
    if (gutter < Alexandria.advance_y * 2)
        return;

    // prepare info
    static char label[32];
    int col = 0, row;

    for (int y = 0; y < height; y += gutter, ++col)
    {
        row = 0;
        for (int x = 0; x < width; x += gutter, ++row)
        {
            // print info
            snprintf(label, sizeof(label), "(%d,%d)", row, col);
            int rx = y + Alexandria.advance_y;
            int cx = x + 4; // margin
            epd_write_default(&Alexandria, label, &cx, &rx, g_framebuffer);
        }
    }
}

static int render_grid_cmd(int argc, char* argv[])
{
    int nerrors = arg_parse(argc, argv, (void**) &render_grid_args);
    if (nerrors > 0)
    {
        arg_print_errors(stdout, render_grid_args.end, "render_grid");
        return 1;
    }

    const int color = validate_color(render_grid_args.color);
    if (color == -1) return 1;

    const int gutter = render_grid_args.gutter->count == 1
        ? render_grid_args.gutter->ival[0]
        : 75;

    render_grid(gutter, color);

    return 0;
}
