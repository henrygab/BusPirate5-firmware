#define BP_DEBUG_OVERRIDE_DEFAULT_CATEGORY BP_DEBUG_CAT_CMDLINE_PARSER

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include <stdint.h>
#include "pirate.h"
#include "system_config.h"
#include "ui/ui_const.h"
#include "ui/ui_prompt.h"
#include "ui/ui_cmdln.h"

// the command line struct with buffer and several offsets

/// @brief start/end offsets here correspond to one entire line of input 
///        Note that the buffer is a circular buffer, so it's not safe to
///        presume that any strings are contiguous, as they may span the
///        end / beginning of that circular buffer.
// TODO: Verify that the offsets are ALWAYS between 0 and UI_CMDBUFFSIZE-1
command_line_history_t cmdln;

/// @brief When supporting multiple commands in a single line, this structure
///        provides offsets (relative to cmdln.buf) to the current command,
///        much like cmdln provides offsets to the entire line of input.
///        Therefore, for the same reasons, it is not safe to presume that
///        any strings are contiguous, as they may span the end / beginning
///        of that circular buffer.
// TODO: Check whether the offsets in this structure are permitted to
//       exceed UI_CMDBUFFSIZE?
// TODO: change so that cmdln always has current input line starting at
//       offset 0, and all other history lines are valid / safe to strcpy()
//       to offset 0.
command_info_t command_info;

static const struct prompt_result empty_result = {0}; // BUGBUG -- this appears to be a useless variable ... all it does is zero-initialize?

bool cmdline_validate_invariants_command_line(const command_line_history_t * history)
{
    BP_ASSERT(history != NULL);
    BP_ASSERT(history->write_offset < UI_CMDBUFFSIZE);
    BP_ASSERT(history->read_offset < UI_CMDBUFFSIZE);
    BP_ASSERT(history->which_history < UI_CMDBUFFSIZE);
    BP_ASSERT(history->cursor_offset < UI_CMDBUFFSIZE);
    return true;
}
bool cmdline_validate_invariants_command_pointer(const command_pointer_t * cp)
{
    BP_ASSERT(cp != NULL);
    BP_ASSERT(cp->wptr < UI_CMDBUFFSIZE);
    BP_ASSERT(cp->rptr < UI_CMDBUFFSIZE);
    return true;
}
bool cmdline_validate_invariants_command_info(const command_info_t * cmdinfo)
{
    BP_ASSERT(cmdinfo != NULL);
    BP_ASSERT(cmdinfo->rptr < UI_CMDBUFFSIZE);
    BP_ASSERT(cmdinfo->wptr < UI_CMDBUFFSIZE);
    BP_ASSERT(cmdinfo->startptr < UI_CMDBUFFSIZE);
    BP_ASSERT(cmdinfo->endptr < UI_CMDBUFFSIZE);
    BP_ASSERT(cmdinfo->nextptr < UI_CMDBUFFSIZE);
    BP_ASSERT(
        (cmdinfo->delimiter == ';') ||
        (cmdinfo->delimiter == '|') ||
        (cmdinfo->delimiter == '&') ||
        (cmdinfo->delimiter ==  0 )
    );
    return true;
}

void cmdln_init(void) {
    PRINT_DEBUG("cmdln_init()\r\n");
    memset(&cmdln, 0, sizeof(cmdln));
    memset(&command_info, 0, sizeof(command_info));
    // C11 supports generics!
    cmdline_validate_invariants(&cmdln);
    cmdline_validate_invariants(&command_info);
}
// buffer offset update, rolls over
uint32_t cmdln_pu(uint32_t i) {
    // Is there any **_other_** reason why UI_CMDBUFFSIZE is commented as "Must be a power of 2?"
    // If not, remove that restriction and simply (optimizer should result in equivalent code):
    return i % UI_CMDBUFFSIZE;
}
static uint32_t cmdln_available_chars(uint32_t rptr, uint32_t wptr) {
    // This is a circular buffer.
    static_assert( (2*UI_CMDBUFFSIZE) < UINT32_MAX, "UI_CMDBUFFSIZE too large for this shortcut" );
    // Just add UI_CMDBUFFSIZE to the write pointer before subtracting them.
    // This avoids the need for conditional addition, since the modulo operation
    // will remove this addition.
    uint32_t tmp = wptr + UI_CMDBUFFSIZE - rptr;
    return tmp % UI_CMDBUFFSIZE;
}

