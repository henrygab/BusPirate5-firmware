
#define BP_DEBUG_OVERRIDE_DEFAULT_CATEGORY BP_DEBUG_CAT_CMD_COMMENTS

#include "pirate.h"
#include "cmd_comment.h"
#include "stdbool.h"
#include "ui/ui_cmdln.h"
#include "string.h"

// What is needed to declare the parameters?
// List of parameters that the command would accept:
// -h -- universal help flag (does not need to be declared)
//
// * bool positional_allowed
// * char flag
// * permitted data type(s)


static void hack_to_help_validate_command_line_parsing(uint32_t start_offset, uint32_t end_offset) {

    // The ultimate goal is to verify APIs act the same after changing the backend/underlying logic.

    // # Comment lines that will successfully find corresponding flags:
    // # -----------------------------------------
    /*
# -h finds flag 'h'
# find flag -h
# find 'x' with hex value 21845 -x 0x5555
# and 'x' with bin value 43690 -x 0b1010101010101010
# but `y` with string abc -y abc
    */

    // # Behavior that requires review:
    // # -------------------------------
    //
    // # Next line should successfully find flag 'x' with value 1234,
    // # but appears to error out because there's an earlier '-x' in single-quotes.
    // # (??? this should not be detected as a flag ???)
    //
    // # find '-x' with dec value 1234 -x 1234
    //
    // # Next line should FAIL to find any flags, because the argument is
    // # preceded by TWO dashes, not one.   Currently, all these ARE detected,
    // # which appears to be a bug.
    //
    // # No flags --h --x 1234 --y abc
    //
    // # Why is getting positional item at index 6 as a string
    // # returning a reformatted decimal value? (43690) instead
    // # of the original binary value (0b1010101010101010)?
    // # if intended to get reformatted value as string, perhaps
    // # name the API accordingly?
    //
    // # and 'x' with bin value 43690 -x 0b1010101010101010
    //



    // # Comment lines that should NOT find flags:
    // # -----------------------------------------
    /*
    */

    // For validating the major changes, also call various functions (ignoring failures)

    // finding flags by name
    do { // cmdln_args_find_flag('h')
        bool has_flag_h = cmdln_args_find_flag('h');

        command_var_t x_value_stuff = {0};
        uint32_t x_value = 0;
        bool has_flag_value_x = cmdln_args_find_flag_uint32('x', &x_value_stuff, &x_value);

        command_var_t y_value_stuff = {0};
        char y_string[5] = {0};
        bool has_flag_str_y = cmdln_args_find_flag_string('y', &y_value_stuff, 5, &y_string[0]);

        PRINT_DEBUG( "cmd_comment_handler:   ret | err | x value (dec/hex)     | y str \r\n");
        PRINT_DEBUG( "cmd_comment_handler:  -----|-----|-----------------------|-------\r\n");
        PRINT_DEBUG( "cmd_comment_handler:   %c%c%c | %c%c%c | %10d / %08X | %s\r\n",
            has_flag_h          ? 'H' : '.',
            has_flag_value_x    ? 'X' : '.',
            has_flag_str_y      ? 'Y' : '.',
            has_flag_h          ? '.' : 'h',
            x_value_stuff.error ? 'x' : '.',
            y_value_stuff.error ? 'y' : '.',
            (has_flag_value_x && x_value_stuff.has_value) ? x_value : 0,
            (has_flag_value_x && x_value_stuff.has_value) ? x_value : 0,
            (has_flag_str_y   && y_value_stuff.has_value) ? y_string : ""
        );
    } while(false);



    // Then validate getting things by position...
    // NOTE: here, limiting string to 5 characters for debug print purposes
    PRINT_DEBUG( "cmd_comment_handler:   arg | UFS | uint32                | Float  | String\r\n");
    PRINT_DEBUG( "cmd_comment_handler:   ----|-----|-----------------------|--------|--------\r\n");

    do { // cmdln_args_string_by_position(0, ...)
        for (int_fast8_t i = 0; i < 10; ++i) {
            float vf = 0.0f;
            char vfs[20] = {0};
            uint32_t vi = 0;
            char vs[6] = {0};
            bool bf = cmdln_args_float_by_position(i, &vf);
            bool bi = cmdln_args_uint32_by_position(i, &vi);
            bool bs = cmdln_args_string_by_position(i, 6, &vs[0]);
            if (bf) {
                // RTT does not output float....
                int written_chars = snprintf(&vfs[0], 20, "%f", vf);
                if (written_chars >= 20) {
                    vfs[0] = 'e';
                    vfs[1] = 'r';
                    vfs[2] = 'r';
                    vfs[19] = 0x00; // null-terminate
                }
            }

            // ( "cmd_comment_handler:     1 | UFS | 4294967295 / F7E6D5C4 | 10.374 |  xxxxx\r\n");
            // ( "cmd_comment_handler:     1 | ... |          0 / 00000000 |  0     |  \r\n");
            PRINT_DEBUG(
                "cmd_comment_handler:   %3d | %c%c%c | %10d / %8X | %20s  |  %s\r\n",
                i,
                bi ? 'U' : '.',
                bf ? 'F' : '.',
                bs ? 'S' : '.',
                bi ? vi : 0,
                bi ? vi : 0,
                bf ? vfs : "",
                bs ? &vs[0] : ""
            );
        }

    } while (false);
    
}

void cmd_comment_handler(uint32_t start_offset, uint32_t end_offset) {
    cmdline_validate_invariants_command_line(&cmdln);

    start_offset = cmdln_pu(start_offset + cmdln.read_offset);
    end_offset = cmdln_pu(end_offset + cmdln.read_offset);

    // Total printing hack (which can be removed when history is rotated after each command)
    if (start_offset == end_offset) {
        PRINT_WARNING("##\r\n");
    } else if (start_offset < end_offset) {
        char last_char = cmdln.buf[end_offset];
        cmdln.buf[end_offset] = 0x00; // null-terminate the string
        PRINT_WARNING("#%s%c\r\n", &cmdln.buf[start_offset], last_char);
        cmdln.buf[end_offset] = last_char; // restore the last character
    } else {
        char end_of_buffer_char = cmdln.buf[UI_CMDBUFFSIZE - 1];
        char last_char = cmdln.buf[end_offset];
        cmdln.buf[UI_CMDBUFFSIZE - 1] = 0x00; // null-terminate the first half of the string
        cmdln.buf[end_offset] = 0x00; // null-terminate the second half of the string
        PRINT_WARNING(
            "#%s%c%s%c\r\n",
            &cmdln.buf[start_offset], end_of_buffer_char,
            &cmdln.buf[0], last_char
            );
        cmdln.buf[UI_CMDBUFFSIZE - 1] = end_of_buffer_char; // restore the end of buffer character
        cmdln.buf[end_offset] = last_char; // restore the last character
    }
    hack_to_help_validate_command_line_parsing(start_offset, end_offset);

    return;
}
