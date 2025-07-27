#include <stdio.h>
#include "pico/stdlib.h"
#include <stdint.h>
#include <math.h>
#include "pirate.h"
#include "command_struct.h"
#include "ui/ui_prompt.h" //needed for prompt_result struct
#include "ui/ui_parse.h"
#include "ui/ui_const.h"
#include "ui/ui_cmdln.h"

// remove definition of `cmdln` global ..
// to detect inadvertent use, as these functions
// should **_NOT_** touch global state anymore
#undef cmdln

// TODO: UI_CMDBUFFSIZE is globally defined, and therefore
//       ANY command_line_history_t must have identical buffer
//       size.  When memory pressure hits, revisit this.
//       See UI_CMDBUFFSIZE
//       See cmdln_pu() ... which rolls-over the buffer pointer


static const struct prompt_result empty_result;

bool ui_parse_get_hex_ex(command_line_history_t * history, struct prompt_result* result, uint32_t* value) {
    char c;

    *result = empty_result;
    result->no_value = true;
    (*value) = 0;

    while (cmdln_try_peek_ex(history, 0, &c)) // peek at next char
    {
        if (((c >= '0') && (c <= '9'))) {
            (*value) <<= 4;
            (*value) += (c - 0x30);
        } else if (((c | 0x20) >= 'a') && ((c | 0x20) <= 'f')) {
            (*value) <<= 4;
            c |= 0x20;              // to lowercase
            (*value) += (c - 0x57); // 0x61 ('a') -0xa
        } else {
            return false;
        }
        cmdln_try_discard_ex(history, 1); // discard
        result->success = true;
        result->no_value = false;
    }

    return result->success;
}
bool ui_parse_get_bin_ex(command_line_history_t * history, struct prompt_result* result, uint32_t* value) {
    char c;
    *result = empty_result;
    result->no_value = true;
    (*value) = 0;

    while (cmdln_try_peek_ex(history, 0, &c)) // peek at next char
    {
        if ((c < '0') || (c > '1')) {
            return false;
        }
        (*value) <<= 1;
        (*value) += c - 0x30;
        cmdln_try_discard_ex(history, 1); // discard
        result->success = true;
        result->no_value = false;
    }

    return result->success;
}
bool ui_parse_get_bin(struct prompt_result* result, uint32_t* value) {
    return ui_parse_get_bin_ex(&cmdln, result, value);
}
bool ui_parse_get_dec_ex(command_line_history_t * history, struct prompt_result* result, uint32_t* value) {
    char c;

    *result = empty_result;
    result->no_value = true;
    (*value) = 0;

    while (cmdln_try_peek_ex(history, 0, &c)) // peek at next char
    {
        if ((c < '0') || (c > '9')) // if there is a char, and it is in range
        {
            return false;
        }
        (*value) *= 10;
        (*value) += (c - 0x30);
        cmdln_try_discard_ex(history, 1); // discard
        result->success = true;
        result->no_value = false;
    }

    return result->success;
}
bool ui_parse_get_dec(struct prompt_result* result, uint32_t* value) {
    return ui_parse_get_dec_ex(&cmdln, result, value);
}
bool ui_parse_get_int_ex(command_line_history_t * history, struct prompt_result* result, uint32_t* value) {
    bool r1, r2;
    char p1, p2;

    *result = empty_result;
    *value = 0;

    r1 = cmdln_try_peek_ex(history, 0, &p1);
    r2 = cmdln_try_peek_ex(history, 1, &p2);

    if (!r1 || (p1 == 0x00)) // no data, end of data, or no value entered on prompt
    {
        result->no_value = true;
        return false;
    }

    if (r2 && (p2 | 0x20) == 'x') // HEX
    {
        cmdln_try_discard_ex(history, 2); // remove 0x
        ui_parse_get_hex_ex(history, result, value);
        result->number_format = df_hex;  // whatever from ui_const
    } else if (r2 && (p2 | 0x20) == 'b') // BIN
    {
        cmdln_try_discard_ex(history, 2); // remove 0b
        ui_parse_get_bin_ex(history, result, value);
        result->number_format = df_bin; // whatever from ui_const
    } else                              // DEC
    {
        ui_parse_get_dec_ex(history, result, value);
        result->number_format = df_dec; // whatever from ui_const
    }

    return result->success;

}


