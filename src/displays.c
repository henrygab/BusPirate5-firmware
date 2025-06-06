#include <stdint.h>
#include "pico/stdlib.h"
#include "pirate.h"
#include "system_config.h"
#include "bytecode.h"
#include "command_struct.h"
#include "commands.h"
#include "displays.h"

#include "display/default.h"
#ifdef BP_USE_SCOPE
#include "display/scope.h"
#endif

struct _display displays[MAXDISPLAY] = {
    // clang-format off
    {
        .display_periodic   = noperiodic,              // service to regular poll whether a byte ahs arrived
        .display_setup      = disp_default_setup,      // setup UI
        .display_setup_exc  = disp_default_setup_exc,  // real setup
        .display_cleanup    = disp_default_cleanup,    // cleanup for HiZ
        .display_settings   = disp_default_settings,   // display settings
        .display_help       = 0,                       // display small help about the protocol
        .display_name       = "Default",               // friendly name (promptname)
        .display_command    = 0,                       // scope specific commands
        .display_lcd_update = disp_default_lcd_update, // screen write
    },
#ifdef BP_USE_SCOPE
    {
        .display_periodic   = scope_periodic,   // service to regular poll whether a byte ahs arrived
        .display_setup      = scope_setup,      // setup UI
        .display_setup_exc  = scope_setup_exc,  // real setup
        .display_cleanup    = scope_cleanup,    // cleanup for HiZ
        .display_settings   = scope_settings,   // display settings
        .display_help       = scope_help,       // display small help about the protocol
        .display_name       = "Scope",          // friendly name (promptname)
        .display_command    = scope_commands,   // scope specific commands
        .display_lcd_update = scope_lcd_update, // scope screen write
    },
    // clang-format on
#endif
};
/* For Emacs:
 * Local Variables:
 * mode:c
 * indent-tabs-mode:t
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4:
 */
