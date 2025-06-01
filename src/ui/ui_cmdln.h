#pragma once

// Although named `command_line`, this structure
// covers the full history of the command line input.
// so it may have multiple command lines stored within.
// When read and write offsets are equal, the buffer is empty.
// Read and write offsets should ALWAYS be between 0 and UI_CMDBUFFSIZE-1.
typedef struct _command_line_history_t {
    uint32_t write_offset;    // Offset into command_line_history_t.buf
    uint32_t read_offset;     // Offset into command_line_history_t.buf // This will eventually go away, when current command line is always at zero
    uint32_t which_history;   // 0: History not used, 1..N: previous command that was copied into current command line
    uint32_t cursor_offset;   // Offset into command_line_history_t.buf where the cursor is currently positioned
    char buf[UI_CMDBUFFSIZE]; // Global circular buffer used to store command line input and history
} command_line_history_t;
extern command_line_history_t cmdln;

// This structure is used to track a single "command" and all options associated with that command.
// At present, multiple commands can be entered in a single command line,
// and this structure will define the offsets to a single one of those commands.

// TODO: document the fields in this structure
// WARNING: the end offset might EXCEED UI_CMDBUFFSIZE?!  Change to be a character count instead of end offset?
// WARNING: Some fields are modulo'd by UI_CMDBUFFSIZE, others are NOT.  This is just asking for bugs....
typedef struct _command_info_t {
    // Maybe include a pointer to the `command_line_history_t` that this refers to?
    // Since this structure has no meaning without  that context...
    command_line_history_t * history; // pointer to the command line history structure for this command_info_t
    uint32_t rptr;     // NOT a pointer .. this is an offset into command history .buf (a circular buffer)
    uint32_t wptr;     // NOT a pointer .. this is an offset into command history .buf (a circular buffer)
    uint32_t startptr; // NOT a pointer .. this is an offset into command history .buf (a circular buffer)
    uint32_t endptr;   // NOT a pointer .. this is an offset into command history .buf (a circular buffer)
    uint32_t nextptr;  // NOT a pointer .. this is an offset into command history .buf (a circular buffer)
    char delimiter;    // Is this command part of a chain?  e.g., `;`, `||`, '&&` -- 0 means no, else stores a single-char version of the delimiter
    char command[9];   // BUGBUG -- Hard-coded buffer size ... should this be MAX_COMMAND_LENGTH?
} command_info_t;

// TODO: rename this structure to avoid confusion between pointers / offsets ... maybe `command_extent_t` or `single_command_t`?

// This is a short-lived structure that will become invalid anytime the history is modified.
// TODO: add way to assert that the history matches ... maybe a simple edit counter in the command_line_history_t struct,
//       which is copied into this structure when it is initialized?
//       then, can assert that the edit counters match before relying on the data.
//       this will catch really tricky bugs much more rapidly.
typedef struct _command_pointer_t {
    command_line_history_t const * history; // can this be a pointer to a const?
    uint32_t rptr; // Initialized to history->read_offset;  Use as: history[buf + this->read_offset]
    uint32_t wptr; // Initialized to history->read_offset;  Use as: history[buf + this->read_offset]
} command_pointer_t;



void cmdln_init(command_line_history_t* out_cmdln);
void cmdln_init_command_info(command_line_history_t* command_line_history, command_info_t* out_cmdinfo);
void cmdln_init_command_pointer(command_line_history_t* command_line_history, command_pointer_t* out_cmdinfo);

bool cmdline_validate_invariants_command_line_history(const command_line_history_t * cmdline);
bool cmdline_validate_invariants_command_pointer(const command_pointer_t * cp);
bool cmdline_validate_invariants_command_info(const command_info_t * cmdinfo);

