#pragma once

#include <ui/ui_cmdln.h>

SYNTAX_STATUS syntax_compile_ex(command_line_history_t* command_history_buffer);

SYNTAX_STATUS syntax_compile(); // defaults to the global command line history buffer (terminal input)
SYNTAX_STATUS syntax_run(void);
SYNTAX_STATUS syntax_post(void);
