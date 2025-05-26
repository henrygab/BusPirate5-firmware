
#define BP_DEBUG_OVERRIDE_DEFAULT_CATEGORY BP_DEBUG_CAT_CMD_COMMENTS

#include "pirate.h"
#include "cmd_comment.h"
#include "stdbool.h"
#include "ui/ui_cmdln.h"
#include "string.h"

void cmd_comment_handler(struct command_result* res) {

    PRINT_DEBUG("cmd_comment_handler()\r\n");

    // TODO: log the current command line (the comment) into the debug output stream
    // COPY_OF_COMMAND_LINE comment_command_line;
    // retrieve_command_line_copy(&comment_command_line);
    // PRINT_FATAL("CmdlineComment: %s", comment_command_line.buf);
    // PRINT_INFO("CmdlineComment: %s", comment_command_line.buf);

    res->success = true;
    res->error = false;
    res->exit = false;
    res->no_value = true;
    res->help_flag = false;
    return;
}
