#pragma once

// Although named `command_line`, this structure
// covers the full history of the command line input.
// so it may have multiple command lines stored within.
// When read and write offsets are equal, the buffer is empty.
// Read and write offsets should ALWAYS be between 0 and UI_CMDBUFFSIZE-1.
struct _command_line {
    uint32_t write_offset;    // NOT a pointer .. this is an offset into member .buf (a circular buffer)
    uint32_t rptr;    // NOT a pointer .. this is an offset into member .buf (a circular buffer)
    uint32_t histptr; // NOT a pointer .. this is an offset into member .buf (a circular buffer)
    uint32_t cursptr; // NOT a pointer .. this is an offset into member .buf (a circular buffer)
    char buf[UI_CMDBUFFSIZE];
};

// This structure is used to track a single "command" and all options associated with that command.
// At present, multiple commands can be entered in a single command line,
// and this structure will define the offsets to a single one of those commands.

// TODO: rename this structure to avoid confusion between pointers / offsets ... maybe `_command_extent`?
struct _command_pointer {
    uint32_t wptr; // NOT a pointer .. this is an offset into member .buf (a circular buffer)
    uint32_t rptr; // NOT a pointer .. this is an offset into member .buf (a circular buffer)
};

// TODO: document the fields in this structure
// WARNING: the end offset might EXCEED UI_CMDBUFFSIZE?!  Change to be a character count instead of end offset?
// WARNING: Some fields are modulo'd by UI_CMDBUFFSIZE, others are NOT.  This is just asking for bugs....
struct _command_info_t {
    uint32_t rptr;     // NOT a pointer .. this is an offset into member .buf (a circular buffer)
    uint32_t wptr;     // NOT a pointer .. this is an offset into member .buf (a circular buffer)
    uint32_t startptr; // NOT a pointer .. this is an offset into member .buf (a circular buffer)
    uint32_t endptr;   // NOT a pointer .. this is an offset into member .buf (a circular buffer)
    uint32_t nextptr;  // NOT a pointer .. this is an offset into member .buf (a circular buffer)
    char delimiter;
    char command[9];   // BUGBUG -- Hard-coded buffer size ... should this be MAX_COMMAND_LENGTH?
};

typedef struct command_var_struct {
    bool has_arg;
    bool has_value;
    uint32_t value_pos;
    bool error;
    uint8_t number_format;
} command_var_t;

bool cmdln_args_find_flag(char flag);
bool cmdln_args_find_flag_uint32(char flag, command_var_t* arg, uint32_t* value);
bool cmdln_args_find_flag_string(char flag, command_var_t* arg, uint32_t max_len, char* str);
bool cmdln_args_float_by_position(uint32_t pos, float* value);
bool cmdln_args_uint32_by_position(uint32_t pos, uint32_t* value);
bool cmdln_args_string_by_position(uint32_t pos, uint32_t max_len, char* str);
bool cmdln_find_next_command(struct _command_info_t* cp);
bool cmdln_info(void);
bool cmdln_info_uint32(void);

// update a command line buffer offset with rollover -- (i.e., modulo operator due to circular buffer)
uint32_t cmdln_pu(uint32_t i);
// try to add a byte to the command line buffer, return false if buffer full
bool cmdln_try_add(char* c);
// try to get a byte, return false if buffer empty
bool cmdln_try_remove(char* c);
// try to peek 0+n bytes (no offsets updated)
// returns false if at the end of the buffer
// this should always be used sequentially from zero,
// if wanting to peek multiple characters forward
//     e.g., bool got_char = peek(0, &c) && peek(1, &c);
// to avoid missing the end of the buffer
bool cmdln_try_peek(uint32_t i, char* c);
// try to discard n bytes (advance the read offset)
// return false if end of buffer is reached
// (should be used with try_peek to confirm before discarding...)
bool cmdln_try_discard(uint32_t i);
// this moves the read offset to the write offset,
// allowing the next command line to be entered after the previous.
// this allows the history scroll through the circular buffer
bool cmdln_next_buf_pos(void);

void cmdln_init(void);

// TODO: rename this API to avoid confusion about pointers / offsets
bool cmdln_try_peek_pointer(struct _command_pointer* cp, uint32_t i, char* c);
// TODO: rename this API to avoid confusion about pointers / offsets
void cmdln_get_command_pointer(struct _command_pointer* cp);

extern struct _command_line cmdln;