void cmdln_get_command_pointer(command_pointer_t* cp) {
    cmdline_validate_invariants(&cmdln);
    cp->wptr = cmdln.write_offset; // These are offsets, NOT pointers
    cp->rptr = cmdln.read_offset; // These are offsets, NOT pointers
    cmdline_validate_invariants(cp);
}

bool cmdln_try_add(char* c) {
    cmdline_validate_invariants(&cmdln);
    // TODO: leave one space for 0x00 command seperator????
    if (cmdln_pu(cmdln.write_offset + 1) == cmdln_pu(cmdln.read_offset)) {
        PRINT_WARNING("cmdln_try_add: Buffer full, could not add '%c'", *c);
        return false;
    }
    cmdln.buf[cmdln.write_offset] = (*c);
    cmdln.write_offset = cmdln_pu(cmdln.write_offset + 1);
    cmdline_validate_invariants(&cmdln);
    return true;
}

bool cmdln_try_remove(char* c) {
    cmdline_validate_invariants(&cmdln);
    uint32_t tmp_read_offset = cmdln_pu(cmdln.read_offset);
    uint32_t tmp_write_offset = cmdln_pu(cmdln.write_offset);
    if (cmdln_pu(cmdln.read_offset) == cmdln_pu(cmdln.write_offset)) {
        // this is not a warning, as it's a normal occurrence
        PRINT_DEBUG("cmdln_try_remove: Buffer empty, could not remove character");
        return false;
    }
    if (cmdln.buf[tmp_read_offset] == 0x00) {
        PRINT_DEBUG("cmdln_try_remove: Buffer offset 0x%02x (%d) stored null char, nothing to remove\n",
                    cmdln.read_offset, cmdln.read_offset);
        return false;
    }
    (*c) = cmdln.buf[tmp_read_offset];
    cmdln.read_offset = cmdln_pu(tmp_read_offset + 1);
    cmdline_validate_invariants(&cmdln);
    return true;
}

bool cmdln_try_peek(uint32_t i, char* c) {
    cmdline_validate_invariants(&cmdln);
    uint32_t tmp = cmdln_pu(cmdln.read_offset + i);
    if (tmp == cmdln_pu(cmdln.write_offset)) {
        PRINT_NEVER("cmdln_try_peek: Buffer offset 0x%02x (%d) is end of written buffer, no character to peek\n",
                    tmp, tmp);
        return false;
    }

    (*c) = cmdln.buf[tmp];
    if ((*c) == 0x00) {
        PRINT_NEVER("cmdln_try_peek: Buffer offset 0x%02x (%d) stored null char\n", tmp, tmp);
        return false;
    }

    return true;
}

bool cmdln_try_peek_pointer(command_pointer_t* cp, uint32_t i, char* c) {
    cmdline_validate_invariants(&cmdln);
    cmdline_validate_invariants(cp);

    uint32_t tmp = cmdln_pu(cp->rptr + i);
    if (tmp == cmdln_pu(cp->wptr)) {
        PRINT_DEBUG("cmdln_try_peek_pointer: Buffer offset 0x%02x (%d) is end of written buffer, no character to peek\n",
                    tmp, tmp);
        return false;
    }

    (*c) = cmdln.buf[cmdln_pu(cp->rptr + i)];
    if ((*c) == 0x00) {
        PRINT_DEBUG("cmdln_try_peek_pointer: Buffer offset 0x%02x (%d) stored null char\n", tmp, tmp);
        // BUGBUG -- Unlike cmdln_try_peek(), this does not return false if the character is 0x00?
        // return false;
    }
    return true;
}

