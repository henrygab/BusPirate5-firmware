#define BP_DEBUG_OVERRIDE_DEFAULT_CATEGORY BP_DEBUG_CAT_OTP

#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../pirate.h"
#include "bp_otp.h"
#include "debug_rtt.h"


// ****** IMPORTANT DEVELOPEMENT NOTE ******
// During development, it's REALLY useful to force the code to single-step through this process.
// To support this, this file has code that uses waits for the RTT terminal to accept input.
//
// To use this is a TWO STEP process:
// 1. Define WAIT_FOR_KEY() to be MyWaitForAnyKey_with_discards() in this file.
// 2. At the location in the code you want to start single-stepping, set the static `g_WaitForKey` to true.
//
// Because OTP fuses can only transition from 0 -> 1, this capability is critical to
// minimizing the number of RP2350 chips with invalid data during development.


// TODO: update this to wait for actual keypresses over RTT (as in the earlier experimentation firmware)
//       to allow for review of all the things happening....
#define WAIT_FOR_KEY()
// #define WAIT_FOR_KEY() MyWaitForAnyKey_with_discards()

// decide where to single-step through the whitelabel process ... controlled via RTT (no USB connection required)
static volatile bool g_WaitForKey = false;
static void MyWaitForAnyKey_with_discards(void) {
    if (!g_WaitForKey) {
        return;
    }
    // clear any prior keypresses
    int t;
    do {
        t = SEGGER_RTT_GetKey();
    } while (t >= 0);

    int c = SEGGER_RTT_WaitKey();

    // clear any remaining kepresses (particularly useful for telnet, which does line-by-line input)
    do {
        t = SEGGER_RTT_GetKey();
    } while (t >= 0);

    return;
}

// ======================================================================
// the actual structure for the OTP DIRENTRY should remain opaque to callers.
// this allows us to fix any architectural oversights later in a backwards-compatible
// manner, without changing most client code.
typedef struct _BP_OTPDIR_ENTRY {
    union {
        uint8_t  as_uint8_t[8];
        uint16_t as_uint16[4];
 
        struct {
            BP_OTPDIR_ENTRY_TYPE entry_type;
            union {
                // For BP_OTPDIR_DATA_ENCODING_TYPE_NONE
                // All fields must be zero.
                // If the entry_type value is also zero, then by definition the CRC16 value is also zero.
                struct {
                    uint16_t     must_be_zero[2];
                } none; // BP_OTPDIR_DATA_ENCODING_TYPE_NONE
                // For BP_OTPDIR_DATA_ENCODING_TYPE_RAW
                // Start row is the first row storing the data.
                // The row count must be non-zero.
                // For buffer allocation purposes, the total data size is 32-bits for every row.
                // (See raw data ... 24 bits returned as a 32-bit word).
                struct {
                    uint16_t     start_row;   // first row of the data
                    uint16_t     row_count;   // count of rows used for the data, each row considered to be 32-bits in size (e.g., Raw 24 bits)
                } raw_data;
                // For BP_OTPDIR_DATA_ENCODING_TYPE_BYTE3X
                // Start row is the first row storing the data.
                // The row count must be non-zero.
                // For buffer allocation purposes, the total data size is equal to the row count (one byte per row).
                struct {
                    uint16_t     start_row;   // first row of the data
                    uint16_t     row_count;   // number of rows used for the data, each row storing a single byte of data
                } byte3x_data;
                // For BP_OTPDIR_DATA_ENCODING_TYPE_RBIT3
                // Start row is the first row storing the data.
                // The row count must be a non-zero multiple of 3.
                // For buffer allocation purposes, the total data size is 32-bit for every three rows.
                // (See raw data ... 24 bits in a 32-bit word).
                struct {
                    uint16_t     start_row;   // first row of the data
                    uint16_t     row_count;   // MUST be a multiple of 3 ... as each row must be duplicated three times; every 3 rows considered to store 32-bits in size (e.g., Raw 24 bits)
                } rbit3_data;
                // For BP_OTPDIR_DATA_ENCODING_TYPE_RBIT8
                // Start row is the first row storing the data.
                // The row count must be a non-zero multiple of 8.
                // For buffer allocation purposes, the total data size is 32-bits for every eight rows.
                // (See raw data ... 24 bits in a 32-bit word).
                struct {
                    uint16_t     start_row;   // first row of the data
                    uint16_t     row_count;   // MUST be a multiple of 8 ... as each row must be duplicated eight times; every 8 rows considered to store 32-bits in size (e.g., Raw 24 bits)
                } rbit8_data;
                // For BP_OTPDIR_DATA_ENCODING_TYPE_ECC and BP_OTPDIR_DATA_ENCODING_TYPE_ECC_ASCII_STRING
                // Start row is the first row storing the data.
                // The byte count must be non-zero.
                // For buffer allocation purposes, the total data size is stored directly in the OTP directory entry.
                // The row count is the byte count divided by 2 (rounded up).
                struct {
                    uint16_t     start_row;  // first row of the data
                    uint16_t     byte_count; // count of valid bytes in the data (including trailing NULL for strings)
                } ecc_data;
                // For BP_OTPDIR_DATA_ENCODING_TYPE_EMBEDED_IN_DIRENTRY
                // Data is stored embedded directly in the directory entry.
                // For buffer allocation purposes, there is always a uint32_t of data.
                // CAUTION: When implementing, must manually construct / decontruct the uint32_t value,
                // due to alignment requirements, cannot declare this as a uint32_t directly in the structure
                // (without pack pragmas or the like).
                struct {
                    uint8_t      data[4];    // Up to 32-bits of data stored directly in the directory entry
                } embedded_data;
            };
            uint16_t             crc16;
        }; // common entry_type and CRC16, with union for various encodings into the directory entry
    };
} BP_OTPDIR_ENTRY;
static_assert(sizeof(BP_OTPDIR_ENTRY) == (4 * sizeof(uint16_t)));
#define xROWS_PER_DIRENTRY 4u


