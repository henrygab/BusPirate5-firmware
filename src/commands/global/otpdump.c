#define BP_DEBUG_OVERRIDE_DEFAULT_CATEGORY BP_DEBUG_CAT_OTP

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include "pico/stdlib.h"
#include "pirate.h"
#include "command_struct.h"       // File system related
#include "ui/ui_cmdln.h"    // This file is needed for the command line parsing functions
// #include "ui/ui_prompt.h" // User prompts and menu system
// #include "ui/ui_const.h"  // Constants and strings
#include "ui/ui_help.h"    // Functions to display help in a standardized way
#include "system_config.h" // Stores current Bus Pirate system configuration
#include "pico/bootrom.h"
#include "hardware/structs/otp.h"
#include "ui/ui_term.h"
#include "otpdump.h"

// This array of strings is used to display help USAGE examples for the dummy command
static const char* const usage[] = {
    "otpdump -r <start row> -c <maximum row count> -a",
    "   -r <start row>         : First OTP Row Address to dump",
    "   -c <maximum row count> : Maximum number of OTP rows to dump",
    "   -a                     : Show even blank (all-zero) rows",
    "",
    "By default, this command will show only non-blank rows.",
};


// This is a struct of help strings for each option/flag/variable the command accepts
// Record type 1 is a section header
// Record type 0 is a help item displayed as: "command" "help text"
// This system uses the T_ constants defined in translation/ to display the help text in the user's preferred language
// To add a new T_ constant:
//      1. open the master translation en-us.h
//      2. add a T_ tag and the help text
//      3. Run json2h.py, which will rebuild the translation files, adding defaults where translations are missing
//      values
//      4. Use the new T_ constant in the help text for the command
static const struct ui_help_options cmdline_options[] = {
    /*{ 1, "", T_HELP_DUMMY_COMMANDS },    // section heading
    { 0, "init", T_HELP_DUMMY_INIT },    // init is an example we'll find by position
    { 0, "test", T_HELP_DUMMY_TEST },    // test is an example we'll find by position
    { 1, "", T_HELP_DUMMY_FLAGS },       // section heading for flags
    { 0, "-b", T_HELP_DUMMY_B_FLAG },    //-a flag, with no optional string or integer
    { 0, "-i", T_HELP_DUMMY_I_FLAG },    //-b flag, with optional integer*/
    { 1, "", T_HELP_DUMMY_COMMANDS},
};

typedef struct _PARSED_OTP_COMMAND_OPTIONS {
    uint16_t StartRow;
    uint16_t MaximumRows;
    bool ShowAllRows;
} PARSED_OTP_COMMAND_OPTIONS;

typedef struct _OTP_DUAL_ROW_READ_RESULT {
    union {
        uint32_t as_uint32;
        struct { // is this order backwards?
            uint16_t row0;
            uint16_t row1;
        };
    };
} OTP_DUAL_ROW_READ_RESULT;
static_assert(sizeof(OTP_DUAL_ROW_READ_RESULT) == sizeof(uint32_t), "");

typedef struct _OTP_RAW_READ_RESULT {
    // anonymous structs are supported in C11
    union {
        uint32_t as_uint32;
        struct {
            uint8_t lsb;
            uint8_t msb;
            uint8_t correction;
            uint8_t is_error; // 0: ok, 0xFF: Permission failure, 0x77: ECC error likely
        };
    };
} OTP_RAW_READ_RESULT;
static_assert(sizeof(OTP_RAW_READ_RESULT) == sizeof(uint32_t), "");

typedef struct _OTP_READ_RESULT {
    OTP_RAW_READ_RESULT as_raw;
    uint16_t read_with_ecc;
    uint16_t read_via_bootrom; // recommended to use this as "truth" if no errors
    bool data_ok;
    bool err_permissions;
    bool err_possible_ecc_mismatch;
    bool err_from_bootrom;
} OTP_READ_RESULT;

static const uint16_t LAST_OTP_ROW  = 4095u; // 8k of corrected ECC data == 4k rows
static const uint16_t OTP_ROW_COUNT = 4096u; // 8k of corrected ECC data == 4k rows

