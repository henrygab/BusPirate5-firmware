
#include "debug_rtt.h"

bool _SYSTEM_INITIALIZATION_COMPLETED = false;
bool _SYSTEM_INITIALIZATION_COMPLETED_BY_CORE[2] = { false, false };

void bp_mark_system_initialized(void) {
    unsigned int core = get_core_num();
    if (core == 0) {
        _SYSTEM_INITIALIZATION_COMPLETED_BY_CORE[0] = true;
        if (_SYSTEM_INITIALIZATION_COMPLETED_BY_CORE[1]) {
            _SYSTEM_INITIALIZATION_COMPLETED = true;
        }
    } else if (core == 1) {
        _SYSTEM_INITIALIZATION_COMPLETED_BY_CORE[1] = true;
        if (_SYSTEM_INITIALIZATION_COMPLETED_BY_CORE[0]) {
            _SYSTEM_INITIALIZATION_COMPLETED = true;
        }
    }
}
bool bp_is_system_initialized(void) {
    return _SYSTEM_INITIALIZATION_COMPLETED;
}

// PICO SDK defines the following (weak) functions for handling assert() / hard_assert() failures:
//
//     void __assert_func(const char *file, int line, const char *func, const char *failedexpr);
//     void hard_assertion_failure(void);
//
// Override these functions with our own versions so that information is also stored in RTT
// and via terminal output.

void __assert_func(const char *file, int line, const char *func, const char *failedexpr) {
    BP_DEBUG_PRINT(BP_DEBUG_LEVEL_FATAL, BP_DEBUG_CAT_CATCHALL,
        "assertion \"%s\" failed: file \"%s\", line %d%s%s\n",
        failedexpr, file, line, func ? ", function: " : "",
        func ? func : ""
        );
    printf(
        "assertion \"%s\" failed: file \"%s\", line %d%s%s\n",
        failedexpr, file, line, func ? ", function: " : "",
        func ? func : ""
        );
    exit(1);
}
void hard_assertion_failure(void) {
    BP_DEBUG_PRINT(BP_DEBUG_LEVEL_FATAL, BP_DEBUG_CAT_CATCHALL,
        "hard_assert() failed\n"
        );
    panic("Hard assert");
}



bp_debug_categories_t _DEBUG_ENABLED_CATEGORIES = {
    .bitfield.catchall = 1u,
    .bitfield.early_boot = 1u,
    //.bitfield.onboard_pixels = 1u,
    .bitfield.onboard_storage = 1u,
    .bitfield.otp = 1u,
    .bitfield.certificate = 1u,
    // lots of zero-init'd values likely here
    .bitfield.temp = 1u,
}; // mask of enabled debug categories

// If listed, will be initialized to the specified value.
// Otherwise, will be initialized to zero (BP_DEBUG_LEVEL_FATAL).
// Note that, unless also enabled in the bitmask above,
// only FATAL messages will be output.
bp_debug_level_t _DEBUG_LEVELS[E_DEBUG_CAT_TEMP+1] = {
    [E_DEBUG_CAT_CATCHALL       ] = BP_DEBUG_LEVEL_FATAL,
    [E_DEBUG_CAT_EARLY_BOOT     ] = BP_DEBUG_LEVEL_VERBOSE,
    [E_DEBUG_CAT_ONBOARD_PIXELS ] = BP_DEBUG_LEVEL_VERBOSE,
    [E_DEBUG_CAT_ONBOARD_STORAGE] = BP_DEBUG_LEVEL_VERBOSE,
    [E_DEBUG_CAT_OTP            ] = BP_DEBUG_LEVEL_DEBUG,
    // add others here, in order of enumeration value
    [E_DEBUG_CAT_TEMP           ] = BP_DEBUG_LEVEL_DEBUG,
}; // each category can have its own debug print level

// TODO: Split this into parts to allow greater flexibility and performance:
//       (a) formatting of the message into a per-core temporary buffer via
//           `int snprintf_(char* buffer, size_t count, const char* format, ...);`
//       (b) optionally, output of the formatted buffer via RTT
//       (c) optionally, output of the formatted buffer via UART
//       (d) optionally, output of the formatted buffer via console (printf)
// Specifically, the above will avoid the need to reformat the message for each
// output method.
// NOTE: Reserve two characters at the start, to allow switching between RTT terminals
//       Start with 0xFF 0xNN (0xNN is the hex character for the terminal number .. '0' .. '9', 'A' .. 'F'
//       Then, for non-RTT output, just strip the first two characters.

#define _DEBUG_PRINT_BUFFER_CHAR_COUNT (511u)
char _DEBUG_PRINT_BUFFER[2][_DEBUG_PRINT_BUFFER_CHAR_COUNT+1]; // one buffer per core

void bp_debug_internal_print_handler(uint8_t category, const char* file, int line, const char* function, const char *format_string, ...) {
    char * buffer = _DEBUG_PRINT_BUFFER[get_core_num()];
    size_t offset = 0u;

    uint8_t cat_modulo16 = category % 16u;

    buffer[offset] = 0xFF; // RTT terminal switch sentinel
    buffer[offset+1] = (cat_modulo16 < 10) ? '0' + cat_modulo16 : 'A' + cat_modulo16 - 10;
    offset += 2;

    if (file != NULL) {
        offset += snprintf(
            buffer + offset,
            _DEBUG_PRINT_BUFFER_CHAR_COUNT - offset,
            "%s:%4d ",
            file,
            line
        );
    }
    if (function != NULL) {
        offset += snprintf(
            buffer + offset,
            _DEBUG_PRINT_BUFFER_CHAR_COUNT - offset,
            ":%s ",
            function
        );
    }
    if (format_string != NULL) {
        va_list ParamList;
        va_start(ParamList, format_string);
        offset += vsnprintf(
            buffer + offset,
            _DEBUG_PRINT_BUFFER_CHAR_COUNT - offset,
            format_string,
            ParamList
        );
        va_end(ParamList);
    }

    // Now that the message if fully formatted, output it via RTT
    SEGGER_RTT_Write(0, buffer, offset);

    // TODO: also output via debug UART, console (puts()), etc.
    // puts(buffer+2);
    // NOTE: can we avoid blocking functions here?

    return;
}