bool cmdln_try_discard(uint32_t i) {

    cmdline_validate_invariants(&cmdln);

    // BUGBUG -- this will increment the read offset, even if the read offset
    //           says there's nothing to discard (i.e., rptr == wptr)

    uint32_t available_chars = cmdln_available_chars(cmdln.read_offset, cmdln.write_offset);
    
    // When this was indiscriminately going past the end of the buffer,
    // stuff was in undefined behavior land.
    // What to do when there are some available characters, but not
    // the count that was requested?
    //   Option 1: return false, without dropping any characters
    //   Option 2: drop the available characters, and return false
    // Went with option 2, because name of this function is `try_discard()`.
    bool result = available_chars >= i;

    uint32_t to_discard = result ? i : available_chars;
    if (to_discard != 0) {
        if (!result) {
            PRINT_WARNING("cmdln_try_discard: requested to discard %d characters, only %d discarded (no more remain)", i, to_discard);
        } else if (to_discard == 1) {
            char c = cmdln.buf[cmdln.read_offset];
            PRINT_DEBUG("cmdln_try_discard: discarding single character %c (0x%02x) characters", c, c);
        } else {
            PRINT_DEBUG(
                "cmdln_try_discard: discarding %d characters by adjusting cmdln.rptr from %d to %d",
                to_discard, cmdln.read_offset, cmdln_pu(cmdln.read_offset + to_discard)
            );
        }
        cmdln.read_offset = cmdln_pu(cmdln.read_offset + to_discard);
    }

    cmdline_validate_invariants(&cmdln);
    return result;
}

bool cmdln_next_buf_pos(void) {
    // TODO: This may be a good spot to rotate the command history buffer....
    cmdline_validate_invariants(&cmdln);
    cmdln.read_offset = cmdln.write_offset;
    cmdln.cursor_offset = cmdln.write_offset;
    cmdln.which_history = 0;
    cmdline_validate_invariants(&cmdln);
}

// These are new functions to ease argument and options parsing
//  Isolate the next command between the current read pointer and 0x00, (?test?) end of pointer, ; || && (next commmand)
//  functions to move within the single command range

// consume white space (0x20, space)
//  non_white_space = true, consume non-white space characters (not space)
static bool cmdln_consume_white_space_ex(uint32_t* rptr, bool non_white_space) {

    cmdline_validate_invariants(&cmdln);
    cmdline_validate_invariants(&command_info);


    // consume white space
    while (true) {
        char c;
        // all remaining characters matched ... no more characters remain
        if (!(
            command_info.endptr >= (command_info.startptr + (*rptr)) &&  // BUGBUG -- NOT pointers, and did NOT do modulo operation, so this will fail when start offset is near end of circular buffer
            cmdln_try_peek(command_info.startptr + (*rptr), &c)
            )) {
            return false;
        }
        bool matches =
               (!non_white_space && c == ' ')     // consume white space --OR--
            || ( non_white_space && c != ' ');    // consume non-whitespace
        if (!matches) {
            // found non-matching character ... so can stop consuming
            return true;
        }
        // consume this character and move to next
        (*rptr)++;
    }
}
bool cmdln_consume_white_space(uint32_t* rptr) {
    return cmdln_consume_white_space_ex(rptr, false);
}
bool cmdln_consume_non_white_space(uint32_t* rptr) {
    return cmdln_consume_white_space_ex(rptr, true);
}

// internal function to take copy string from start position to next space or end of buffer
// notice, we do not pass rptr by reference, so it is not updated
static bool cmdln_args_get_string_old(uint32_t rptr, uint32_t max_len, char* string) {

    cmdline_validate_invariants(&cmdln);
    cmdline_validate_invariants(&command_info);

    char c;
    for (uint32_t i = 0; i < max_len; i++) {
        // no more characters
        if ((!(command_info.endptr >= command_info.startptr + rptr &&
               cmdln_try_peek(command_info.startptr + rptr, &c))) ||
            c == ' ' || i == (max_len - 1)) {
            string[i] = 0x00;
            if (i == 0) {
                return false;
            } else {
                return true;
            }
        }
        string[i] = c;
        rptr++;
    }
}

static bool cmdln_args_get_string_new(uint32_t read_offset, uint32_t max_len, char* string) {
    cmdline_validate_invariants(&cmdln);
    cmdline_validate_invariants(&command_info);
    memset(string, 0x00, max_len); // clear the string buffer

    for (uint32_t i = 0; i < max_len; i++) {
        // BUGBUG -- The below check does not appear correct if
        //           the string being retrieved spans the end
        //           of the circular buffer.

        if (command_info.endptr < command_info.startptr + read_offset) {
            string[i] = 0x00; // reached end of current command
            return i != 0;
        }
        char c;
        if (!cmdln_try_peek(command_info.startptr + read_offset, &c)) {
            // how can this happen?
            PRINT_FATAL("Peek() failed in cmdln_args_get_string, read_offset=%d, startptr=%d, endptr=%d, i=%d",
                        read_offset, command_info.startptr, command_info.endptr, i);
            string[i] = 0x00;
            return i != 0;
        }
        if (c == ' ') {
            string[i] = 0x00;
            return i != 0;
        }
        // got a character ... make sure we can null-terminate the string
        if (i == max_len - 1) {
            memset(string, 0x00, max_len);
            return false;
        }
        string[i] = c;
        read_offset++;
    }
}