#if RPI_PLATFORM == RP2350
    #define xCORE_COUNT 2u
    #define xFIRST_USER_CONTENT_ROW ((uint16_t)0x0C0u)
    #define xUSER_CONTENT_ROW_COUNT ((uint16_t)(0xF40u - xFIRST_USER_CONTENT_ROW))
#else
    #error "unsupported platform ... may have different count of cores"
#endif

// Define a hard-coded location at which to start searching for directory entries.
#define BP_OTPDIR_ENTRY_START_ROW ((uint16_t)(xFIRST_USER_CONTENT_ROW + xUSER_CONTENT_ROW_COUNT - xROWS_PER_DIRENTRY))


// define iterator state so it's invalid (needs reset) when zero-initialized.
typedef struct _XOTP_DIRENTRY_ITERATOR_STATE {
    uint16_t        current_otp_row_start;                // zero is not a valid OTP row for an OTP_DIRENTRY item
    BP_OTPDIR_ENTRY current_entry;                        // all-zero data is indicative of end-of-directory
    bool            entry_validated;                      // zero means entry not yet validated (entry type, crc, start row, data length)
    bool            should_try_next_row_if_not_validated; // set to true when read fails with ECC error
} XOTP_DIRENTRY_ITERATOR_STATE;

// One "current" OTP_DIRENTRY location is stored for each core.
// This avoids the need for cores to synchronize their read-only operations
// on the OTP directory.
static XOTP_DIRENTRY_ITERATOR_STATE x_current_directory_entry[xCORE_COUNT] = {};

#pragma region    // Basic CRC16
// It is critical that this CRC return a value of 0x0000u
// when the data provided to it is all-zero, to account for
// an unwritten area being considered an indication of the
// end-of-directory (appendable).
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


#pragma region    // OTP Directory Data Encoding Type Validation functions
bool x_otpdir_is_valid_user_content_row_range(uint16_t start_row, uint16_t row_count) {
    if (start_row < xFIRST_USER_CONTENT_ROW) {
        PRINT_WARNING("Validation of OTPDIR entry as RAW failed: start row (%03x) is less than first user content row %03x",
            start_row, xFIRST_USER_CONTENT_ROW
        );
        return false;
    }
    uint16_t allowed_row_count = xUSER_CONTENT_ROW_COUNT - (start_row - xFIRST_USER_CONTENT_ROW);
    if (row_count > allowed_row_count) {
        PRINT_WARNING("Validation of OTPDIR entry as RAW failed: row count (%03x) exceeds %03x maximum rows for starting at row %03x (user content starts at %03x, %03x rows)",
            row_count, allowed_row_count, start_row, xFIRST_USER_CONTENT_ROW, xUSER_CONTENT_ROW_COUNT
        );
        return false;
    }
    return true;
}

