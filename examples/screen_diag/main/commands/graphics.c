#include "graphics.h"

#include <stdio.h>
#include <string.h>

#include <esp_console.h>
#include <argtable3/argtable3.h>

#include "../epd.h"

static struct {
    struct arg_int *x, *y;
    struct arg_int *len;
    struct arg_int *color;
    struct arg_end *end;
} draw_hvline_args;

static struct {
    struct arg_int *from_x, *from_y;
    struct arg_int *to_x, *to_y;
    struct arg_int *color;
    struct arg_end *end;
} draw_line_args ;

static struct {
    struct arg_int *x, *y;
    struct arg_int *width, *height;
    struct arg_int *color;
    struct arg_end *end;
} draw_rect_args;

static struct {
    struct arg_int *x, *y;
    struct arg_int *radius;
    struct arg_int *color;
    struct arg_end *end;
} draw_circle_args;

static struct {
    struct arg_int *x0, *y0;
    struct arg_int *x1, *y1;
    struct arg_int *x2, *y2;
    struct arg_int *color;
    struct arg_end *end;
} draw_triangle_args;

static int draw_hline(int argc, char* argv[]);
static int draw_vline(int argc, char* argv[]);
static int draw_line(int argc, char* argv[]);
static int draw_rect(int argc, char* argv[]);
static int fill_rect(int argc, char* argv[]);
static int draw_circle(int argc, char* argv[]);
static int fill_circle(int argc, char* argv[]);
static int draw_triangle(int argc, char* argv[]);
static int fill_triangle(int argc, char* argv[]);