bool cmdln_args_get_string(uint32_t rptr, uint32_t max_len, char* string) {

    char * string_old = string;

    char string_new[max_len]; // GCC extension: VLA ... for testing purposes validating old vs. new code
    memset(string_new, 0x00, max_len);

    bool result_old = cmdln_args_get_string_old(rptr, max_len, string_old);
    bool result_new = cmdln_args_get_string_new(rptr, max_len, string_new);

    bool result_is_same = (result_old == result_new);
    if (!result_is_same) {
        PRINT_WARNING("cmdln_args_get_string: (%d) Old and new code returned different results: %d vs %d", rptr, result_old, result_new);
    }
    size_t strlen_old = strnlen(string_old, max_len);
    size_t strlen_new = strnlen(string_new, max_len);
    size_t strlen_min = (strlen_old < strlen_new) ? strlen_old : strlen_new;
    size_t strlen_max = (strlen_old < strlen_new) ? strlen_new : strlen_old;
    if (strlen_old != strlen_new) {
        PRINT_WARNING("cmdln_args_get_string: (%d) Old and new code returned different string lengths: %d vs %d ('%s' vs '%s')", rptr, (int)strlen_old, (int)strlen_new, string_old, string_new);
    } else if (memcmp(string_old, string_new, strlen_old) != 0) {
        PRINT_WARNING("cmdln_args_get_string: (%d) Old and new code returned different strings: '%s' vs '%s'", rptr, string_old, string_new);
    }
    return result_old;
}


// TODO: add support for C23-style numeric literal separator '
//       e.g., 0x0001'3054   0b1101'1011'0111'1111   24'444'444.848'22

// parse a hex value from the first digit
// notice, we do not pass rptr by reference, so it is not updated
bool cmdln_args_get_hex(uint32_t* rptr, struct prompt_result* result, uint32_t* value) {

    cmdline_validate_invariants(&cmdln);
    cmdline_validate_invariants(&command_info);

    char c;

    //*result=empty_result;
    result->no_value = true;
    (*value) = 0;

    while (command_info.endptr >= command_info.startptr + (*rptr) &&
           cmdln_try_peek(command_info.startptr + (*rptr), &c)) { // peek at next char
        if (((c >= '0') && (c <= '9'))) {
            (*value) <<= 4;
            (*value) += (c - 0x30);
        } else if (((c | 0x20) >= 'a') && ((c | 0x20) <= 'f')) {
            (*value) <<= 4;
            c |= 0x20;              // to lowercase
            (*value) += (c - 0x57); // 0x61 ('a') -0xa
        } else {
            break;
        }
        (*rptr)++;
        result->success = true;
        result->no_value = false;
    }
    return result->success;
}

// parse a bin value from the first digit
// notice, we do not pass rptr by reference, so it is not updated
bool cmdln_args_get_bin(uint32_t* rptr, struct prompt_result* result, uint32_t* value) {

    cmdline_validate_invariants(&cmdln);
    cmdline_validate_invariants(&command_info);

    char c;
    //*result=empty_result;
    result->no_value = true;
    (*value) = 0;

    while (command_info.endptr >= command_info.startptr + (*rptr) &&
           cmdln_try_peek(command_info.startptr + (*rptr), &c)) {
        if ((c < '0') || (c > '1')) {
            break;
        }
        (*value) <<= 1;
        (*value) += c - 0x30;
        (*rptr)++;
        result->success = true;
        result->no_value = false;
    }

    return result->success;
}

// parse a decimal value from the first digit
// notice, we do not pass rptr by reference, so it is not updated
bool cmdln_args_get_dec(uint32_t* rptr, struct prompt_result* result, uint32_t* value) {

    cmdline_validate_invariants(&cmdln);
    cmdline_validate_invariants(&command_info);

    char c;
    //*result=empty_result;
    result->no_value = true;
    (*value) = 0;

    while (command_info.endptr >= command_info.startptr + (*rptr) &&
           cmdln_try_peek(command_info.startptr + (*rptr), &c)) // peek at next char
    {
        if ((c < '0') || (c > '9')) // if there is a char, and it is in range
        {
            break;
        }
        (*value) *= 10;
        (*value) += (c - 0x30);
        (*rptr)++;
        result->success = true;
        result->no_value = false;
    }
    return result->success;
}