// BP_OTPDIR_DATA_ENCODING_TYPE_NONE
bool x_otpdir_entry_appears_valid_none(const BP_OTPDIR_ENTRY* entry) {
    if (entry->entry_type.encoding_type != BP_OTPDIR_DATA_ENCODING_TYPE_NONE) {
        PRINT_ERROR("Validating entry type as NONE, but type of the entry is 0x%02x", entry->entry_type.encoding_type);
        return false;
    }
    if ((entry->none.must_be_zero[0] != 0u) || (entry->none.must_be_zero[1] != 0u)) {
        PRINT_ERROR("Validating entry type as NONE, but data is not all zero");
        return false;
    }
    return true;
}
// BP_OTPDIR_DATA_ENCODING_TYPE_RAW
bool x_otpdir_entry_appears_valid_raw(const BP_OTPDIR_ENTRY* entry) {
    if (entry->entry_type.encoding_type != BP_OTPDIR_DATA_ENCODING_TYPE_RAW) {
        PRINT_ERROR("Validating entry type as RAW, but type of the entry is 0x%02x", entry->entry_type.encoding_type);
        return false;
    }
    if (entry->raw_data.row_count == 0u) {
        PRINT_ERROR("Validating entry type as RAW, but row count is zero");
        return false;
    }
    if (!x_otpdir_is_valid_user_content_row_range(entry->raw_data.start_row, entry->raw_data.row_count)) {
        return false;
    }
    return true;
}
// BP_OTPDIR_DATA_ENCODING_TYPE_BYTE3X
bool x_otpdir_entry_appears_valid_byte3x(const BP_OTPDIR_ENTRY* entry) {
    if (entry->entry_type.encoding_type != BP_OTPDIR_DATA_ENCODING_TYPE_BYTE3X) {
        PRINT_ERROR("Validating entry type as BYTE3X, but type of the entry is 0x%02x", entry->entry_type.encoding_type);
        return false;
    }
    if (entry->byte3x_data.row_count == 0u) {
        PRINT_ERROR("Validating entry type as BYTE3X, but row count is zero");
        return false;
    }
    if (!x_otpdir_is_valid_user_content_row_range(entry->byte3x_data.start_row, entry->byte3x_data.row_count)) {
        return false;
    }
    return true;
}
// BP_OTPDIR_DATA_ENCODING_TYPE_RBIT3
bool x_otpdir_entry_appears_valid_rbit3(const BP_OTPDIR_ENTRY* entry) {
    if (entry->entry_type.encoding_type != BP_OTPDIR_DATA_ENCODING_TYPE_RBIT3) {
        PRINT_ERROR("Validating entry type as RBIT3, but type of the entry is 0x%02x", entry->entry_type.encoding_type);
        return false;
    }
    if (entry->rbit3_data.row_count == 0u) {
        PRINT_ERROR("Validating entry type as RBIT3, but row count is zero");
        return false;
    }
    if (!x_otpdir_is_valid_user_content_row_range(entry->rbit3_data.start_row, entry->rbit3_data.row_count)) {
        return false;
    }
    if ((entry->rbit3_data.row_count % 3u) != 0u) {
        PRINT_ERROR("Validating entry type as RBIT3, but row count (%03x) is not a multiple of 3", entry->rbit3_data.row_count);
        return false;
    }
    return true;
}
// BP_OTPDIR_DATA_ENCODING_TYPE_RBIT8
bool x_otpdir_entry_appears_valid_rbit8(const BP_OTPDIR_ENTRY* entry) {
    if (entry->entry_type.encoding_type != BP_OTPDIR_DATA_ENCODING_TYPE_RBIT8) {
        PRINT_ERROR("Validating entry type as RBIT8, but type of the entry is 0x%02x", entry->entry_type.encoding_type);
        return false;
    }
    if (entry->rbit8_data.row_count == 0u) {
        PRINT_ERROR("Validating entry type as RAW, but row count is zero");
        return false;
    }
    if (!x_otpdir_is_valid_user_content_row_range(entry->rbit8_data.start_row, entry->rbit8_data.row_count)) {
        return false;
    }
    if ((entry->rbit8_data.row_count % 8u) != 0u) {
        PRINT_ERROR("Validating entry type as RBIT8, but row count (%03x) is not a multiple of 8", entry->rbit8_data.row_count);
        return false;
    }
    return true;
}
// BP_OTPDIR_DATA_ENCODING_TYPE_ECC and BP_OTPDIR_DATA_ENCODING_TYPE_ECC_ASCII_STRING
bool x_otpdir_entry_appears_valid_ecc(const BP_OTPDIR_ENTRY* entry) {
    if (entry->entry_type.encoding_type != BP_OTPDIR_DATA_ENCODING_TYPE_ECC) {
        PRINT_ERROR("Validating entry type as ECC, but type of the entry is 0x%02x", entry->entry_type.encoding_type);
        return false;
    }
    if (entry->ecc_data.byte_count == 0u) {
        PRINT_ERROR("Validating entry type as ECC, but byte count is zero");
        return false;
    }
    uint16_t row_count = entry->ecc_data.byte_count / 2u + (entry->ecc_data.byte_count % 2u) ? 1 : 0;
    if (!x_otpdir_is_valid_user_content_row_range(entry->ecc_data.start_row, (entry->ecc_data.byte_count + 1u) / 2u)) {
        return false;
    }
    return true;
}
// BP_OTPDIR_DATA_ENCODING_TYPE_EMBEDED_IN_DIRENTRY
bool x_otpdir_entry_appears_valid_embedded(const BP_OTPDIR_ENTRY* entry) {
    if (entry->entry_type.encoding_type != BP_OTPDIR_DATA_ENCODING_TYPE_EMBEDED_IN_DIRENTRY) {
        PRINT_ERROR("Validating entry type as EMBEDED_IN_DIRENTRY, but type of the entry is 0x%02x", entry->entry_type.encoding_type);
        return false;
    }
    // No further validation solely from encoding type is possible.
    return true;
}
#pragma endregion // OTP Directory Data Encoding Type Validation functions