void register_graphics_commands(void)
{
    // setup args
    draw_hvline_args.x = arg_intn(NULL, NULL, "<x>", 1, 1, "start x position");
    draw_hvline_args.y = arg_intn(NULL, NULL, "<y>", 1, 1, "start y position");
    draw_hvline_args.len = arg_intn(NULL, NULL, "<len>", 1, 1, "length of the line");
    draw_hvline_args.color = arg_intn(NULL, NULL, "<color>", 0, 1, "default value: 0x00");
    draw_hvline_args.end = arg_end(3);

    draw_line_args.from_x = arg_intn(NULL, NULL, "<start_x>", 1, 1, "start x position");
    draw_line_args.from_y = arg_intn(NULL, NULL, "<start_y>", 1, 1, "start y position");
    draw_line_args.to_x = arg_intn(NULL, NULL, "<end_x>", 1, 1, "end x position");
    draw_line_args.to_y = arg_intn(NULL, NULL, "<end_y>", 1, 1, "end y position");
    draw_line_args.color = arg_intn(NULL, NULL, "<color>", 0, 1, "default value: 0x00");
    draw_line_args.end = arg_end(5);

    draw_rect_args.x = arg_intn(NULL, NULL, "<x>", 1, 1, "top left x position");
    draw_rect_args.y = arg_intn(NULL, NULL, "<y>", 1, 1, "top left y position");
    draw_rect_args.width = arg_intn(NULL, NULL, "<width>", 1, 1, "square width");
    draw_rect_args.height = arg_intn(NULL, NULL, "<height>", 1, 1, "square height");
    draw_rect_args.color = arg_intn(NULL, NULL, "<color>", 0, 1, "default value: 0x00");
    draw_rect_args.end = arg_end(5);

    draw_circle_args.x = arg_intn(NULL, NULL, "<center_x>", 1, 1, "center x position");
    draw_circle_args.y = arg_intn(NULL, NULL, "<center_y>", 1, 1, "center y position");
    draw_circle_args.radius = arg_intn(NULL, NULL, "<radius>", 1, 1, "circle radius");
    draw_circle_args.color = arg_intn(NULL, NULL, "<color>", 0, 1, "default value: 0x00");
    draw_circle_args.end = arg_end(5);

    draw_triangle_args.x0 = arg_intn(NULL, NULL, "<x0>", 1, 1, "first edge x position");
    draw_triangle_args.y0 = arg_intn(NULL, NULL, "<y0>", 1, 1, "first edge y position");
    draw_triangle_args.x1 = arg_intn(NULL, NULL, "<x1>", 1, 1, "second edge x position");
    draw_triangle_args.y1 = arg_intn(NULL, NULL, "<y1>", 1, 1, "second edge y position");
    draw_triangle_args.x2 = arg_intn(NULL, NULL, "<x0>", 1, 1, "third edge x position");
    draw_triangle_args.y2 = arg_intn(NULL, NULL, "<y0>", 1, 1, "third edge y position");
    draw_triangle_args.color = arg_intn(NULL, NULL, "<color>", 0, 1, "default value: 0x00");
    draw_triangle_args.end = arg_end(7);

    // register commands
    const esp_console_cmd_t commands[] = {
        {
            .command = "draw_hline",
            .help = "Draw horizontal line.",
            .hint = NULL,
            .func = &draw_hline,
            .argtable = &draw_hvline_args
        },
        {
            .command = "draw_vline",
            .help = "Draw vertical line.",
            .hint = NULL,
            .func = &draw_vline,
            .argtable = &draw_hvline_args
        },
        {
            .command = "draw_line",
            .help = "Draw line between two points.",
            .hint = NULL,
            .func = &draw_line,
            .argtable = &draw_line_args
        },
        {
            .command = "draw_rect",
            .help = "Draw a rectangle.",
            .hint = NULL,
            .func = &draw_rect,
            .argtable = &draw_rect_args
        },
        {
            .command = "fill_rect",
            .help = "Draw a filled rectangle.",
            .hint = NULL,
            .func = &fill_rect,
            .argtable = &draw_rect_args
        },
        {
            .command = "draw_circle",
            .help = "Draw a circle.",
            .hint = NULL,
            .func = &draw_circle,
            .argtable = &draw_circle_args
        },
        {
            .command = "fill_circle",
            .help = "Draw a filled circle.",
            .hint = NULL,
            .func = &fill_circle,
            .argtable = &draw_circle_args
        },
        {
            .command = "draw_triangle",
            .help = "Draw a triangle from three different points.",
            .hint = NULL,
            .func = &draw_triangle,
            .argtable = &draw_triangle_args
        },
        {
            .command = "fill_triangle",
            .help = "Draw a filled triangle from three different points.",
            .hint = NULL,
            .func = &fill_triangle,
            .argtable = &draw_triangle_args
        }
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

static int draw_hline(int argc, char* argv[])
{
    int nerrors = arg_parse(argc, argv, (void**) &draw_hvline_args);
    if (nerrors > 0)
    {
        arg_print_errors(stdout, draw_hvline_args.end, "draw_hline");
        return 1;
    }

    const int color = validate_color(draw_hvline_args.color);
    if (color == -1) return 1;

    epd_draw_hline(draw_hvline_args.x->ival[0], draw_hvline_args.y->ival[0],
        draw_hvline_args.len->ival[0], color, g_framebuffer);

    return 0;
}

static int draw_vline(int argc, char* argv[])
{
    int nerrors = arg_parse(argc, argv, (void**) &draw_hvline_args);
    if (nerrors > 0)
    {
        arg_print_errors(stdout, draw_hvline_args.end, "draw_vline");
        return 1;
    }

    const int color = validate_color(draw_hvline_args.color);
    if (color == -1) return 1;

    epd_draw_vline(draw_hvline_args.x->ival[0], draw_hvline_args.y->ival[0],
        draw_hvline_args.len->ival[0], color, g_framebuffer);

    return 0;
}

static int draw_line(int argc, char* argv[])
{
    int nerrors = arg_parse(argc, argv, (void**) &draw_line_args);
    if (nerrors > 0)
    {
        arg_print_errors(stdout, draw_line_args.end, "draw_line");
        return 1;
    }

    const int color = validate_color(draw_line_args.color);
    if (color == -1) return 1;

    epd_draw_line(draw_line_args.from_x->ival[0], draw_line_args.from_y->ival[0],
        draw_line_args.to_x->ival[0], draw_line_args.to_y->ival[0],
        color, g_framebuffer);

    return 0;
}

static int draw_rect(int argc, char* argv[])
{
    int nerrors = arg_parse(argc, argv, (void**) &draw_rect_args);
    if (nerrors > 0)
    {
        arg_print_errors(stdout, draw_rect_args.end, "draw_rect");
        return 1;
    }

    const int color = validate_color(draw_rect_args.color);
    if (color == -1) return 1;

    EpdRect rect = {
        .x = draw_rect_args.x->ival[0],
        .y = draw_rect_args.y->ival[0],
        .width = draw_rect_args.width->ival[0],
        .height = draw_rect_args.height->ival[0]
    };

    epd_draw_rect(rect, color, g_framebuffer);

    return 0;
}

static int fill_rect(int argc, char* argv[])
{
    int nerrors = arg_parse(argc, argv, (void**) &draw_rect_args);
    if (nerrors > 0)
    {
        arg_print_errors(stdout, draw_rect_args.end, "fill_rect");
        return 1;
    }

    const int color = validate_color(draw_rect_args.color);
    if (color == -1) return 1;

    EpdRect rect = {
        .x = draw_rect_args.x->ival[0],
        .y = draw_rect_args.y->ival[0],
        .width = draw_rect_args.width->ival[0],
        .height = draw_rect_args.height->ival[0]
    };

    epd_fill_rect(rect, color, g_framebuffer);

    return 0;
}

static int draw_circle(int argc, char* argv[])
{
    int nerrors = arg_parse(argc, argv, (void**) &draw_circle_args);
    if (nerrors > 0)
    {
        arg_print_errors(stdout, draw_circle_args.end, "draw_circle");
        return 1;
    }

    const int color = validate_color(draw_circle_args.color);
    if (color == -1) return 1;

    epd_draw_circle(draw_circle_args.x->ival[0], draw_circle_args.y->ival[0],
        draw_circle_args.radius->ival[0], color, g_framebuffer);

    return 0;
}

static int fill_circle(int argc, char* argv[])
{
    int nerrors = arg_parse(argc, argv, (void**) &draw_circle_args);
    if (nerrors > 0)
    {
        arg_print_errors(stdout, draw_circle_args.end, "fill_circle");
        return 1;
    }

    const int color = validate_color(draw_circle_args.color);
    if (color == -1) return 1;

    epd_fill_circle(draw_circle_args.x->ival[0], draw_circle_args.y->ival[0],
        draw_circle_args.radius->ival[0], color, g_framebuffer);

    return 0;
}

static int draw_triangle(int argc, char* argv[])
{
    int nerrors = arg_parse(argc, argv, (void**) &draw_triangle_args);
    if (nerrors > 0)
    {
        arg_print_errors(stdout, draw_triangle_args.end, "draw_triangle");
        return 1;
    }

    const int color = validate_color(draw_triangle_args.color);
    if (color == -1) return 1;

    epd_draw_triangle(draw_triangle_args.x0->ival[0], draw_triangle_args.y0->ival[0],
        draw_triangle_args.x1->ival[0], draw_triangle_args.y1->ival[0],
        draw_triangle_args.x2->ival[0], draw_triangle_args.y2->ival[0],
        color, g_framebuffer);

    return 0;
}

static int fill_triangle(int argc, char* argv[])
{
    int nerrors = arg_parse(argc, argv, (void**) &draw_triangle_args);
    if (nerrors > 0)
    {
        arg_print_errors(stdout, draw_triangle_args.end, "fill_triangle");
        return 1;
    }

    const int color = validate_color(draw_triangle_args.color);
    if (color == -1) return 1;

    epd_fill_triangle(draw_triangle_args.x0->ival[0], draw_triangle_args.y0->ival[0],
        draw_triangle_args.x1->ival[0], draw_triangle_args.y1->ival[0],
        draw_triangle_args.x2->ival[0], draw_triangle_args.y2->ival[0],
        color, g_framebuffer);

    return 0;
}
