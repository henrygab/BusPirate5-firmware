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

#pragma region    // BP_OTP_DATA_ENCODING_TYPE
// TODO: REQUIRES --std:c23 or --std:gcc23 -- Fix these to be constexpr   
// TODO: REQUIRES --std:c23 or --std:gcc23 -- Fix initialization to use above constexpr structs

struct _BP_OTP_DATA_ENCODING_TYPE { uint8_t value; };
enum {
    e_BP_OTP_DATA_ENCODING_TYPE_UNUSED = 0x00,
    e_BP_OTP_DATA_ENCODING_TYPE_RAW = 0x01,
    e_BP_OTP_DATA_ENCODING_TYPE_ECC = 0x02,
    e_BP_OTP_DATA_ENCODING_TYPE_BYTE3X = 0x03,
    e_BP_OTP_DATA_ENCODING_TYPE_RBIT3 = 0x04,
    e_BP_OTP_DATA_ENCODING_TYPE_RBIT8 = 0x05,
    e_BP_OTP_DATA_ENCODING_TYPE_ECC_ASCII_STRING = 0x06,
    e_BP_OTP_DATA_ENCODING_TYPE_EMBEDED_IN_DIRENTRY = 0x07,
};

// clang-format off
// No data is stored in this row.  Used to allow unwritten space to later be used to appended to the directory entries
//C23 constexpr    BP_OTP_DATA_ENCODING_TYPE       v_BP_OTP_DATA_ENCODING_TYPE_UNUSED = { .value = 0x00 };
static const BP_OTP_DATA_ENCODING_TYPE       v_BP_OTP_DATA_ENCODING_TYPE_UNUSED = { .value = e_BP_OTP_DATA_ENCODING_TYPE_UNUSED };
const        BP_OTP_DATA_ENCODING_TYPE * const BP_OTP_DATA_ENCODING_TYPE_UNUSED = &v_BP_OTP_DATA_ENCODING_TYPE_UNUSED;
// Single row, 24-bits of raw data (returned as a 32-bit value), no error correction or detection
// Note that multiple rows do NOT pack the data ... each row takes 4 bytes.
//C23 constexpr    BP_OTP_DATA_ENCODING_TYPE       v_BP_OTP_DATA_ENCODING_TYPE_RAW = { .value = 0x01 };
static const BP_OTP_DATA_ENCODING_TYPE       v_BP_OTP_DATA_ENCODING_TYPE_RAW = { .value = e_BP_OTP_DATA_ENCODING_TYPE_RAW };
const        BP_OTP_DATA_ENCODING_TYPE * const BP_OTP_DATA_ENCODING_TYPE_RAW = &v_BP_OTP_DATA_ENCODING_TYPE_RAW;
// Single row, 16-bits of data, ECC detects 2-bit errors, corrects 1-bit errors
// and BRBP allows writing even where a row has a single bit already set to 1
// (even if stored value would normally store zero for that bit).
//C23 constexpr    BP_OTP_DATA_ENCODING_TYPE       v_BP_OTP_DATA_ENCODING_TYPE_ECC = { .value = 0x02 };
static const BP_OTP_DATA_ENCODING_TYPE       v_BP_OTP_DATA_ENCODING_TYPE_ECC = { .value = e_BP_OTP_DATA_ENCODING_TYPE_ECC };
const        BP_OTP_DATA_ENCODING_TYPE * const BP_OTP_DATA_ENCODING_TYPE_ECC = &v_BP_OTP_DATA_ENCODING_TYPE_ECC;
// Single row, 8-bits of data, stored triple-redundant.
// Majority voting (2-of-3) is applied for each bit independently.
//C23 constexpr    BP_OTP_DATA_ENCODING_TYPE       v_BP_OTP_DATA_ENCODING_TYPE_BYTE3X = { .value = 0x03 };
static const BP_OTP_DATA_ENCODING_TYPE       v_BP_OTP_DATA_ENCODING_TYPE_BYTE3X = { .value = e_BP_OTP_DATA_ENCODING_TYPE_BYTE3X };
const        BP_OTP_DATA_ENCODING_TYPE * const BP_OTP_DATA_ENCODING_TYPE_BYTE3X = &v_BP_OTP_DATA_ENCODING_TYPE_BYTE3X;
// Identical data stored in three consecutive OTP rows.
// Majority voting (2-of-3) is applied for each bit independently.
//C23 constexpr    BP_OTP_DATA_ENCODING_TYPE       v_BP_OTP_DATA_ENCODING_TYPE_RBIT3 = { .value = 0x04 };
static const BP_OTP_DATA_ENCODING_TYPE       v_BP_OTP_DATA_ENCODING_TYPE_RBIT3 = { .value = e_BP_OTP_DATA_ENCODING_TYPE_RBIT3 };
const        BP_OTP_DATA_ENCODING_TYPE * const BP_OTP_DATA_ENCODING_TYPE_RBIT3 = &v_BP_OTP_DATA_ENCODING_TYPE_RBIT3;
// Identical data stored in eight consecutive OTP rows.
// Special voting is applied for each bit independently:
// If 3 or more rows have the bit set, then that bit is considered set.
// This is used ONLY for the critical boot rows.
//C23 constexpr    BP_OTP_DATA_ENCODING_TYPE       v_BP_OTP_DATA_ENCODING_TYPE_RBIT8 = { .value = 0x05 };
static const BP_OTP_DATA_ENCODING_TYPE       v_BP_OTP_DATA_ENCODING_TYPE_RBIT8 = { .value = e_BP_OTP_DATA_ENCODING_TYPE_RBIT8 };
const        BP_OTP_DATA_ENCODING_TYPE * const BP_OTP_DATA_ENCODING_TYPE_RBIT8 = &v_BP_OTP_DATA_ENCODING_TYPE_RBIT8;
// Identical to BP_OTP_DATA_ENCODING_TYPE_ECC, with the added requirement
// that the record's length includes at least one trailing zero byte.
// This is useful for ASCII / UTF-8 strings.
//C23 constexpr    BP_OTP_DATA_ENCODING_TYPE       v_BP_OTP_DATA_ENCODING_TYPE_ECC_ASCII_STRING  = { .value = 0x06 };
static const BP_OTP_DATA_ENCODING_TYPE       v_BP_OTP_DATA_ENCODING_TYPE_ECC_ASCII_STRING  = { .value = e_BP_OTP_DATA_ENCODING_TYPE_ECC_ASCII_STRING };
const        BP_OTP_DATA_ENCODING_TYPE * const BP_OTP_DATA_ENCODING_TYPE_ECC_ASCII_STRING = &v_BP_OTP_DATA_ENCODING_TYPE_ECC_ASCII_STRING;
// Value is encoded directly within the directory entry as an (up to) 32-bit value.
// The least significant 16 bits are stored within the `start_row` field.
// The most significant 16 bits are stored within the `byte_count` field.
//C23 constexpr    BP_OTP_DATA_ENCODING_TYPE       v_BP_OTP_DATA_ENCODING_TYPE_EMBEDED_IN_DIRENTRY = { .value = 0x07 };
static const BP_OTP_DATA_ENCODING_TYPE       v_BP_OTP_DATA_ENCODING_TYPE_EMBEDED_IN_DIRENTRY = { .value = e_BP_OTP_DATA_ENCODING_TYPE_EMBEDED_IN_DIRENTRY };
const        BP_OTP_DATA_ENCODING_TYPE * const BP_OTP_DATA_ENCODING_TYPE_EMBEDED_IN_DIRENTRY = &v_BP_OTP_DATA_ENCODING_TYPE_EMBEDED_IN_DIRENTRY;

