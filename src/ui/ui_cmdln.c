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
struct _command_line cmdln;

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
struct _command_info_t command_info;

static const struct prompt_result empty_result = {0}; // BUGBUG -- this appears to be a useless variable ... all it does is zero-initialize?

void cmdln_init(void) {
    PRINT_DEBUG("cmdln_init()\r\n");

    for (uint32_t i = 0; i < UI_CMDBUFFSIZE; i++) {
        cmdln.buf[i] = 0x00;
    }
    cmdln.wptr = 0;
    cmdln.rptr = 0;
    cmdln.histptr = 0;
    cmdln.cursptr = 0;
}
// buffer offset update, rolls over
uint32_t cmdln_pu(uint32_t i) {
    // Is there any **_other_** reason why UI_CMDBUFFSIZE is commented as "Must be a power of 2?"
    // If not, remove that restriction and simply (optimizer should result in equivalent code):
    // return i % UI_CMDBUFFSIZE;
    return ((i) & (UI_CMDBUFFSIZE - 1));
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

void cmdln_get_command_pointer(struct _command_pointer* cp) {
    cp->wptr = cmdln.wptr; // These are offsets, NOT pointers
    cp->rptr = cmdln.rptr; // These are offsets, NOT pointers
}

bool cmdln_try_add(char* c) {
    // TODO: leave one space for 0x00 command seperator????
    if (cmdln_pu(cmdln.wptr + 1) == cmdln_pu(cmdln.rptr)) {
        PRINT_WARNING("cmdln_try_add: Buffer full, could not add '%c'", *c);
        return false;
    }
    cmdln.buf[cmdln.wptr] = (*c);
    cmdln.wptr = cmdln_pu(cmdln.wptr + 1);
    return true;
}

bool cmdln_try_remove(char* c) {
    uint32_t tmp_read_offset = cmdln_pu(cmdln.rptr);
    uint32_t tmp_write_offset = cmdln_pu(cmdln.wptr);
    if (cmdln_pu(cmdln.rptr) == cmdln_pu(cmdln.wptr)) {
        // this is not a warning, as it's a normal occurrence
        PRINT_DEBUG("cmdln_try_remove: Buffer empty, could not remove character");
        return false;
    }
    if (cmdln.buf[tmp_read_offset] == 0x00) {
        PRINT_DEBUG("cmdln_try_remove: Buffer offset 0x%02x (%d) stored null char, nothing to remove\n",
                    cmdln.rptr, cmdln.rptr);
        return false;
    }
    (*c) = cmdln.buf[tmp_read_offset];
    cmdln.rptr = cmdln_pu(tmp_read_offset + 1);
    return true;
}

bool cmdln_try_peek(uint32_t i, char* c) {
    uint32_t tmp = cmdln_pu(cmdln.rptr + i);
    if (tmp == cmdln_pu(cmdln.wptr)) {
        PRINT_DEBUG("cmdln_try_peek: Buffer offset 0x%02x (%d) is end of written buffer, no character to peek\n",
                    tmp, tmp);
        return false;
    }

    (*c) = cmdln.buf[tmp];
    if ((*c) == 0x00) {
        PRINT_DEBUG("cmdln_try_peek: Buffer offset 0x%02x (%d) stored null char\n", tmp, tmp);
        return false;
    }

    return true;
}

bool cmdln_try_peek_pointer(struct _command_pointer* cp, uint32_t i, char* c) {
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

    // BUGBUG -- this will increment the read offset, even if the read offset
    //           says there's nothing to discard (i.e., rptr == wptr)

    uint32_t available_chars = cmdln_available_chars(cmdln.rptr, cmdln.wptr);
    
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
            char c = cmdln.buf[cmdln.rptr];
            PRINT_DEBUG("cmdln_try_discard: discarding single character %c (0x%02x) characters", c, c);
        } else {
            PRINT_DEBUG(
                "cmdln_try_discard: discarding %d characters by adjusting cmdln.rptr from %d to %d",
                to_discard, cmdln.rptr, cmdln_pu(cmdln.rptr + to_discard)
            );
        }
        cmdln.rptr = cmdln_pu(cmdln.rptr + to_discard);
    }
    return result;
}

bool cmdln_next_buf_pos(void) {
    cmdln.rptr = cmdln.wptr;
    cmdln.cursptr = cmdln.wptr;
    cmdln.histptr = 0;
}

// These are new functions to ease argument and options parsing
//  Isolate the next command between the current read pointer and 0x00, (?test?) end of pointer, ; || && (next commmand)
//  functions to move within the single command range

// consume white space (0x20, space)
//  non_white_space = true, consume non-white space characters (not space)
static bool cmdln_consume_white_space_ex(uint32_t* rptr, bool non_white_space) {
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
bool cmdln_args_get_string(uint32_t rptr, uint32_t max_len, char* string) {
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

// TODO: add support for C23-style numeric literal separator '
//       e.g., 0x0001'3054   0b1101'1011'0111'1111   24'444'444.848'22

// parse a hex value from the first digit
// notice, we do not pass rptr by reference, so it is not updated
bool cmdln_args_get_hex(uint32_t* rptr, struct prompt_result* result, uint32_t* value) {
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
                // printf("No value for flag %c\r\n", flag);
                arg->has_value = false;
                return true;
            }
            // printf("Value for flag %c\r\n", flag);
            arg->has_value = true;
            arg->value_pos = rptr;
            return true;
        }
        rptr++;
    }
    // printf("Flag %c not found\r\n", flag);
    return false;
}