// This function will read the entry at the given OTP row.
// If the entry is not readable in RAW form, then it will
// skip the current entry and read the next one.
static void x_otp_read_and_validate_direntry(uint16_t direntry_otp_row, XOTP_DIRENTRY_ITERATOR_STATE* out_state) {
    bool failure = false;
    bool should_try_next_row_if_not_validated = false;
    if (direntry_otp_row < xFIRST_USER_CONTENT_ROW) {
        failure = true;
    } else if (direntry_otp_row >= (xFIRST_USER_CONTENT_ROW + xUSER_CONTENT_ROW_COUNT)) {
        failure = true;
    }

    BP_OTPDIR_ENTRY entry;
    memset(&entry, 0, sizeof(BP_OTPDIR_ENTRY));
 
    // read the entry
    if (!failure) {
        if (!bp_otp_read_ecc_data(direntry_otp_row, &entry, sizeof(BP_OTPDIR_ENTRY))) {
            // TODO: Want to skip entries that are not readable; Have to read as RAW to
            //       distinguish unreadable vs. not encoded with ECC data.
            failure = true;
            should_try_next_row_if_not_validated = true;
        }
    }

    // First thing is to validate the CRC16
    if (!failure) {
        uint16_t crc16 = crc16_calculate(&entry, sizeof(BP_OTPDIR_ENTRY) - 2);
        if (crc16 != entry.crc16) {
            failure = true;
        }
    }

    // Next, perform entry-type-specific validation
    if (!failure) {
        if (entry.entry_type.encoding_type == BP_OTPDIR_DATA_ENCODING_TYPE_NONE) {
            if (!x_otpdir_entry_appears_valid_none(&entry)) {
                failure = true;
            }
            // if all the data is zero, this is the end of the directory (which is same as a failure)
            if ((entry.as_uint16[0] == 0u) &&
                (entry.as_uint16[1] == 0u) &&
                (entry.as_uint16[2] == 0u) &&
                (entry.as_uint16[3] == 0u)) {
                failure = true;
            }
        } else if (entry.entry_type.encoding_type == BP_OTPDIR_DATA_ENCODING_TYPE_RAW) {
            if (!x_otpdir_entry_appears_valid_raw(&entry)) {
                failure = true;
            }
        } else if (entry.entry_type.encoding_type == BP_OTPDIR_DATA_ENCODING_TYPE_BYTE3X) {
            if (!x_otpdir_entry_appears_valid_byte3x(&entry)) {
                failure = true;
            }
        } else if (entry.entry_type.encoding_type == BP_OTPDIR_DATA_ENCODING_TYPE_RBIT3) {
            if (!x_otpdir_entry_appears_valid_rbit3(&entry)) {
                failure = true;
            }
        } else if (entry.entry_type.encoding_type == BP_OTPDIR_DATA_ENCODING_TYPE_RBIT8) {
            if (!x_otpdir_entry_appears_valid_rbit8(&entry)) {
                failure = true;
            }
        } else if (
            (entry.entry_type.encoding_type == BP_OTPDIR_DATA_ENCODING_TYPE_ECC) ||
            (entry.entry_type.encoding_type == BP_OTPDIR_DATA_ENCODING_TYPE_ECC_ASCII_STRING)) {
            if (!x_otpdir_entry_appears_valid_ecc(&entry)) {
                failure = true;
            }
        } else if (entry.entry_type.encoding_type == BP_OTPDIR_DATA_ENCODING_TYPE_EMBEDED_IN_DIRENTRY) {
            if (!x_otpdir_entry_appears_valid_embedded(&entry)) {
                failure = true;
            }
        } else {
            PRINT_WARNING("Unknown OTPDIR entry encoding type: 0x%02x, full data %04x %04x %04x %04x",
                entry.entry_type.encoding_type,
                entry.as_uint16[0], entry.as_uint16[1], entry.as_uint16[2], entry.as_uint16[3]
            );
            // unknown entry type
            failure = true;
        }
    }

    // OK, either failure, or the entry itself seems reasonable.  Update the state accordingly.
    if (failure) {
        memset(out_state, 0, sizeof(XOTP_DIRENTRY_ITERATOR_STATE));
        out_state->should_try_next_row_if_not_validated = should_try_next_row_if_not_validated;
    } else {
        memcpy(&out_state->current_entry, &entry, sizeof(BP_OTPDIR_ENTRY));
        out_state->current_otp_row_start = direntry_otp_row;
        out_state->entry_validated = true;
    }
    return;
}