static char map_to_printable_char(uint8_t c) {
    if (c >= ' ' && c <= '~') {
        return (char)c;
    }
    return '.';
}
static void print_otp_read_result(const OTP_READ_RESULT * data, uint16_t row) {    
    uint16_t row_data =
        (!data->err_from_bootrom         ) ? data->read_via_bootrom :
        (!data->err_possible_ecc_mismatch) ? data->read_with_ecc    :
        data->read_via_bootrom;
    printf("%s", ui_term_color_info());
    printf("Row 0x%03" PRIX16 ":", row);
    printf(" %04" PRIX16, data->read_via_bootrom);

    if (data->read_via_bootrom != data->read_with_ecc) {
        printf(" %s!==%s ", ui_term_color_warning(), ui_term_color_info());
    } else {
        printf(" === ");
    }
    printf("%04" PRIX16, data->read_with_ecc);

    if (((data->read_via_bootrom >> 8)    != data->as_raw.msb) ||
        ((data->read_via_bootrom & 0xFFu) != data->as_raw.lsb)) {
        printf(" %s=?=%s ", ui_term_color_warning(), ui_term_color_info());
    } else {
        printf(" === ");
    }
    printf("%02" PRIX8 " %02" PRIX8 " [%02" PRIX8 "] (%c%c)",
        data->as_raw.lsb,
        data->as_raw.msb,
        data->as_raw.correction,
        map_to_printable_char(row_data & 0xFFu),
        map_to_printable_char(row_data >> 8)
        );
    if (data->err_permissions) {
        printf("%s", ui_term_color_error());
        printf(" PERM");
        printf("%s", ui_term_color_info());
    } else {
        printf("     ");
    }
    if (data->err_possible_ecc_mismatch) {
        printf("%s", ui_term_color_warning());
        printf("  ECC");
        printf("%s", ui_term_color_info());
    } else {
        printf("     ");
    }
    if (data->err_from_bootrom) {
        printf("%s", ui_term_color_error());
        printf("  ROM");
        printf("%s", ui_term_color_info());
    } else {
        printf("     ");
    }
    printf("%s", ui_term_color_reset());
    printf("\r\n");
}

static bool _is_start_and_count_within_range(uint16_t start_row, uint16_t row_count, uint16_t first_valid_row, uint16_t last_valid_row) {
    if (first_valid_row < last_valid_row) {
        // API usage error
        PRINT_ERROR("API usage error: first_valid_row < last_valid_row");
        return false;
    }
    if (row_count == 0) {
        PRINT_INFO("Range Validation: row_count == 0 ... any start address permitted");
        return true; // always true when row_count is zero
    }
    if (first_valid_row == 0 && last_valid_row == UINT16_MAX) {
        // all values are within range ... cannot overflow
        // but... probably should still print a warning, as loops may be infinite unless careful
        PRINT_WARNING("Range Validation: first_valid_row === 0 and last_valid_row === UINT16_MAX ... caution advised in indexed loops!");
        return true;
    }
    // no overflow due to above check
    const uint16_t max_row_count = last_valid_row - first_valid_row + 1;
    if (row_count > max_row_count) {
        PRINT_WARNING("Range Validation: row_count (%04x) > max_row_count (%04x)", row_count, max_row_count);
        return false;
    }
    if (start_row < first_valid_row) {
        PRINT_WARNING("Range Validation: start_row (%04x) < first_valid_row (%04x)", start_row, first_valid_row);
        return false;
    }
    if (start_row > last_valid_row) {
        PRINT_WARNING("Range Validation: start_row (%04x) > last_valid_row (%04x)", start_row, last_valid_row);
        return false;
    }
    if (row_count == 1) {
        return true;
    }
    // start and count individually may be OK ... now verify range
    //     start + (count - 1) <= last_valid_row
    //     start <= last_valid_row - (count - 1)
    //     start <= last_valid_row - count + 1
    uint16_t tmp = last_valid_row;
    tmp -= row_count;
    tmp += 1;
    if (start_row > tmp) {
        PRINT_WARNING("Range Validation: start_row (%04x) + row_count (%04x) > last_valid_row (%04x) [calc: %04x]", start_row, row_count, last_valid_row, tmp);
        return false;
    }
    return true;
}
static bool _xotp_is_valid_start_row_and_count_impl1(uint16_t start_row, uint16_t row_count) {
    if (start_row > LAST_OTP_ROW) {
        PRINT_WARNING("OTP Range Validation: start_row (%04x) > LAST_OTP_ROW (%04x)", start_row, LAST_OTP_ROW);
        return false;
    }
    if (row_count > OTP_ROW_COUNT) {
        PRINT_WARNING("OTP Range Validation: row_count (%04x) > OTP_ROW_COUNT (%04x)", row_count, OTP_ROW_COUNT);
        return false;
    }
    uint16_t maximum_start_row = OTP_ROW_COUNT - row_count;
    if (start_row > maximum_start_row) {
        PRINT_WARNING("OTP Range Validation: start_row (%04x) + row_count (%04x) > OTP_ROW_COUNT (%04x) [calc: %04x]", start_row, row_count, OTP_ROW_COUNT, maximum_start_row);
        return false;
    }
    return true;
}
static bool _xotp_is_valid_start_row_and_count(uint16_t start_row, uint16_t row_count) {
    bool r1 = _is_start_and_count_within_range(start_row, row_count, 0, LAST_OTP_ROW);
    bool r2 = _xotp_is_valid_start_row_and_count_impl1(start_row, row_count);
    if (r1 != r2) {
        PRINT_ERROR("Range Validation Discrepancy: r1=%d, r2=%d for start_row=0x%04x, row_count=0x%04x", r1, r2, start_row, row_count);
        assert(false);
    }
    return r1;
}