// clang-format on
#pragma endregion // BP_OTP_DATA_ENCODING_TYPE


#pragma region    // BP_OTP_DIRENTRY_TYPE
// TODO: REQUIRES --std:c23 or --std:gcc23 -- Fix these to be constexpr   
// TODO: REQUIRES --std:c23 or --std:gcc23 -- Fix initialization to use above constexpr structs

struct _BP_OTP_DIRENTRY_TYPE {
    union {
        uint16_t as_uint16_t;
        struct {
            uint16_t id            : 8; // can be extended to 12-bits if ever exceed 255 types for the given encoding type
            uint16_t must_be_zero  : 4;
            uint16_t encoding_type : 4; // e.g., ECC, ECC_String, BYTE3X, RBIT3, RBIT8, RAW
        };
    };
};
static_assert(sizeof(BP_OTP_DIRENTRY_TYPE) == sizeof(uint16_t));

// This is because C23 appears to be needed for constexpr / const evaluation of the below structures?
//
// End of the directory entry list.
// This allows for a directy to be appended to, as an all-zero entry can still be written (if not locked via page locks or the like).
//C23 constexpr BP_OTP_DIRENTRY_TYPE       v_BP_OTP_DIRENTRY_TYPE_END = { .encoding_type = v_BP_OTP_DATA_ENCODING_TYPE_RAW.value, id = 0x0000 };
static const BP_OTP_DIRENTRY_TYPE       v_BP_OTP_DIRENTRY_TYPE_END = {  .id = 0x0000, .encoding_type = e_BP_OTP_DATA_ENCODING_TYPE_UNUSED };
const        BP_OTP_DIRENTRY_TYPE * const BP_OTP_DIRENTRY_TYPE_END = &v_BP_OTP_DIRENTRY_TYPE_END;
// USB Whitelabel data, including the whitelabel structure itself and all the counted strings that it uses.
// Because the strings all have a starting offset <= 256 rows, and the longest string is 255 rows,
// this entry type's size should never exceed 511 rows.
// All data for this entry is stored using ECC encoding.
//C23 constexpr BP_OTP_DIRENTRY_TYPE       v_BP_OTP_DIRENTRY_TYPE_USB_WHITELABEL = { .encoding_type = BP_OTP_DATA_ENCODING_TYPE_ECC.value, .id = 0x0001 };
static const BP_OTP_DIRENTRY_TYPE       v_BP_OTP_DIRENTRY_TYPE_USB_WHITELABEL = { .id = 0x0001, .encoding_type = e_BP_OTP_DATA_ENCODING_TYPE_ECC };
const        BP_OTP_DIRENTRY_TYPE * const BP_OTP_DIRENTRY_TYPE_USB_WHITELABEL = &v_BP_OTP_DIRENTRY_TYPE_USB_WHITELABEL;

