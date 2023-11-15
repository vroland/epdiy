#pragma once
/* Helper functions and macros for common use cases,
 * when registering or implementing commands.
 */

#include <stdbool.h>
#include <stdint.h>

#include <esp_console.h>
#include <argtable3/argtable3.h>

#ifndef ARRAY_SIZE
/**
 * Returns size of a (static) C array.
 */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))
#endif

/**
 * Checks whether the given color argument is a valid color.
 * I.e. when it's color value is possible to represent by an uint8_t (0x00 - 0xFF).
 * If no color argument was provided by the user, returns the default color of 0x00.
 *
 * @param inout_color initial value will be used as default color,
 *                    when the user did not specify a color.
 *                    when successful, will be set to the given color.
 * @param arg user-specified color argument
 * @return whether the given color is valid
 */
bool validate_color(uint8_t* inout_color, struct arg_int* arg);

/**
 * Determines the number of arguments stored in a struct (container).
 * That is usually the number of arguments for a given command and
 * can be used when initializing the arg_end parameter.
 *
 * This macro assumes, that
 * 1. each argument inside the struct is referenced by a pointer,
 * 2. each struct ends with an arg_end*,
 * 3. there are no other members in the struct, besides different argument types.
 */
#define NARGS(container) ((sizeof(container) / sizeof(struct arg_end*)) - 1)

/**
 * Handles argument validation for the command.
 * Assumes that `argc` and `argv` variables are visible, thus should
 * only be used inside the command implementation function.
 *
 * @param args_struct name of the (static) argument struct.
 */
#define HANDLE_ARGUMENTS(args_struct) \
    {                                                               \
        int nerrors = arg_parse(argc, argv, (void**) &args_struct); \
        if (nerrors > 0) {                                          \
            arg_print_errors(stdout, args_struct.end, argv[0]);     \
            return 1;                                               \
        }                                                           \
    }

/**
 * Get optional argument value if provided by the user. Otherwise use the default value.
 *
 * @param arg pointer to argument struct (e.g. struct arg_int*)
 * @param accessor accessor used to retrieve the first value (e.g. ival for struct arg_int)
 * @param default_value
 */
#define GET_ARG(arg, accessor, default_value) (arg)->count == 1 ? (arg)->accessor[0] : (default_value)

/**
 * Alias for GET_ARG, specialized for int arguments.
 *
 * @param arg pointer to an struct arg_int
 * @param default_value
 */
#define GET_INT_ARG(arg, default_value) GET_ARG(arg, ival, default_value)