// check res->error after calling this function to see if error occurred
static void parse_otp_command_line(PARSED_OTP_COMMAND_OPTIONS* options, struct command_result* res) {
    res->error = false;
    memset(options, 0, sizeof(PARSED_OTP_COMMAND_OPTIONS));
    options->StartRow = 0u;
    options->MaximumRows = OTP_ROW_COUNT;
    options->ShowAllRows = false; // normal dump is only non-zero data

    command_var_t arg;
    // NOTE: Optimizer will do its job.  Below formatting allows collapsing
    //       the code while allowing all main decision points to be at top level.

    // Parse the row count first, so if omitted, can auto-adjust row count later
    uint32_t row_count = 0u;

    bool row_count_flag = cmdln_args_find_flag_uint32('c', &arg, &row_count);
    if (row_count_flag && !arg.has_value) {
        printf(
            "ERROR: Row count requires an integer argument\r\n"
            );
        res->error = true;
    } else if (row_count_flag && arg.has_value && (row_count > OTP_ROW_COUNT || row_count == 0)) {
        printf(
            "ERROR: Row count (-c) must be in range [1..%" PRId16 "] (0x0..0x%" PRIx16 ")\r\n",
            OTP_ROW_COUNT, OTP_ROW_COUNT
            );
        // ui_help_show(true, usage, count_of(usage), &options[0], count_of(options));
        res->error = true;
    } else if (row_count_flag && arg.has_value) {
        options->MaximumRows = row_count; // bounds checked above
    }


    uint32_t start_row = 0;
    bool start_row_flag = cmdln_args_find_flag_uint32('r', &arg, &start_row);
    if (start_row_flag && !arg.has_value) {
        printf(
            "ERROR: Start row requires an integer argument\r\n"
            );
        res->error = true;
    } else if (start_row_flag && arg.has_value && start_row > LAST_OTP_ROW) {
        printf(
            "ERROR: Start row (-r) must be in range [0..%" PRId16 "] (0x0..0x%" PRIx16 ")\r\n",
            LAST_OTP_ROW, LAST_OTP_ROW
            );
        res->error = true;
    } else if (res->error) {
        // no checks of row count vs. start row ...
        // already had an error earlier and thus not meaningful check
    } else if (start_row_flag && arg.has_value && row_count_flag) {
        uint16_t maximum_start_row = OTP_ROW_COUNT - options->MaximumRows;
        // no automatic adjustment if both start and count arguments were provided.
        // instead, validate that the start + count are within bounds.
        // row_count is already individually bounds-checked 
        // start_row is already individually bounds-checked
        if (start_row > maximum_start_row) {
            printf(
                "ERROR: Start row (-r) + row count (-c) may not exceed %" PRId16 " (0x%" PRIx16 ")\r\n",
                OTP_ROW_COUNT, OTP_ROW_COUNT
                );
            res->error = true;
        }
    } else if (start_row_flag && arg.has_value) {
        options->StartRow = start_row;
        // automatically adjust the row count
        // (only when row count not explicitly provided)
        // start_row bounds-checked above
        uint16_t maximum_row_count = OTP_ROW_COUNT - start_row;
        if (options->MaximumRows > maximum_row_count) {
            options->MaximumRows = maximum_row_count;
        }
    }

    options->ShowAllRows = cmdln_args_find_flag('a');

    if (res->error) {
        PRINT_WARNING("Detected error parsing OTPDump parameters\n");
    } else {
        PRINT_VERBOSE("Parsed OTPDump parameters: StartRow=0x%04x, MaximumRows=0x%04x, ShowAllRows=%d",
            options->StartRow, options->MaximumRows, options->ShowAllRows
            );
    }
    return;
}