// BusPirate Provenance Certificate
// All data for this entry is stored using ECC encoding.
//C23 constexpr BP_OTP_DIRENTRY_TYPE       v_BP_OTP_DIRENTRY_TYPE_BP_CERTIFICATE = { .encoding_type = BP_OTP_DATA_ENCODING_TYPE_ECC.value, .id = 0x0002 };
static const BP_OTP_DIRENTRY_TYPE       v_BP_OTP_DIRENTRY_TYPE_BP_CERTIFICATE = { .id = 0x0002, .encoding_type = e_BP_OTP_DATA_ENCODING_TYPE_ECC };
const        BP_OTP_DIRENTRY_TYPE * const BP_OTP_DIRENTRY_TYPE_BP_CERTIFICATE = &v_BP_OTP_DIRENTRY_TYPE_BP_CERTIFICATE;


#pragma endregion // BP_OTP_DIRENTRY_TYPE


// this is a detail that should remain hidden within bp_otp_direntry.c
typedef struct _BP_OTP_DIRENTRY {
    union {
        uint16_t as_uint16[4];
        struct {
            BP_OTP_DIRENTRY_TYPE entry_type;

            uint16_t             start_row;  // For entry types pointing to data in OTP rows, this is the row storing the first byte(s) of that data.
            uint16_t             byte_count; // For entry types pointing to data in OTP rows, this indicates the count of bytes consecutively stored.
            
            uint16_t             crc16;
        };
    };
} BP_OTP_DIRENTRY;
#define xROWS_PER_DIRENTRY 4u


#if RPI_PLATFORM == RP2350
    #define xCORE_COUNT 2u
    #define xFIRST_USER_CONTENT_ROW ((uint16_t)0x0C0u)
    #define xUSER_CONTENT_ROW_COUNT ((uint16_t)(0xF40u - xFIRST_USER_CONTENT_ROW))