// Yes, this is syntactic sugar.
#define cmdline_validate_invariants(X) \
    _Generic((X),                  \
        command_pointer_t *             : cmdline_validate_invariants_command_pointer,      \
        const command_pointer_t *       : cmdline_validate_invariants_command_pointer,      \
        command_line_history_t *        : cmdline_validate_invariants_command_line_history, \
        const command_line_history_t *  : cmdline_validate_invariants_command_line_history, \
        command_info_t *                : cmdline_validate_invariants_command_info,         \
        const command_info_t *          : cmdline_validate_invariants_command_info          \
        )(X)



typedef struct command_var_struct {
    // clang-format off
    bool     has_arg;
    bool     has_value;
    uint32_t value_pos;
    bool     error;
    uint8_t  number_format;
    // clang-format on
} command_var_t;

////////////////////////////////////////////////////////////////////////////////////////
// TODO: update to use these new versions of the functions
////////////////////////////////////////////////////////////////////////////////////////
bool cmdln_args_find_flag_ex(command_info_t* cp, char flag);
bool cmdln_args_find_flag_uint32_ex(command_info_t* cp, char flag, command_var_t* arg, uint32_t* value);
bool cmdln_args_find_flag_string_ex(command_info_t* cp, char flag, command_var_t* arg, uint32_t max_len, char* str);
bool cmdln_args_float_by_position_ex(command_info_t* cp, uint32_t pos, float* value);
bool cmdln_args_uint32_by_position_ex(command_info_t* cp, uint32_t pos, uint32_t* value);
bool cmdln_args_string_by_position_ex(command_info_t* cp, uint32_t pos, uint32_t max_len, char* str);

bool cmdln_try_add_ex(command_line_history_t * command_line_history, char* c);
bool cmdln_try_remove_ex(command_line_history_t * command_line_history, char* c);

// try to peek 0+n bytes (no offsets updated)
// returns false if at the end of the buffer
// this should always be used sequentially from zero,
// if wanting to peek multiple characters forward
//     e.g., bool got_char = peek(0, &c) && peek(1, &c);
// to avoid missing the end of the buffer
bool cmdln_try_peek_ex(command_line_history_t const * command_line_history, uint32_t i, char* c);
bool cmdln_try_discard_ex(command_line_history_t* command_history_buffer, uint32_t i);
bool cmdln_next_buf_pos_ex(command_line_history_t* command_history_buffer);

// peek at offset from a specific command_pointer_t
bool cmdln_try_peek_pointer(command_pointer_t* cp, uint32_t i, char* c);

// Debug function ... dump parsing of current line of input to debugger
bool cmdln_info_ex(command_line_history_t* command_history_buffer);





////////////////////////////////////////////////////////////////////////////////////////
// TODO: update these to be inline functions for the ...ex() versions
////////////////////////////////////////////////////////////////////////////////////////
bool cmdln_args_find_flag(char flag);
bool cmdln_args_find_flag_uint32(char flag, command_var_t* arg, uint32_t* value);
bool cmdln_args_find_flag_string(char flag, command_var_t* arg, uint32_t max_len, char* str);
bool cmdln_args_float_by_position(uint32_t pos, float* value);
bool cmdln_args_uint32_by_position(uint32_t pos, uint32_t* value);
bool cmdln_args_string_by_position(uint32_t pos, uint32_t max_len, char* str);


// Finds the next command (if any) in the current single line of command input
bool cmdln_find_next_command(command_info_t* cp);


// TODO: Remove this once rotate operation implemented
uint32_t cmdln_pu(uint32_t i);


// try to add a byte to the command line buffer, return false if buffer full
bool cmdln_try_add(char* c);
// try to get a byte, return false if buffer empty
bool cmdln_try_remove(char* c);
inline bool cmdln_try_peek(uint32_t i, char* c) { return cmdln_try_peek_ex(&cmdln, i, c);}
// try to discard n bytes (advance the read offset)
// return false if end of buffer is reached
// (should be used with try_peek to confirm before discarding...)
bool cmdln_try_discard(uint32_t i);
// this moves the read offset to the write offset,
// allowing the next command line to be entered after the previous.
// this allows the history scroll through the circular buffer
bool cmdln_next_buf_pos(); // TODO: Rename this API to more clearly indicate its purpose



