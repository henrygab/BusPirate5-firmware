
#define BP_DEBUG_OVERRIDE_DEFAULT_CATEGORY BP_DEBUG_CAT_CMD_COMMENTS

#include "pirate.h"
#include "cmd_comment.h"
#include "stdbool.h"
#include "ui/ui_cmdln.h"
#include "string.h"


void cmd_comment_handler(uint32_t start_offset, uint32_t end_offset) {
    start_offset = cmdln_pu(start_offset + cmdln.read_offset);
    end_offset = cmdln_pu(end_offset + cmdln.read_offset);

    // Total hack:
    if (start_offset == end_offset) {
        PRINT_DEBUG("##\r\n");
    } else if (start_offset < end_offset) {
        char last_char = cmdln.buf[end_offset];
        cmdln.buf[end_offset] = 0x00; // null-terminate the string
        PRINT_DEBUG("#%s%c\r\n", &cmdln.buf[start_offset], last_char);
        cmdln.buf[end_offset] = last_char; // restore the last character
    } else {
        char end_of_buffer_char = cmdln.buf[UI_CMDBUFFSIZE - 1];
        char last_char = cmdln.buf[end_offset];
        cmdln.buf[UI_CMDBUFFSIZE - 1] = 0x00; // null-terminate the first half of the string
        cmdln.buf[end_offset] = 0x00; // null-terminate the second half of the string
        PRINT_DEBUG(
            "#%s%c%s%c\r\n",
            &cmdln.buf[start_offset], end_of_buffer_char,
            &cmdln.buf[0], last_char
            );
        cmdln.buf[UI_CMDBUFFSIZE - 1] = end_of_buffer_char; // restore the end of buffer character
        cmdln.buf[end_offset] = last_char; // restore the last character
    }

    // TODO: log the current command line (the comment) into the debug output stream
    //       This will make it easier to correlate other debug output with
    //       where in a long script (or macro, or other set of commands) that output
    //       was generated.

    // start_offset is array index of start of the command in the global command line buffer
    // end_offset   is array index of start of the command in the global command line buffer
    // HOWEVER ... the command line buffer is used as a circular buffer, so these offsets
    //       can NOT be used directly ... have to copy the command line to our own
    //       buffer to ensure it's a single contiguous string.
    // As a hack, if end_offset < start_offset, could...
    //  copy the last character of the command line buffer to a temporary variable
    //  set that last character to 0x00
    //  treat g_...[start_offset] as a null-terminated string
    return;
}