static void internal_triple_read_otp(OTP_READ_RESULT* out_data, uint16_t row) {
    memset(out_data, 0, sizeof(OTP_READ_RESULT));

    // Use the read aliases ... not the ROM functions ...
    // as they are read-only and thus "safer" to use without error.
    // These MUST remain as 32-bit-size, 32-bit-aligned reads.
    // OTP_DATA_BASE     // reads two consecutive rows, with ECC correction applied -- returns 0xFFFFFFFFu on failure(!)
    // OTP_DATA_RAW_BASE // reads 24 bits of raw data (HW ECC correction disabled)

    // Read the memory-mapped raw OTP data.
    do {
        out_data->as_raw.as_uint32 = ((volatile uint32_t*)(OTP_DATA_RAW_BASE))[row];

        if (out_data->as_raw.is_error != 0) {
            out_data->err_permissions = true;
        }
    } while (0);

    // Read the memory-mapped ECC corrected data.
    do {
        // divide the row number by two to determine the correct read offset
        OTP_DUAL_ROW_READ_RESULT corrected_data = {0};
        corrected_data.as_uint32 = ((volatile uint32_t*)(OTP_DATA_BASE))[row/2u];
        out_data->read_with_ecc = (row & 1) ? corrected_data.row1 : corrected_data.row0;
        // Check if there was a potential ECC error
        if (corrected_data.as_uint32 == 0xFFFFFFFFu) {
            out_data->err_possible_ecc_mismatch = true;
        }
    } while (0);

    // Maybe the ROM function will detect ECC on a single row?
    do {
        uint16_t buffer[2] = {0xAAAAu, 0xAAAAu}; // detect if bootrom overflows reading single ECC row
        otp_cmd_t cmd = {0};
        cmd.flags = (row & 0xFFFFu) | 0x00020000; // IS_ECC + Row number
        int ret = rom_func_otp_access((uint8_t*)buffer, sizeof(uint16_t), cmd);
        if (ret != BOOTROM_OK) {
            out_data->err_from_bootrom = true;
        } else if (buffer[1] != 0xAAAAu) {
            printf("Bootrom asked to read single uint16_t from OTP, but read two values (buffer overflow)");
            out_data->read_via_bootrom = buffer[0];
            out_data->err_from_bootrom = true;
        } else {
            out_data->read_via_bootrom = buffer[0];
        }
    } while (0);

    if (out_data->err_permissions || out_data->err_possible_ecc_mismatch || out_data->err_from_bootrom) {
        out_data->data_ok = false;
    } else {
        out_data->data_ok = true;
    }

    return;
}

void dump_otp(uint16_t start_row, uint16_t row_count, bool show_all_rows) {
    uint16_t remaining_rows = row_count;
    for (uint16_t current_row = start_row; remaining_rows; --remaining_rows, ++current_row) {
        OTP_READ_RESULT result = {0};
        internal_triple_read_otp(&result, current_row);
        if (!show_all_rows && result.data_ok && result.read_with_ecc == 0) {
            continue; // to next row ... skipping blank rows
        }
        print_otp_read_result(&result, current_row);
    }
    return;
}

