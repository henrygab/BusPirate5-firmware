#pragma once

#include <ui/ui_cmdln.h>


// gets all number accepted formats
bool ui_parse_get_int_ex(command_line_history_t * history, struct prompt_result* result, uint32_t* value);
// BUGBUG -- rename this to reflect it also accepts eXit option!
// get DEC (base 10) with eXit option
bool ui_parse_get_uint32_ex(command_line_history_t * history, struct prompt_result* result, uint32_t* value);
bool ui_parse_get_string_ex(command_line_history_t * history, struct prompt_result* result, char* str, uint8_t* size);
// BUGBUG -- rename this to reflect it also accepts eXit option!
// get float with eXit option
bool ui_parse_get_float_ex(command_line_history_t * history, struct prompt_result* result, float* value);
bool ui_parse_get_delimited_sequence_ex(command_line_history_t * history, struct prompt_result* result, char delimiter, uint32_t* value);
bool ui_parse_get_units_ex(command_line_history_t * history, struct prompt_result* result, char* units, uint8_t length, uint8_t* unit_type);
bool ui_parse_get_macro_ex(command_line_history_t * history, struct prompt_result* result, uint32_t* value);
bool ui_parse_get_attributes_ex(command_line_history_t * history, struct prompt_result* result, uint32_t* attr, uint8_t len);
bool ui_parse_get_bool_ex(command_line_history_t * history, struct prompt_result* result, bool* value);
bool ui_parse_get_colon_ex(command_line_history_t * history, uint32_t* value);
bool ui_parse_get_dot_ex(command_line_history_t * history, uint32_t* value);
void ui_parse_consume_whitespace_ex(command_line_history_t * history);

#if !defined(BP_UI_PARSE_NO_LEGACY_FUNCTIONS)

// These are the old UI parsing functions, which presumed
// a single command line history buffer / current command line.
// Callers of these functions need to be updated to have the
// command line history buffer passed in as a parameter, using
// the ..._ex() version of the function.

inline bool ui_parse_get_int(struct prompt_result* result, uint32_t* value) {
    return ui_parse_get_int_ex(&cmdln, result, value);
}
inline bool ui_parse_get_uint32(struct prompt_result* result, uint32_t* value) {
    return ui_parse_get_uint32_ex(&cmdln, result, value);
}
inline bool ui_parse_get_string(struct prompt_result* result, char* str, uint8_t* size) {
    return ui_parse_get_string_ex(&cmdln, result, str, size);
}
inline bool ui_parse_get_float(struct prompt_result* result, float* value) {
    return ui_parse_get_float_ex(&cmdln, result, value);
}
inline bool ui_parse_get_delimited_sequence(struct prompt_result* result, char delimiter, uint32_t* value) {
    return ui_parse_get_delimited_sequence_ex(&cmdln, result, delimiter, value);
}
inline bool ui_parse_get_units(struct prompt_result* result, char* units, uint8_t length, uint8_t* unit_type) {
    return ui_parse_get_units_ex(&cmdln, result, units, length, unit_type);
}
inline bool ui_parse_get_macro(struct prompt_result* result, uint32_t* value) {
    return ui_parse_get_macro_ex(&cmdln, result, value);
}
inline bool ui_parse_get_attributes(struct prompt_result* result, uint32_t* attr, uint8_t len) {
    return ui_parse_get_attributes_ex(&cmdln, result, attr, len);
}
inline bool ui_parse_get_bool(struct prompt_result* result, bool* value) {
    return ui_parse_get_bool_ex(&cmdln, result, value);
}
inline bool ui_parse_get_colon(uint32_t* value) {
    return ui_parse_get_colon_ex(&cmdln, value);
}
inline bool ui_parse_get_dot(uint32_t* value) {
    return ui_parse_get_dot_ex(&cmdln, value);
}
inline void ui_parse_consume_whitespace(void) {
    ui_parse_consume_whitespace_ex(&cmdln);
}

#endif // BP_UI_PARSE_NO_LEGACY_FUNCTIONS