#else
    #error "unsupported platform ... may have different count of cores"
#endif
#define BP_OTP_DIRENTRY_START_ROW ((uint16_t)(xFIRST_USER_CONTENT_ROW + xUSER_CONTENT_ROW_COUNT - xROWS_PER_DIRENTRY))


// define this based on zero-initialized data representing fresh, but INVALID state.
typedef struct _XOTP_DIRENTRY_ITERATOR_STATE {
    uint16_t current_otp_row_start; // zero is not a valid OTP row for an OTP_DIRENTRY item
    BP_OTP_DIRENTRY current_entry;  // all-zero data is indicative of end-of-directory
    bool     entry_validated;       // zero means entry not yet validated (entry type, crc, start row, data length)
    bool     should_try_next_row_if_not_validated; // set to true when read fails with ECC error
} XOTP_DIRENTRY_ITERATOR_STATE;

// One "current" OTP_DIRENTRY location is stored for each core.
// This avoids the need for cores to synchronize their read-only operations
// on the OTP directory.
static XOTP_DIRENTRY_ITERATOR_STATE x_current_directory_entry[xCORE_COUNT] = {};

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

static void x_otp_read_and_validate_direntry(uint16_t direntry_otp_row, XOTP_DIRENTRY_ITERATOR_STATE* out_state) {
    bool failure = false;
    bool should_try_next_row_if_not_validated = false;
    if (direntry_otp_row < xFIRST_USER_CONTENT_ROW) {
        failure = true;
    } else if (direntry_otp_row >= (xFIRST_USER_CONTENT_ROW + xUSER_CONTENT_ROW_COUNT)) {
        failure = true;
    }

    BP_OTP_DIRENTRY entry;
    memset(&entry, 0, sizeof(BP_OTP_DIRENTRY));
 
    // read the entry
    if (!failure) {
        if (!bp_otp_read_ecc_data(direntry_otp_row, &entry, sizeof(BP_OTP_DIRENTRY))) {
            // TODO: Do not fail altogether on ECC errors.  Instead, skip the entry.
            failure = true;
            should_try_next_row_if_not_validated = true;
        }
    }

    // First thing is to validate the CRC16
    if (!failure) {
        uint16_t crc16 = crc16_calculate(&entry, sizeof(BP_OTP_DIRENTRY) - 2);
        if (crc16 != entry.crc16) {
            failure = true;
        }
    }

    // Next, perform entry-type-specific validation
    if (!failure) {
        if (entry.entry_type.encoding_type == e_BP_OTP_DATA_ENCODING_TYPE_UNUSED) {
            // if all the data is zero, this is the end of the directory
            if ((entry.as_uint16[0] == 0u) &&
                (entry.as_uint16[1] == 0u) &&
                (entry.as_uint16[2] == 0u) &&
                (entry.as_uint16[3] == 0u)) {
                failure = true;
            }
        } else if (
            (entry.entry_type.encoding_type == e_BP_OTP_DATA_ENCODING_TYPE_RAW) ||
            (entry.entry_type.encoding_type == e_BP_OTP_DATA_ENCODING_TYPE_ECC) ||
            (entry.entry_type.encoding_type == e_BP_OTP_DATA_ENCODING_TYPE_BYTE3X) ||
            (entry.entry_type.encoding_type == e_BP_OTP_DATA_ENCODING_TYPE_RBIT3)  ||
            (entry.entry_type.encoding_type == e_BP_OTP_DATA_ENCODING_TYPE_RBIT8)  ||
            (entry.entry_type.encoding_type == e_BP_OTP_DATA_ENCODING_TYPE_ECC_ASCII_STRING)) {

            // validate the start row is within the user data area
            if (entry.start_row < xFIRST_USER_CONTENT_ROW) {
                failure = true;
            } else if (entry.start_row >= (xFIRST_USER_CONTENT_ROW + xUSER_CONTENT_ROW_COUNT)) {
                failure = true;
            }
            // for that start row, what's the maximum remaining byte count?
            uint16_t remaining_rows = xUSER_CONTENT_ROW_COUNT - (entry.start_row - xFIRST_USER_CONTENT_ROW);
            if (entry.byte_count > (xUSER_CONTENT_ROW_COUNT*2u)) {
                failure = true;
            }
        } else if (entry.entry_type.encoding_type == e_BP_OTP_DATA_ENCODING_TYPE_EMBEDED_IN_DIRENTRY) {
            // no validation beyond the CRC16 is possible
        } else {
            // unknown entry type
            failure = true;
        }
    }

    // OK, either failure, or the entry is validated.  Update the state accordingly.
    if (failure) {
        memset(out_state, 0, sizeof(XOTP_DIRENTRY_ITERATOR_STATE));
        out_state->should_try_next_row_if_not_validated = should_try_next_row_if_not_validated;
    } else {
        memcpy(&out_state->current_entry, &entry, sizeof(BP_OTP_DIRENTRY));
        out_state->current_otp_row_start = direntry_otp_row;
        out_state->entry_validated = true;
    }
    return;
}