bool ui_parse_get_string_ex(command_line_history_t * history, struct prompt_result* result, char* str, uint8_t* size) {
    char c = 0;
    uint8_t max_size = *size;

    *result = empty_result;
    result->no_value = true;
    *size = 0;

    while (max_size-- && cmdln_try_peek_ex(history, 0, &c)) {
        if (c <= ' ') {
            break;
        } else if (c <= '~') {
            *str++ = c;
            (*size)++;
        }
        cmdln_try_remove_ex(history, &c);
        result->success = true;
        result->no_value = false;
    }
    // Terminate string
    *str = '\0';

    return result->success;
}

void ui_parse_consume_whitespace_ex(command_line_history_t* history) {
    while ((history->read_offset != history->write_offset) && ((history->buf[history->read_offset] == ' ') || (history->buf[history->read_offset] == ','))) {
        history->read_offset = cmdln_pu(history->read_offset + 1);
    }
}

bool ui_parse_get_macro_ex(command_line_history_t * history, struct prompt_result* result, uint32_t* value) {
    char c = 0;
    bool r = false;

    // cmdln_try_discard(1); // advance 1 position '('
    // BUGBUG -- return value is ignored!
    ui_parse_get_int_ex(history, result, value); // get number
    r = cmdln_try_remove_ex(history, &c);        // advance 1 position ')'
    if (r && c == ')') {
        result->success = true;
    } else {
        result->error = true;
    }
    return result->success;
}

bool ui_parse_get_colon_ex(command_line_history_t * history, uint32_t* value) {
    prompt_result result;
    ui_parse_get_delimited_sequence_ex(history, &result, ':', value);
    return result.success;
}

bool ui_parse_get_dot_ex(command_line_history_t * history, uint32_t* value) {
    prompt_result result;
    ui_parse_get_delimited_sequence_ex(history, &result, '.', value);
    return result.success;
}

bool ui_parse_get_delimited_sequence_ex(command_line_history_t * history, struct prompt_result* result, char delimiter, uint32_t* value) {
    char c;
    *result = empty_result;

    if (cmdln_try_peek_ex(history, 0, &c)) // advance one, did we reach the end?
    {
        if (c == delimiter) // we have a change in bits \o/
        {
            // check that the next char is actually numeric before continue
            //  prevents eating consecutive ....
            if (cmdln_try_peek_ex(history, 1, &c)) {
                if (c >= '0' && c <= '9') {
                    cmdln_try_discard_ex(history, 1); // discard delimiter
                    // BUGBUG -- ignores return value
                    ui_parse_get_int_ex(history, result, value);
                    result->success = true;
                    return true;
                }
            }
        }
    }
    result->no_value = true;
    return false;
}

bool ui_parse_get_attributes_ex(command_line_history_t * history, struct prompt_result* result, uint32_t* attr, uint8_t len) {
    *result = empty_result;
    result->no_value = true;

    for (uint8_t i = 0; i < len; i++) {
        ui_parse_consume_whitespace_ex(history); // eat whitechars
        ui_parse_get_uint32_ex(history, result, &attr[i]);
        if (result->error || result->no_value) {
            return false;
        }
    }

    return true;
}

bool ui_parse_get_bool_ex(command_line_history_t * history, struct prompt_result* result, bool* value) {

    bool r = false;
    char c = 0;

    *result = empty_result; // initialize result with empty result

    r = cmdln_try_peek_ex(history, 0, &c);
    if (!r || c == 0x00) // user pressed enter only
    {
        result->no_value = true;
    } else if (r && ((c | 0x20) == 'x')) // exit
    {
        result->exit = true;
    } else if (((c | 0x20) == 'y')) // yes or no
    {
        result->success = true;
        (*value) = true;
    } else if (((c | 0x20) == 'n')) {
        result->success = true;
        (*value) = false;
    } else // bad result (not number)
    {
        result->error = true;
    }
    cmdln_try_discard_ex(history, 1); // discard
    return true;
}

