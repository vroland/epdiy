#include "commands.h"

bool validate_color(uint8_t* inout_color, struct arg_int* arg)
{
    int user_color = arg->count != 0 ? arg->ival[0] : *inout_color;
    if (user_color < 0 || user_color > 0xFF)
    {
        printf("Invalid color %d (0x%02x): Must be in range 0x00 to 0xFF.\r\n", user_color, (uint8_t) user_color);
        return false;
    }

    *inout_color = (uint8_t) user_color;

    return true;
}