// decodes value from the cmdline
// XXXXXX integer
// 0xXXXX hexadecimal
// 0bXXXX bin
bool cmdln_args_get_int(uint32_t* rptr, struct prompt_result* result, uint32_t* value) {

    cmdline_validate_invariants(&cmdln);
    cmdline_validate_invariants(&command_info);

    bool r1, r2;
    char p1, p2;

    *result = empty_result;
    r1 = cmdln_try_peek(command_info.startptr + (*rptr), &p1);
    r2 = cmdln_try_peek(command_info.startptr + (*rptr) + 1, &p2);
    if (!r1 || (p1 == 0x00)) { // no data, end of data, or no value entered on prompt
        result->no_value = true;
        return false;
    }

    if (r2 && (p2 | 0x20) == 'x') { // HEX
        (*rptr) += 2;
        cmdln_args_get_hex(rptr, result, value);
        result->number_format = df_hex;    // whatever from ui_const
    } else if (r2 && (p2 | 0x20) == 'b') { // BIN
        (*rptr) += 2;
        cmdln_args_get_bin(rptr, result, value);
        result->number_format = df_bin; // whatever from ui_const
    } else {                            // DEC
        cmdln_args_get_dec(rptr, result, value);
        result->number_format = df_dec; // whatever from ui_const
    }
    return result->success;
}

bool cmdln_args_find_flag_internal(char flag, command_var_t* arg) {

    cmdline_validate_invariants(&cmdln);
    cmdline_validate_invariants(&command_info);

    uint32_t rptr = 0;
    char flag_c;
    char dash_c;
    arg->error = false;
    arg->has_arg = false;
    while (command_info.endptr >= (command_info.startptr + rptr + 1) && cmdln_try_peek(rptr, &dash_c) &&
           cmdln_try_peek(rptr + 1, &flag_c)) {
        if (dash_c == '-' && flag_c == flag) {
            arg->has_arg = true;
            if ((!cmdln_consume_non_white_space(&rptr))     // move past the flag characters
                || (!cmdln_consume_white_space(&rptr)) // move past spaces. @end of buffer, next flag, or value
                || (cmdln_try_peek(rptr, &dash_c) && dash_c == '-')) // next argument, no value
            {
                PRINT_NEVER("cmdln_args_find_flag_internal: Flag %c found, no value\r\n", flag);
                arg->has_value = false;
                return true;
            }
            PRINT_NEVER("cmdln_args_find_flag_internal: Flag %c found, value offset %d\r\n", flag, rptr);
            arg->has_value = true;
            arg->value_pos = rptr;
            return true;
        }
        rptr++;
    }
    PRINT_NEVER("cmdln_args_find_flag_internal: Flag %c not found\r\n", flag);
    return false;
}

// check if a flag is present and get the integer value
//  returns true if flag is present AND has a string value
//  use cmdln_args_find_flag to see if a flag was present with no string value
bool cmdln_args_find_flag_uint32(char flag, command_var_t* arg, uint32_t* value) {

    cmdline_validate_invariants(&cmdln);
    cmdline_validate_invariants(&command_info);

    if (!cmdln_args_find_flag_internal(flag, arg)) {
        return false;
    }

    if (!arg->has_value) {
        return false;
    }

    struct prompt_result result;
    if (!cmdln_args_get_int(&arg->value_pos, &result, value)) {
        arg->error = true;
        return false;
    }

    return true;
}

// check if a flag is present and get the integer value
//  returns true if flag is present AND has a integer value
//  use cmdln_args_find_flag to see if a flag was present with no integer value
bool cmdln_args_find_flag_string(char flag, command_var_t* arg, uint32_t max_len, char* str) {

    cmdline_validate_invariants(&cmdln);
    cmdline_validate_invariants(&command_info);

    if (!cmdln_args_find_flag_internal(flag, arg)) {
        return false;
    }

    if (!arg->has_value) {
        return false;
    }

    if (!cmdln_args_get_string(arg->value_pos, max_len, str)) {
        arg->error = true;
        return false;
    }

    return true;
}