// NOTE: there is a distinct iterator tracked for each core.
static void x_otp_direntry_reset_directory_iterator() {
    XOTP_DIRENTRY_ITERATOR_STATE* state = &x_current_directory_entry[ get_core_num() ];
    memset(state, 0, sizeof(XOTP_DIRENTRY_ITERATOR_STATE));
    uint16_t row = BP_OTP_DIRENTRY_START_ROW;
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
static const BP_OTP_DIRENTRY_TYPE * x_otp_direntry_get_current_type(void) {
    XOTP_DIRENTRY_ITERATOR_STATE* state = &x_current_directory_entry[ get_core_num() ];
    if (!state->entry_validated) {
        return BP_OTP_DIRENTRY_TYPE_END;
    }
    if (state->current_entry.entry_type.as_uint16_t == BP_OTP_DIRENTRY_TYPE_END->as_uint16_t) {
        return BP_OTP_DIRENTRY_TYPE_END;
    } else if (state->current_entry.entry_type.as_uint16_t == BP_OTP_DIRENTRY_TYPE_USB_WHITELABEL->as_uint16_t) {
        return BP_OTP_DIRENTRY_TYPE_USB_WHITELABEL;
    } else if (state->current_entry.entry_type.as_uint16_t == BP_OTP_DIRENTRY_TYPE_BP_CERTIFICATE->as_uint16_t) {
        return BP_OTP_DIRENTRY_TYPE_BP_CERTIFICATE;
    } else {
        // ERROR! Unknown entry type!
        return BP_OTP_DIRENTRY_TYPE_END;
    }
    return BP_OTP_DIRENTRY_TYPE_END;
}

static size_t x_otp_direntry_get_current_buffer_size_required(void) {

    XOTP_DIRENTRY_ITERATOR_STATE* state = &x_current_directory_entry[ get_core_num() ];
    size_t result = 0u;

    if (!state->entry_validated) {
        result = 0u;
    } else if (state->current_entry.entry_type.encoding_type == e_BP_OTP_DATA_ENCODING_TYPE_UNUSED) {
        result = 0u;
    } else if (
        (state->current_entry.entry_type.encoding_type == e_BP_OTP_DATA_ENCODING_TYPE_RAW)    ||
        (state->current_entry.entry_type.encoding_type == e_BP_OTP_DATA_ENCODING_TYPE_ECC)    ||
        (state->current_entry.entry_type.encoding_type == e_BP_OTP_DATA_ENCODING_TYPE_BYTE3X) ||
        (state->current_entry.entry_type.encoding_type == e_BP_OTP_DATA_ENCODING_TYPE_RBIT3)  ||
        (state->current_entry.entry_type.encoding_type == e_BP_OTP_DATA_ENCODING_TYPE_RBIT8)  ||
        (state->current_entry.entry_type.encoding_type == e_BP_OTP_DATA_ENCODING_TYPE_ECC_ASCII_STRING)) {
        result = state->current_entry.byte_count;
    } else if (state->current_entry.entry_type.encoding_type == e_BP_OTP_DATA_ENCODING_TYPE_EMBEDED_IN_DIRENTRY) {
        result = 4u;
    } else {
        // ERROR! Unknown entry type!
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

