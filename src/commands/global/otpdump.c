#define BP_DEBUG_OVERRIDE_DEFAULT_CATEGORY BP_DEBUG_CAT_OTP

#include <stddef.h>
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

#define FIELD_OFFSET(type, field) ((size_t)&(((type*)0)->field))


#pragma region    // Basic CRC16
static uint16_t crc16_table_a001[] = {
	0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
	0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
	0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
	0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
	0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
	0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
	0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
	0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
	0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
	0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
	0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
	0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
	0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
	0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
	0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
	0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
	0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
	0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
	0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
	0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
	0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
	0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
	0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
	0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
	0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
	0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
	0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
	0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
	0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
	0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
	0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
	0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040,
};
static inline uint16_t crc16_calculate(const void* buffer, size_t buffer_len) {
    const uint8_t * data = (const uint8_t*)buffer;
    uint16_t crc = 0x0000u;
    for (size_t i = 0; i < buffer_len; ++i) {
        uint8_t idx = crc ^ *data;
        ++data;
        crc = (crc >> 8) ^ crc16_table_a001[idx];
    }
    return crc;
}
#pragma endregion // Basic CRC16

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
    if (out_buffer == NULL) {
        PRINT_ERROR("_xotp_read_ecc_buffer: API Usage Error: Buffer cannot be NULL");
        return false;
    }
    if (buffer_len == 0) {
        PRINT_ERROR("_xotp_read_ecc_buffer: API Usage Error: Buffer length cannot be zero");
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
static bool _xotp_read_raw_buffer(void* out_buffer, size_t buffer_len, uint16_t start_address) {
    if (out_buffer == NULL) {
        PRINT_ERROR("_xotp_read_raw_buffer: API Usage Error: Buffer cannot be NULL");
        return false;
    }
    if (buffer_len == 0) {
        PRINT_ERROR("_xotp_read_raw_buffer: API Usage Error: Buffer length cannot be zero");
        return false;
    }
    memset(out_buffer, 0, buffer_len);
    if (buffer_len % 4u != 0u) {
        PRINT_ERROR("_xotp_read_raw_buffer: API Usage Error: Buffer length must be even, was 0x%zx", buffer_len);
        // restricted to buffers of even length
        // buffer_len zero excluded above
        // buffer_len one excluded here
        // thus, buffer_len must be >= 2 when this if statement is false
        return false;
    }
    size_t row_count = buffer_len / 4u;
    // validate bounds
    if (!_xotp_is_valid_start_row_and_count(start_address, row_count)) {
        return false;
    }

    // perform the ECC read using bootrom
    otp_cmd_t cmd = {0};
    cmd.flags = (start_address & 0xFFFFu); // just the row number ... no ECC flag
    int ret = rom_func_otp_access(out_buffer, buffer_len, cmd);
    if (ret != BOOTROM_OK) {
        PRINT_WARNING("_xotp_read_raw_buffer: Bootrom error %d reading OTP data with CMD %08x, length 0x%zx", ret, cmd.flags, buffer_len);
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
    uint16_t expected_crc = crc16_calculate(direntry, FIELD_OFFSET(XOTP_DIRECTORY_ITEM, CRC16));
    if (expected_crc != direntry->CRC16) {
        PRINT_WARNING("_xotp_validate_directory_entry: CRC16 mismatch: expected %04x, found %04x", expected_crc, direntry->CRC16);
        return false;
    }
    // if RowCount is zero, StartAddress may be any value
    // thus, only validate when non-zero row count
    if (direntry->RowCount != 0) {
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
    if (out_buffer == NULL) {
        PRINT_ERROR("xotp_get_directory_item_data: API Usage Error: out_buffer is NULL");
        return false;
    }
    if (buffer_len == 0) {
        PRINT_ERROR("xonp_get_directory_item_data: API Usage Error: buffer_len is zero");
        return false;
    }
    memset(out_buffer, 0, buffer_len);
    if (direntry == NULL) {
        PRINT_ERROR("xotp_get_directory_item_data: API Usage Error: direntry is NULL");
        return false;
    }
    if (direntry->RowCount == 0) {
        // no data to copy ... but maybe this is *NOT* an error?
        // and if zero rows to copy, maybe OK to allow
        // out_buffer to be null and buffer_len to be zero
        return false;
    }
    if (buffer_len % 2 != 0) {
        // cannot read an odd number of bytes
        PRINT_ERROR("xotp_get_directory_item_data: API Usage Error: buffer_len must be even, was 0x%zx", buffer_len);
        return false;
    }
    if (!_xotp_validate_directory_entry(direntry)) {
        PRINT_WARNING("xotp_get_directory_item_data: Invalid directory entry");
        return false;
    }
    size_t required_data_length = direntry->RowCount;
    required_data_length *= (direntry->EntryType.ecc_type == OTP_ECC_TYPE_STANDARD) ? sizeof(uint16_t) : sizeof(uint32_t);
    if (buffer_len != required_data_length) {
        PRINT_ERROR("xotp_get_directory_item_data: API Usage Error: buffer_len (0x%zx) must be 0x%zx to read %04x rows", buffer_len, required_data_length, direntry->RowCount);
        return false;
    }
    #pragma endregion // Parameter validation

    bool success =
        (direntry->EntryType.ecc_type == OTP_ECC_TYPE_STANDARD) ?
        _xotp_read_ecc_buffer(out_buffer, buffer_len, direntry->StartRow) :
        _xotp_read_raw_buffer(out_buffer, buffer_len, direntry->StartRow) ;

    if (!success) {
        PRINT_WARNING("xotp_get_directory_item_data: Error reading OTP data, type 0x%04x start 0x%04x count 0x%04x.", direntry->EntryType.as_uint16, direntry->StartRow, direntry->RowCount);
    }
    return success;
}