void otpdump_handler(struct command_result* res) {

    // HACKHACK -- enable RTT manually here
    _DEBUG_ENABLED_CATEGORIES |= (1u << E_DEBUG_CAT_OTP);
    _DEBUG_LEVELS[E_DEBUG_CAT_OTP] = BP_DEBUG_LEVEL_VERBOSE;

    // the help -h flag can be serviced by the command line parser automatically, or from within the command
    // the action taken is set by the help_text variable of the command struct entry for this command
    // 1. a single T_ constant help entry assigned in the commands[] struct in commands.c will be shown automatically
    // 2. if the help assignment in commands[] struct is 0x00, it can be handled here (or ignored)
    // res.help_flag is set by the command line parser if the user enters -h
    // we can use the ui_help_show function to display the help text we configured above
    if (ui_help_show(res->help_flag, usage, count_of(usage), &cmdline_options[0], count_of(cmdline_options))) {
        return;
    }
    PARSED_OTP_COMMAND_OPTIONS options;
    parse_otp_command_line(&options, res);
    if (res->error) {
        ui_help_show(true, usage, count_of(usage), &cmdline_options[0], count_of(cmdline_options));
        return;
    }

    dump_otp(options.StartRow, options.MaximumRows, options.ShowAllRows);
    return;
}



// Directory entries in OTP start at the END of the user-programmable area.
// Row address 0x0C0 appears to be first unused entry at the start.
// Row address 0xF40 appears to be the first used entry at the end.
// Thus, start iterator at 0xF3C and work backwards.
// Each core has its own iterator.
static const uint16_t C_XOTP_ROWS_PER_DIRECTORY_ENTRY = sizeof(XOTP_DIRECTORY_ITEM) / sizeof(uint16_t);
static const uint16_t C_XOTP_DIRECTORY_START = 0xF40u - C_XOTP_ROWS_PER_DIRECTORY_ENTRY;
typedef struct _DIRENTRY_ITERATOR_STATE {
    uint16_t current_row; // invalid by default, starts at C_XOTP_DIRECTORY_START
    uint8_t revision;     // Always zero for now....
    bool is_valid;        // false by default, must reset before any use of iterator
} DIRENTRY_ITERATOR_STATE;

static bool _xotp_read_ecc_buffer(void* out_buffer, size_t buffer_len, uint16_t start_address) {
    if (out_buffer == NULL || buffer_len == 0) {
        return false;
    }
    memset(out_buffer, 0, buffer_len);
    if (buffer_len % 2u != 0u) {
        PRINT_ERROR("_xotp_read_ecc_buffer: API Usage Error: Buffer length must be even, was 0x%zx", buffer_len);
        // restricted to buffers of even length
        // buffer_len zero excluded above
        // buffer_len one excluded here
        // thus, buffer_len must be >= 2 when this if statement is false
        return false;
    }
    size_t row_count = buffer_len / 2u;
    // validate bounds
    if (!_xotp_is_valid_start_row_and_count(start_address, row_count)) {
        return false;
    }

    // perform the ECC read using bootrom
    otp_cmd_t cmd = {0};
    cmd.flags = (start_address & 0xFFFFu) | 0x00020000; // IS_ECC | Row number
    int ret = rom_func_otp_access(out_buffer, buffer_len, cmd);
    if (ret != BOOTROM_OK) {
        PRINT_WARNING("_xotp_read_ecc_buffer: Bootrom error %d reading OTP data with CMD %08x, length 0x%zx", ret, cmd.flags, buffer_len);
        return false;
    }
    return true;
}
// fails only on invalid parameters, or exceeding valid read range for directory entries
// check entry type for ECC read errors.
static bool _xotp_read_directory_entry(XOTP_DIRECTORY_ITEM* out_direntry, uint16_t start_address) {
    if (out_direntry == NULL) {
        PRINT_ERROR("_xotp_read_directory_entry: API Usage Error: out_direntry is NULL");
        return false;
    }
    memset(out_direntry, 0, sizeof(XOTP_DIRECTORY_ITEM));

    // addressing is more restrictive than _xotp_read_ecc_buffer()
    if (start_address > C_XOTP_DIRECTORY_START) {
        // Corruption detected?  invalidate iterator
        PRINT_ERROR("_xotp_read_directory_entry: iter start_address (%04x) > C_XOTP_DIRECTORY_START (%04x)", start_address, C_XOTP_DIRECTORY_START);
        return false;
    }
    if (start_address <= 0x0C0) {
        // can't store entries in the predefined OTP areas
        PRINT_WARNING("_xotp_read_directory_entry: iter start_address (%04x) <= 0x0C0 ... ending search", start_address);
        return false;
    }

    bool read_ok = _xotp_read_ecc_buffer(out_direntry, sizeof(XOTP_DIRECTORY_ITEM), start_address);
    if (!read_ok) {
        PRINT_WARNING("_xotp_read_directory_entry: ECC read error at start_address %04x", start_address);
        // special hack to report ECC error as permissible-to-continue error
        // report this as an entry encoded with "ECC_ERROR" as the type
        memset(out_direntry, 0, sizeof(XOTP_DIRECTORY_ITEM));
        out_direntry->EntryType = XOTP_DIRTYPE_ECC_ERROR;
        // TODO: set valid CRC16 value?
    }
    return true;
}
static bool _xotp_validate_directory_entry(const XOTP_DIRECTORY_ITEM* direntry) {
    if (direntry == NULL) {
        PRINT_ERROR("_xotp_validate_directory_entry: API Usage Error: direntry is NULL");
        return false;
    }
    // BUGBUG / TODO: Validate the CRC16 of the directory entry
    PRINT_WARNING("_xotp_validate_directory_entry: CRC16 validation not yet implemented");
    if (direntry->RowCount != 0) {
        // allow any start address when RowCount is zero
        if (!_xotp_is_valid_start_row_and_count(direntry->StartRow, direntry->RowCount)) {
            PRINT_WARNING("_xotp_validate_directory_entry: detected invalid start_row (%04x) or row_count (%04x)", direntry->StartRow, direntry->RowCount);
            return false;
        }
    }
    return true;
}