// NOTE: there is a distinct iterator tracked for each core.
static void x_otp_direntry_reset_directory_iterator() {
    XOTP_DIRENTRY_ITERATOR_STATE* state = &x_current_directory_entry[ get_core_num() ];
    memset(state, 0, sizeof(XOTP_DIRENTRY_ITERATOR_STATE));
    uint16_t row = BP_OTPDIR_ENTRY_START_ROW;
    do {
        x_otp_read_and_validate_direntry(row, state);
        row -= xROWS_PER_DIRENTRY;
    } while (!state->entry_validated && state->should_try_next_row_if_not_validated);
    return;
}

// For the "current" directory entry, what operations are needed?
// 1. Get the current type (returns pointer to one of the predefined types)
// 2. Get size of buffer required for corresponding data
// 3. Read the data into a caller-supplied buffer
static const BP_OTPDIR_ENTRY_TYPE x_otp_direntry_get_current_type(void) {
    XOTP_DIRENTRY_ITERATOR_STATE* state = &x_current_directory_entry[ get_core_num() ];
    if (!state->entry_validated) {
        return BP_OTPDIR_ENTRY_TYPE_END;
    }
    //return state->current_entry.entry_type;
    if (state->current_entry.entry_type.as_uint16_t == BP_OTPDIR_ENTRY_TYPE_END.as_uint16_t) {
        return BP_OTPDIR_ENTRY_TYPE_END;
    } else if (state->current_entry.entry_type.as_uint16_t == BP_OTPDIR_ENTRY_TYPE_USB_WHITELABEL.as_uint16_t) {
        return BP_OTPDIR_ENTRY_TYPE_USB_WHITELABEL;
    } else if (state->current_entry.entry_type.as_uint16_t == BP_OTPDIR_ENTRY_TYPE_BP_CERTIFICATE.as_uint16_t) {
        return BP_OTPDIR_ENTRY_TYPE_BP_CERTIFICATE;
    }
    // The entry validated, so maybe it's a new entry type?
    PRINT_WARNING("Unknown OTPDIR entry type: 0x%04x ... returning the type anyways as it passed validation", state->current_entry.entry_type.as_uint16_t);
    return state->current_entry.entry_type;
}