// check if a flag is present and get the integer value
//  returns true if flag is present AND has a string value
//  use cmdln_args_find_flag to see if a flag was present with no string value
bool cmdln_args_find_flag_uint32(char flag, command_var_t* arg, uint32_t* value) {
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
    command_var_t arg;
    if (!cmdln_args_find_flag_internal(flag, &arg)) {
        return false;
    }
    return true;
}

// NOTE: pos is the white-space based token from the current command (not entire command line)
bool cmdln_args_string_by_position(uint32_t pos, uint32_t max_len, char* str) {
    char c;
    uint32_t rptr = 0;
    PRINT_DEBUG("cmdln_args_string_by_position(%d, %d, %p)\r\n", pos, max_len, str);
    memset(str, 0x00, max_len);
// start at beginning of command range
#ifdef UI_CMDLN_ARGS_DEBUG
    printf("Looking for string in pos %d\r\n", pos);
#endif
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
    char c;
    uint32_t rptr = 0;
// start at beginning of command range
    PRINT_DEBUG("cmdln_args_uint_by_position(%d, %p)\r\n", pos, value);
#ifdef UI_CMDLN_ARGS_DEBUG
    printf("Looking for uint in pos %d\r\n", pos);
#endif
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
    char c;
    uint32_t rptr = 0;
    uint32_t ipart = 0, dpart = 0;
// start at beginning of command range
    PRINT_DEBUG("cmdln_args_float_by_position(%d, %p)\r\n", pos, value);
#ifdef UI_CMDLN_ARGS_DEBUG
    printf("Looking for float in pos %d\r\n", pos);
#endif
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

// finds the next command in user input
// could be the comment indicator `#` ... in which case entire string ending in 0x00 is considered the comment
// could be a single command ending in 0x00
// could be multiple commands chained with ; || &&
// sets the internal current command pointers to avoid reading into the next or previous commands
// returns true if a command is found
bool cmdln_find_next_command(struct _command_info_t* cp) {
    uint32_t i = 0;
    char c;
    char d;
    bool only_saw_leading_whitespace = true;
    cp->startptr = cp->nextptr; // should be zero on first call, use previous value for subsequent calls
    cp->endptr   = cp->nextptr; // should be zero on first call, use previous value for subsequent calls

    PRINT_DEBUG("cmdln_find_next_command: startptr=%d\r\n", cp->startptr);

    if (!cmdln_try_peek(cp->endptr, &c)) {
        PRINT_DEBUG("cmdln_find_next_command: No command found, endptr=%d\r\n", cp->endptr);
#ifdef UI_CMDLN_ARGS_DEBUG
        printf("End of command line input at %d\r\n", cp->endptr);
#endif
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
            PRINT_DEBUG("cmdln_find_next_command: Found last/only command with end offset: %d\r\n", cp->endptr);
#ifdef UI_CMDLN_ARGS_DEBUG
            printf("Last/only command line input: pos1=%d, c=%d\r\n", got_pos1, c);
#endif
            cp->delimiter = false; // 0 = end of command input
            cp->nextptr = cp->endptr;
            goto cmdln_find_next_command_success;
        } else if (c == ';') {
            PRINT_DEBUG("cmdln_find_next_command: Found ';' end offset: %d\r\n", cp->endptr);
#ifdef UI_CMDLN_ARGS_DEBUG
            printf("Got end of command: ; position: %d, \r\n", cp->endptr);
#endif
            cp->delimiter = ';';
            cp->nextptr = cp->endptr + 1;
            goto cmdln_find_next_command_success;
        } else if (got_pos2 && ((c == '|' && d == '|') || (c == '&' && d == '&'))) {
            PRINT_DEBUG("cmdln_find_next_command: Found '%c%c' end offset: %d\r\n", c, d, cp->endptr);
#ifdef UI_CMDLN_ARGS_DEBUG
            printf("Got end of command: %c position: %d, \r\n", c, cp->endptr);
#endif
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
    PRINT_DEBUG(
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

    PRINT_DEBUG("cmdln_info(): start: %d  end: %d  length: %d", cmdln.rptr, cmdln.wptr, cmdln_available_chars(cmdln.rptr, cmdln.wptr));
    // start and end point?
    printf("Input start: %d, end %d\r\n", cmdln.rptr, cmdln.wptr);
    // how many characters?
    printf("Input length: %d\r\n", cmdln_available_chars(cmdln.rptr, cmdln.wptr));

    // loop through and display all commands in this command line
    uint32_t i = 0;
    struct _command_info_t cp;
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
    PRINT_DEBUG("cmdln_info_uint32(): start: %d  end: %d  length: %d", cmdln.rptr, cmdln.wptr, cmdln_available_chars(cmdln.rptr, cmdln.wptr));
    // start and end point?
    printf("Input start: %d, end %d\r\n", cmdln.rptr, cmdln.wptr);
    // how many characters?
    printf("Input length: %d\r\n", cmdln_available_chars(cmdln.rptr, cmdln.wptr));
    uint32_t i = 0;
    struct _command_info_t cp;
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