// check if a -f(lag) is present. Value is don't care.
// returns true if flag is present
bool cmdln_args_find_flag(char flag) {

    cmdline_validate_invariants(&cmdln);
    cmdline_validate_invariants(&command_info);

    command_var_t arg;
    if (!cmdln_args_find_flag_internal(flag, &arg)) {
        return false;
    }
    return true;
}

// NOTE: pos is the white-space based token from the current command (not entire command line)
bool cmdln_args_string_by_position(uint32_t pos, uint32_t max_len, char* str) {

    cmdline_validate_invariants(&cmdln);
    cmdline_validate_invariants(&command_info);

    char c;
    uint32_t rptr = 0;
    PRINT_NEVER("cmdln_args_string_by_position(%d, %d)\r\n", pos, max_len);
    memset(str, 0x00, max_len);
    // start at beginning of command range
    for (uint32_t i = 0; i < pos + 1; i++) {
        // consume white space
        if (!cmdln_consume_white_space(&rptr)) {
            return false;
        }
        // consume non-white space
        if (i != pos) {
            if (!cmdln_consume_non_white_space(&rptr)) { // consume non-white space
                return false;
            }
        } else {
            cmdln_try_peek(command_info.startptr + (rptr), &c);
            //see if this is a argument or a flag, reject flags
            if (c=='-') {
                return false;
            }
            struct prompt_result result;
            if (cmdln_args_get_string(rptr, max_len, str)) {
                return true;
            } else {
                return false;
            }
        }
    }
    return false;
}

bool cmdln_args_uint32_by_position(uint32_t pos, uint32_t* value) {

    cmdline_validate_invariants(&cmdln);
    cmdline_validate_invariants(&command_info);

    char c;
    uint32_t rptr = 0;
    // start at beginning of command range
    PRINT_NEVER("cmdln_args_uint32_by_position(%d)\r\n", pos);
    for (uint32_t i = 0; i < pos + 1; i++) {
        // consume white space
        if (!cmdln_consume_white_space(&rptr)) {
            return false;
        }
        // consume non-white space
        if (i != pos) {
            if (!cmdln_consume_non_white_space(&rptr)) { // consume non-white space
                return false;
            }
        } else {
            struct prompt_result result;
            if (cmdln_args_get_int(&rptr, &result, value)) {
                return true;
            } else {
                return false;
            }
        }
    }
    return false;
}

bool cmdln_args_float_by_position(uint32_t pos, float* value) {

    cmdline_validate_invariants(&cmdln);
    cmdline_validate_invariants(&command_info);

    char c;
    uint32_t rptr = 0;
    uint32_t ipart = 0, dpart = 0;
    // start at beginning of command range
    PRINT_NEVER("cmdln_args_float_by_position(%d)\r\n", pos);
    for (uint32_t i = 0; i < pos + 1; i++) {
        // consume white space
        if (!cmdln_consume_white_space(&rptr)) {
            return false;
        }
        // consume non-white space
        if (i != pos) {
            if (!cmdln_consume_non_white_space(&rptr)) { // consume non-white space
                return false;
            }
        } else {
            // before decimal
            if (!cmdln_try_peek(rptr, &c)) {
                return false;
            }
            if ((c >= '0') && (c <= '9')) // first part of decimal
            {
                struct prompt_result result;
                if (!cmdln_args_get_int(&rptr, &result, &ipart)) {
                    return false;
                }
                // printf("ipart: %d\r\n", ipart);
            }

            uint32_t dpart_len = 0;
            if (cmdln_try_peek(rptr, &c)) {
                if (c == '.' || c == ',') { // second part of decimal
                    rptr++;                 // discard
                    dpart_len = rptr;       // track digits
                    struct prompt_result result;
                    if (!cmdln_args_get_int(&rptr, &result, &dpart)) {
                        // printf("No decimal part found\r\n");
                    }
                    dpart_len = rptr - dpart_len;
                    // printf("dpart: %d, dpart_len: %d\r\n", dpart, dpart_len);
                }
            }
            (*value) = (float)ipart;
            (*value) += ((float)dpart / (float)pow(10, dpart_len));
            // printf("value: %f\r\n", *value);
            return true;
        }
    }
    return false;
}