static size_t x_otp_direntry_get_current_buffer_size_required(void) {

    XOTP_DIRENTRY_ITERATOR_STATE* state = &x_current_directory_entry[ get_core_num() ];
    size_t result = 0u;

    if (!state->entry_validated) {
        result = 0u;
    } else if (state->current_entry.entry_type.encoding_type == BP_OTPDIR_DATA_ENCODING_TYPE_NONE) {
        result = 0u;
    } else if (state->current_entry.entry_type.encoding_type == BP_OTPDIR_DATA_ENCODING_TYPE_RAW) {
        result = state->current_entry.raw_data.row_count * sizeof(uint32_t); // 32-bits per row, of which 24 bits contain the data
    } else if (state->current_entry.entry_type.encoding_type == BP_OTPDIR_DATA_ENCODING_TYPE_BYTE3X) {
        result = state->current_entry.byte3x_data.row_count;
    } else if (state->current_entry.entry_type.encoding_type == BP_OTPDIR_DATA_ENCODING_TYPE_RBIT3) {
        result = (state->current_entry.rbit3_data.row_count / 3u) * sizeof(uint32_t); // 32-bits per 3 rows, of which 24 bits contain the data
    } else if (state->current_entry.entry_type.encoding_type == BP_OTPDIR_DATA_ENCODING_TYPE_RBIT8) {
        result = (state->current_entry.rbit8_data.row_count / 8u) * sizeof(uint32_t); // 32-bits per 8 rows, of which 24 bits contain the data
    } else if (state->current_entry.entry_type.encoding_type == BP_OTPDIR_DATA_ENCODING_TYPE_ECC) {
        result = state->current_entry.ecc_data.byte_count;
    } else if (state->current_entry.entry_type.encoding_type == BP_OTPDIR_DATA_ENCODING_TYPE_ECC_ASCII_STRING) {
        result = state->current_entry.ecc_data.byte_count;
    } else if (state->current_entry.entry_type.encoding_type == BP_OTPDIR_DATA_ENCODING_TYPE_EMBEDED_IN_DIRENTRY) {
        result = sizeof(uint32_t);
    } else {
        // ERROR! Unknown entry type!
        PRINT_FATAL("Unknown OTPDIR entry encoding type 0x%02x was marked as validated?  OTP Row %03x  full data %04x %04x %04x %04x",
            state->current_entry.entry_type.encoding_type,
            state->current_otp_row_start,
            state->current_entry.as_uint16[0], state->current_entry.as_uint16[1], state->current_entry.as_uint16[2], state->current_entry.as_uint16[3]
        );
        result = 0u;
    }
    return result;
}

#warning "Still need to write API to fill caller-supplied buffer..."
#warning "Stilll need an aPI to move to teh next entry..."

// Returns true when the next directory entry was found.
// Returns false when:
// * An explicit "sealed" directory entry is found (no more entries allowed to be added)
// * An all-zero (blank) entry is found
// * An entry with invalid CRC16 is found
// Entries which cannot be read due to ECC errors are silently skipped.
// Thus, if programming fails, use RAW mode to write data that has ECC errors.








/// All code above this point are the static helper functions / implementation details.
/// Only the below are the public API functions.


void bp_otpdir_reset_iterator(void) {
    x_otp_direntry_reset_directory_iterator();
    return;
}