bool ui_parse_get_float_ex(command_line_history_t * history, struct prompt_result* result, float* value) {
    uint32_t number = 0;
    uint32_t decimal = 0;
    int j = 0;
    bool r = false;
    char c = 0;
    *result = empty_result; // initialize result with empty result

    r = cmdln_try_peek_ex(history, 0, &c);
    if (!r || c == 0x00) // user pressed enter only
    {
        result->no_value = true;
    } else if (r && ((c | 0x20) == 'x')) // exit
    {
        result->exit = true;
    } else if (((c >= '0') && (c <= '9')) || (c == '.') || (c = ',')) // 1-9 decimal
    {
        if ((c >= '0') && (c <= '9')) // there is a number before the . or ,
        {
            ui_parse_get_dec_ex(history, result, &number);
        }

        r = cmdln_try_peek_ex(history, 0, &c);
        if (r && (c == '.' || c == ',')) {
            cmdln_try_discard_ex(history, 1);         // discard seperator
            while (cmdln_try_peek_ex(history, 0, &c)) // peek at next char
            {
                if ((c < '0') || (c > '9')) // if there is a char, and it is in range
                {
                    break;
                }

                decimal *= 10;
                decimal += (c - 0x30);
                cmdln_try_discard_ex(history, 1); // discard
                j++;                  // track digits so we can find the proper divider later...
            }
        }

        (*value) = (float)number;
        (*value) += ((float)decimal / (float)pow(10, j));

        result->success = true;

    } else { // bad result (not number)
        result->error = true;
        return false;
    }

    return true;
}

bool ui_parse_get_uint32_ex(command_line_history_t * history, struct prompt_result* result, uint32_t* value) {
    bool r = false;
    char c = 0;

    *result = empty_result; // initialize result with empty result

    r = cmdln_try_peek_ex(history, 0, &c);
    if (!r || c == 0x00) // user pressed enter only
    {
        result->no_value = true;
    } else if (r && ((c | 0x20) == 'x')) // exit
    {
        result->exit = true;
    } else if ((c >= '0') && (c <= '9')) // 1-9 decimal
    {
        return ui_parse_get_dec_ex(history, result, value);
    } else // bad result (not number)
    {
        result->error = true;
    }
    cmdln_try_discard_ex(history, 1); // discard
    return true;
}

bool ui_parse_get_units_ex(command_line_history_t * history, struct prompt_result* result, char* units, uint8_t length, uint8_t* unit_type) {
    char c;
    uint8_t i = 0;
    *result = empty_result;

    // get the trailing type
    ui_parse_consume_whitespace();

    for (i = 0; i < length; i++) {
        units[i] = 0x00;
    }

    i = 0;
    while (cmdln_try_peek(0, &c)) {
        if ((i < length) && c != 0x00 && c != ' ') {
            units[i] = (c | 0x20); // to lower case
            i++;
            cmdln_try_discard(1);
        } else {
            break;
        }
    }
    units[length - 1] = 0x00;

    // TODO: write our own little string compare...
    if (units[0] == 'n' && units[1] == 's') {
        // ms
        (*unit_type) = freq_ns;
    } else if (units[0] == 'u' && units[1] == 's') {
        // us
        (*unit_type) = freq_us;
    } else if (units[0] == 'm' && units[1] == 's') {
        // ms
        (*unit_type) = freq_ms;
    } else if (units[0] == 'h' && units[1] == 'z') {
        // hz
        (*unit_type) = freq_hz;
    } else if (units[0] == 'k' && units[1] == 'h' && units[2] == 'z') {
        // khz
        (*unit_type) = freq_khz;
    } else if (units[0] == 'm' && units[1] == 'h' && units[2] == 'z') {
        // mhz
        (*unit_type) = freq_mhz;
    } else if (units[0] == '%') {
        //%
        (*unit_type) = freq_percent;
    } else {
        result->no_value = true;
        return false;
    }

    result->success = true;
    return true;
}