// finds the next command in current line of user input
// could be the comment indicator `#` ... in which case entire string ending in 0x00 is considered the comment
// could be a single command ending in 0x00
// could be multiple commands chained with ; || &&
// sets the internal current command pointers to avoid reading into the next or previous commands
// returns true if a command is found
bool cmdln_find_next_command(struct _command_info_t* cp) {

    cmdline_validate_invariants(&cmdln);
    cmdline_validate_invariants(&command_info);
    cmdline_validate_invariants(cp);

    uint32_t i = 0;
    char c;
    char d;
    bool only_saw_leading_whitespace = true;
    cp->startptr = cp->nextptr; // should be zero on first call, use previous value for subsequent calls
    cp->endptr   = cp->nextptr; // should be zero on first call, use previous value for subsequent calls

    PRINT_NEVER("cmdln_find_next_command: startptr=%d\r\n", cp->startptr);

    if (!cmdln_try_peek(cp->endptr, &c)) {
        PRINT_NEVER("cmdln_find_next_command: No command found, endptr=%d\r\n", cp->endptr);
        cp->delimiter = false; // 0 = end of command input
        return false;
    }
    memset(cp->command, 0x00, 9); // BUGBUG -- Hardcoded size of buffer cp->command, should hard-code somewhere a MAXIMUM_COMMAND_IDENTIFIER_CHARACTERS

    // Generally, this loop will (try to) peek the next character into `c`.
    //   If the character is a space, the loop continues.
    //   If the first non-space character is `#`, the remainder of the input line is a comment.
    // Then, to support detection of chained commands using `;`, `||`, or `&&`, will (try to) peek another character into `d`.
    // 
    // If the first character was not retrieved: (SUCCESS)
    //     There is no delimiter, so this is the final command.
    // Else (first character is valid)...
    //     If first character is `;`: (SUCCESS)
    //         The delimiter is set to `;` to indicate more commands might follow.
    //     If second character retrieved and the two characters are `||` or `&&`: (SUCCESS)
    //         The delimiter is set to `|` or `&` to indicate more commands might follow.
    //     The first eight non-space characters are copied into `cp->command`.

    while (true) { // TODO: should limit loop to maximum input buffer length, to catch infinite loop bug that otherwise would be frustratingly hard to debug.

        bool got_pos1 = cmdln_try_peek(cp->endptr, &c);
        
        // consume white space and detect if the line is a comment
        if(got_pos1 && only_saw_leading_whitespace) {
            // consume leading white space only (not any other whitespace)
            if (c == ' ') {
                cp->endptr++;
                continue;
            }
            // If first non-space character is `#`, then the rest of the line is a comment
            if (c == '#') { // first non-space character is `#`, so rest of the line is a comment
                // loop until end of command line input
                while (cmdln_try_peek(cp->endptr, &d)) {
                    cp->endptr++;
                }
                cp->command[0] = '#';
                cp->delimiter = 0; // no continuation by another command for this line
                cp->nextptr = cp->endptr;
                goto cmdln_find_next_command_success;
            } else {
                // any character other than space or `#` means this is not a comment
                only_saw_leading_whitespace = false;
            }
        }
        
        bool got_pos2 = cmdln_try_peek(cp->endptr + 1, &d);
        if (!got_pos1) {
            PRINT_NEVER("cmdln_find_next_command: Found last/only command with end offset: %d\r\n", cp->endptr);
            cp->delimiter = false; // 0 = end of command input
            cp->nextptr = cp->endptr;
            goto cmdln_find_next_command_success;
        } else if (c == ';') {
            PRINT_NEVER("cmdln_find_next_command: Found ';' end offset: %d\r\n", cp->endptr);
            cp->delimiter = ';';
            cp->nextptr = cp->endptr + 1;
            goto cmdln_find_next_command_success;
        } else if (got_pos2 && ((c == '|' && d == '|') || (c == '&' && d == '&'))) {
            PRINT_NEVER("cmdln_find_next_command: Found '%c%c' end offset: %d\r\n", c, d, cp->endptr);
            cp->delimiter = c;
            cp->nextptr = cp->endptr + 2;
            goto cmdln_find_next_command_success;
        } else if (i < 8) { // BUGBUG -- Hardcoded size of buffer cp->command, should hard-code somewhere a MAXIMUM_COMMAND_IDENTIFIER_CHARACTERS
            cp->command[i] = c;
            if (c == ' ') { // BUGBUG -- Could this have previously EVER been true?  Didn't the "eat whitespace" logic above ensure that a space character was never seen here?
                i = 8; // stop at space if possible // BUGBUG -- Hardcoded (likely size of buffer cp->command), should hard-code somewhere a MAXIMUM_COMMAND_IDENTIFIER_CHARACTERS
            }
            i++;
        }
        cp->endptr++;
    }

cmdln_find_next_command_success:
    // NOTE: startptr and endptr are OFFSETS into the command line buffer.
    //       Moreover, they are automagically WRAPPED around the circular
    //       buffer in cmdln_try_peek(), cmdln_try_peek_pointer(), etc.
    //       using `cmdln_pu(uint32_t offset)`
    PRINT_NEVER(
        "cmdln_find_next_command: offset %d .. %d (%d chars), cmd `%s`\n",
        cp->startptr, cp->endptr, cp->endptr - cp->startptr, cp->command
        );

    cp->endptr--;
    command_info.startptr = cp->startptr;
    command_info.endptr = cp->endptr;
    return true;
}

