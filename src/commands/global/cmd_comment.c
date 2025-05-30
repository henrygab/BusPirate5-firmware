
#define BP_DEBUG_OVERRIDE_DEFAULT_CATEGORY BP_DEBUG_CAT_CMD_COMMENTS

#include "pirate.h"
#include "cmd_comment.h"
#include "stdbool.h"
#include "ui/ui_cmdln.h"
#include "string.h"


void cmd_comment_handler(uint32_t start_offset, uint32_t end_offset) {

    PRINT_DEBUG("cmd_comment_handler() - [%d .. %d]\r\n", start_offset, end_offset);
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

    // PRINT_FATAL("CmdlineComment: %s", comment_command_line.buf);
    // PRINT_INFO("CmdlineComment: %s", comment_command_line.buf);
    return;
}