// NOTE: there is a distinct iterator for each core.
static DIRENTRY_ITERATOR_STATE _xotp_iter[2] = {0};

void xotp_reset_directory_iterator() {
    PRINT_VERBOSE("Resetting OTP directory iterator for core %d", get_core_num());
    DIRENTRY_ITERATOR_STATE* iter = &_xotp_iter[get_core_num()];
    memset(iter, 0, sizeof(DIRENTRY_ITERATOR_STATE));
    iter->current_row = C_XOTP_DIRECTORY_START;
    iter->revision = 0;
    iter->is_valid = true;
}
bool xotp_get_next_directory_entry(XOTP_DIRECTORY_ITEM* out_direntry) {
    // zero the output buffer
    if (!out_direntry) {
        // API usage error
        PRINT_ERROR("xotp_get_next_directory_entry: API Usage Error: out_direntry is NULL");
        return false;
    }
    memset(out_direntry, 0, sizeof(XOTP_DIRECTORY_ITEM));

    DIRENTRY_ITERATOR_STATE* iter = &_xotp_iter[get_core_num()];
    while (iter->is_valid) {
        // Read the directory entry at iter->current_row
        XOTP_DIRECTORY_ITEM result = {0};
        // read the directory entry
        if (!_xotp_read_directory_entry(&result, iter->current_row)) {
            // does not fail except on invalid parameters
            // (read errors result in XOTP_DIRTYPE_ECC_ERROR)
            // so invalidate the iterator
            PRINT_ERROR("xotp_get_next_directory_entry: Invalid current row %04x ?", iter->current_row);
            iter->is_valid = false;
            continue;
        }
        if (XOTP_DIRTYPE_ECC_ERROR.as_uint16 == result.EntryType.as_uint16) {
            // Skip entries that have ECC errors ...
            PRINT_WARNING("xotp_get_next_directory_entry: skipping row %04x due to ECC errors", iter->current_row);
            iter->current_row -= C_XOTP_ROWS_PER_DIRECTORY_ENTRY;
            continue;
        }
        if (XOTP_DIRTYPE_BLANK.as_uint16 == result.EntryType.as_uint16) {
            // end of directory entries
            PRINT_WARNING("xotp_get_next_directory_entry: blank entry found at row %04x", iter->current_row);
            iter->is_valid = false;
            continue;
        }
        if (!_xotp_validate_directory_entry(&result)) {
            // Internally inconsistent ... end of entries
            // Want to invalidate the entry?  use raw write to force an ECC error by flipping two bits
            PRINT_WARNING("xotp_get_next_directory_entry: invalid directory entry found at row %04x", iter->current_row);
            iter->is_valid = false;
            continue;
        }
        // SUCCESS!
        memcpy(out_direntry, &result, sizeof(XOTP_DIRECTORY_ITEM));
        iter->current_row -= C_XOTP_ROWS_PER_DIRECTORY_ENTRY;
        return true;
    }

    PRINT_WARNING("xotp_get_next_directory_entry: No more directory entries found");
    return false;
}
bool xotp_find_directory_item(XOTP_DIRENTRY_TYPE type, XOTP_DIRECTORY_ITEM* out_direntry) {
    if (!out_direntry) {
        return false;
    }
    while (xotp_get_next_directory_entry(out_direntry)) {
        if (type.as_uint16 == out_direntry->EntryType.as_uint16) {
            return true;
        }
    }
    return false;
}
bool xotp_get_directory_item_data(
    const XOTP_DIRECTORY_ITEM* direntry, void* out_buffer, size_t buffer_len
    ) {
    #pragma region    // Parameter validation
    if (direntry == NULL) {
        PRINT_ERROR("xotp_get_directory_item_data: API Usage Error: direntry is NULL");
        return false;
    }
    if (direntry->RowCount == 0) {
        // no data to copy ... but maybe this is *NOT* an error?
        // and if zero rows to copy, maybe OK to allow
        // out_buffer to be null and buffer_len to be zero
        return true;
    }
    if (out_buffer == NULL) {
        PRINT_ERROR("xotp_get_directory_item_data: API Usage Error: out_buffer is NULL");
        return false;
    }
    if (buffer_len == 0) {
        PRINT_ERROR("xonp_get_directory_item_data: API Usage Error: buffer_len is zero");
        return false;
    }
    if (buffer_len % 2 != 0) {
        // cannot read an odd number of bytes
        PRINT_ERROR("xotp_get_directory_item_data: API Usage Error: buffer_len must be even, was 0x%zx", buffer_len);
        return false;
    }
    memset(out_buffer, 0, buffer_len);
    if (!_xotp_is_valid_start_row_and_count(direntry->StartRow, direntry->RowCount)) {
        PRINT_WARNING("xotp_get_directory_item_data: API Usage Error: invalid start_row (%04x) or row_count (%04x)", direntry->StartRow, direntry->RowCount);
        return false;
    }
    if (direntry->EntryType.ecc_type != OTP_ECC_TYPE_STANDARD) {
        // BUGBUG -- This is currently presuming all data is using ECC, thus read at 16-bits per row.
        //           Need to implement check of direntry->EntryType, and if it needs raw read,
        PRINT_ERROR("NYI -- xotp_get_directory_item_data: currently only supporting read of ECC stored data");
        return false;
    }
    // BUGBUG -- update this when supporting raw reads  (also needs helper function to fill buffer with raw data)
    size_t required_data_length = direntry->RowCount * sizeof(uint16_t);
    if (buffer_len != required_data_length) {
        PRINT_ERROR("xotp_get_directory_item_data: API Usage Error: buffer_len (0x%zx) must be 0x%zx to read %04x rows", buffer_len, required_data_length, direntry->RowCount);
        return false;
    }
    #pragma endregion // Parameter validation

    // Should we allow reading partial data?
    size_t bytes_to_read = MIN(buffer_len, direntry->RowCount * sizeof(uint16_t));
    // Can we just read all the data using the bootrom?
    if (_xotp_read_ecc_buffer(out_buffer, bytes_to_read, direntry->StartRow)) {
        // Great!  No need for the edge cases below.
        // buffer already filled in with data, so just set valid byte count
        return true;
    }
    // Edge case: at least one of those OTP rows could not be read.
    PRINT_WARNING("xotp_get_directory_item_data: Error reading OTP data, start 0x%04x count 0x%04x.", direntry->StartRow, direntry->RowCount);
    return false; // partial results not supported yet
}