// function for debugging the command line arguments parsers
//  shows all commands and all detected positions
bool cmdln_info(void) {

    cmdline_validate_invariants(&cmdln);
    cmdline_validate_invariants(&command_info);

    PRINT_DEBUG("cmdln_info(): start: %d  end: %d  length: %d", cmdln.read_offset, cmdln.write_offset, cmdln_available_chars(cmdln.read_offset, cmdln.write_offset));
    // start and end point?
    printf("Input start: %d, end %d\r\n", cmdln.read_offset, cmdln.write_offset);
    // how many characters?
    printf("Input length: %d\r\n", cmdln_available_chars(cmdln.read_offset, cmdln.write_offset));

    // loop through and display all commands in this command line
    uint32_t i = 0;
    command_info_t cp;
    cp.nextptr = 0;
    while (cmdln_find_next_command(&cp)) {
        if (cp.delimiter == 0) {
            PRINT_DEBUG("cmdln_info:        Command %s <end of commands>\n", cp.command);
        } else {
            PRINT_DEBUG("cmdln_info:        Command %s, delimiter %c\n", cp.command, cp.delimiter);
        }
        printf("Command: %s, delimiter: %c\r\n", cp.command, cp.delimiter);

        // show all the arguments for this command
        uint32_t pos = 0;
        char str[9];
        // BUGBUG ... this is O(n^2) complexity, because cmdln_arg_string_by_position()
        //            iterates through the command line from the start each time.
        //            May be able to ignore this, as maximum buffer is 512 bytes, and
        //            512*512 = 262144 iterations, greatly limiting impact.
        while (cmdln_args_string_by_position(pos, 9, str)) { // BUGBUG -- hardcoded, based on size of local array `str`
            PRINT_DEBUG("cmdln_info:            Pos %d, value: %s\n", pos, str);
            printf("String pos: %d, value: %s\r\n", pos, str);
            pos++;
        }
    }
}

// function for debugging the command line arguments parsers
//  shows all integers and all detected positions
bool cmdln_info_uint32(void) {

    cmdline_validate_invariants(&cmdln);
    cmdline_validate_invariants(&command_info);

    PRINT_DEBUG("cmdln_info_uint32(): start: %d  end: %d  length: %d", cmdln.read_offset, cmdln.write_offset, cmdln_available_chars(cmdln.read_offset, cmdln.write_offset));
    // start and end point?
    printf("Input start: %d, end %d\r\n", cmdln.read_offset, cmdln.write_offset);
    // how many characters?
    printf("Input length: %d\r\n", cmdln_available_chars(cmdln.read_offset, cmdln.write_offset));
    uint32_t i = 0;
    command_info_t cp;
    cp.nextptr = 0;
    while (cmdln_find_next_command(&cp)) {
        if (cp.delimiter == 0) {
            PRINT_DEBUG("cmdln_info_uint32:        Command %s <end of commands>\n", cp.command);
        } else {
            PRINT_DEBUG("cmdln_info_uint32:        Command %s, delimiter %c\n", cp.command, cp.delimiter);
        }
        printf("Command: %s, delimiter: %c\r\n", cp.command, cp.delimiter);
        uint32_t pos = 0;
        uint32_t value = 0;
        while (cmdln_args_uint32_by_position(pos, &value)) {
            PRINT_DEBUG("cmdln_info_uint32:            Pos %d, value: %d\n", pos, value);
            printf("Integer pos: %d, value: %d\r\n", pos, value);
            pos++;
        }
    }
}
