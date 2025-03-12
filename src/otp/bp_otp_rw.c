#define BP_DEBUG_OVERRIDE_DEFAULT_CATEGORY BP_DEBUG_CAT_OTP

#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>
#include <assert.h>
#include "../pirate.h"
#include "bp_otp.h"
#include "pico.h"
#include <boot/bootrom_constants.h>
#include "pico/bootrom.h"
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

// decide where to single-step through a given process ... controlled via RTT (no USB connection required)
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

// don't want to use that difficult-to-parse API in many places....
static int write_raw_wrapper(uint16_t starting_row, const void* buffer, size_t buffer_size) {
    otp_cmd_t cmd;
    cmd.flags = starting_row;
    cmd.flags |= OTP_CMD_WRITE_BITS;
    return rom_func_otp_access((uint8_t*)buffer, buffer_size, cmd);
}
static int read_raw_wrapper(uint16_t starting_row, void* buffer, size_t buffer_size) {
    otp_cmd_t cmd;
    cmd.flags = starting_row;
    return rom_func_otp_access((uint8_t*)buffer, buffer_size, cmd);
}

// RP2350 OTP storage is strongly recommended to use some form of
// error correction.  Most rows will use ECC, but three other forms exist:
// (1) 2-of-3 voting of a single byte in a single row
// (2) 2-of-3 voting of 24-bits across three consecutive OTP rows (RBIT-3)
// (3) 3-of-8 voting of 24-bits across eight consecutive OTP rows (RBIT-8)
//
// A note on RBIT-8:
// RBIT-8 is used _ONLY_ for CRIT0 and CRIT1. It works similarly to RBIT-3,
// except that each bit is considered set if at least three (3) of the eight
// rows have that bit set.  Thus, it's not a simple majority vote, instead
// tending to favor considering bits as set.
// 
static bool read_single_otp_ecc_row(uint16_t row, uint16_t * data_out) {
    uint32_t existing_raw_data;
    *data_out = 0xFFFFu;
    int r = read_raw_wrapper(row, &existing_raw_data, sizeof(existing_raw_data));
    if (BOOTROM_OK != r) {
        PRINT_ERROR("OTP_RW Error: Failed to read OTP raw row %03x: %d (0x%x)\n", row, r, r);
        return false;
    }
    uint32_t decode_result = bp_otp_decode_raw(existing_raw_data);
    if ((decode_result & 0xFF000000u) != 0u) {
        PRINT_ERROR("OTP_RW Error: Failed to decode OTP row %03x value 0x%06x: Result 0x%08x\n", row, existing_raw_data, decode_result);
        return false;
    }
    *data_out = (uint16_t)decode_result;
    return true;   
}
static bool write_single_otp_ecc_row(uint16_t row, uint16_t data) {

    // Allow writes to valid ECC encoded data, so long as it's possible to do so.
    // This means adjusting for BRBP bits that may are already be set to 1,
    // and then checking if there's a single-bit error that can be ignored.
    // If so, try to write the data, even if some bits are already set!

    // 1. Read the existing raw data
    BP_OTP_RAW_READ_RESULT existing_raw_data;
    int r;
    r = read_raw_wrapper(row, &existing_raw_data, sizeof(existing_raw_data));
    if (BOOTROM_OK != r) {
        PRINT_ERROR("OTP_RW Error: Failed to read OTP raw row %03x: %d (0x%x)\n", row, r, r);
        return false;
    }

    // 2. SUCCESS if existing raw data already encodes the value; Do not update the OTP row.
    //    TODO: Consider if existing data has a single bit flipped from one to zero ... could write
    //          the extra bit to try to reduce errors?
    uint32_t decode_result = bp_otp_decode_raw(existing_raw_data.as_uint32);
    if (((decode_result & 0xFF000000u) == 0u) && ((uint16_t)decode_result == data)) {
        // already written, nothing more to do for this row
        PRINT_VERBOSE("OTP_RW: Row %03x already has data 0x%04x .. not writing\n", row, data);
        return true;
    }

    // 3. Do we have to adjust the encoded data due to existing BRBP bits?  Determine data to write (or fail if not possible)
    uint32_t data_to_write;
    do {
        BP_OTP_RAW_READ_RESULT encoded_new_data      = { .as_uint32 = bp_otp_calculate_ecc(data) };
        BP_OTP_RAW_READ_RESULT encoded_new_data_brbp = { .as_uint32 = encoded_new_data.as_uint32 ^ 0x00FFFFFFu };
        
        // count how many bits would be flipped vs. the new data
        uint_fast8_t bits_to_flip      = __builtin_popcount(existing_raw_data.as_uint32 ^ encoded_new_data.as_uint32);
        uint_fast8_t bits_to_flip_brbp = __builtin_popcount(existing_raw_data.as_uint32 ^ encoded_new_data_brbp.as_uint32);

        if (bits_to_flip == 0u) {
            // Great!  Can write without adjusting for existing data.
            data_to_write = encoded_new_data.as_uint32;
        } else if (bits_to_flip_brbp == 0u) {
            // Great!  Can write without error by using BRBP bits.
            data_to_write = encoded_new_data_brbp.as_uint32;
        } else if (bits_to_flip == 1u) {
            // Can write, but will have an existing single-bit error in the data...
            data_to_write = encoded_new_data.as_uint32 | existing_raw_data.as_uint32;
        } else if (bits_to_flip_brbp == 1u) {
            // Can write, but will ahve to use BRBP bits, and there will still be a single-bit error in the data...
            data_to_write = encoded_new_data_brbp.as_uint32 | existing_raw_data.as_uint32;
        } else {
            // No way to write the data without multiple bit errors
            PRINT_ERROR("OTP_RW Error: Cannot write ECC OTP row %03x with data 0x%04x: Encoded 0x%06x / 0x%06x vs. Existing 0x%06x prevents writing\n",
                row, data, encoded_new_data.as_uint32, encoded_new_data_brbp.as_uint32, existing_raw_data.as_uint32
            );
            return false;
        }
    } while (0);

    // 4. write the encoded raw data
    r = write_raw_wrapper(row, &data_to_write, sizeof(data_to_write));
    if (BOOTROM_OK != r) {
        PRINT_ERROR("OTP_RW Error: Failed to write ECC OTP row %03x with data 0x%06x (ECC encoding of 0x%04x), error %d (0x%x)\n",
            row, data, data_to_write, data_to_write, r, r
        );
        return false;
    }

    // 5. And finally, verify the expected data is now readable from that OTP row
    uint16_t verify_data;
    if (!read_single_otp_ecc_row(row, &verify_data)) {
        PRINT_ERROR("OTP_RW Error: Failed to verify ECC OTP row %03x has data 0x%04x\n", row, data);
        return false;
    }

    // 6. New data was written and verified.  Success!
    return true;
}
static bool write_single_otp_raw_row(uint16_t row, uint32_t data) {
    uint32_t existing_data;
    int r;
    r = read_raw_wrapper(row, &existing_data, sizeof(existing_data));
    if (BOOTROM_OK != r) {
        PRINT_ERROR("OTP_RW Warn: Failed to read OTP raw row %03x: %d (0x%x)\n", row, r, r);
        return false;
    }
    if (existing_data == data) {
        // already written, nothing more to do for this row
        return true;
    }

    // Write will fail if any bit set to zero is actually set to one in the existing data
    // Detect this so can provide a clearer error message.
    uint32_t incompatible_bits = existing_data & ~data;
    if (incompatible_bits != 0u) {
        PRINT_ERROR("OTP_RW Warn: OTP row %03x cannot be written to %06x (existing data 0x%06x has incompatible bits at 0x%06x)\n",
            row, data, existing_data, incompatible_bits
        );
        return false;
    }

    // use the bootrom function to write the new raw data
    r = write_raw_wrapper(row, &data, sizeof(data));
    if (BOOTROM_OK != r) {
        PRINT_ERROR("OTP_RW Warn: Failed to write OTP raw row %03x: %d (0x%x)\n", row, r, r);
        return false;
    }

    // Verify the data was recorded ...
    r = read_raw_wrapper(row, &existing_data, sizeof(existing_data));
    if (BOOTROM_OK != r) {
        PRINT_ERROR("OTP_RW Warn: Failed to read OTP raw row %03x: %d (0x%x)\n", row, r, r);
        return false;
    }
    if (existing_data != data) {
        PRINT_ERROR("OTP_RW Warn: Failed to verify OTP raw row %03x: %d (0x%x) has data 0x%06x\n", row, r, r, data);
        return false;
    }
    return true;
}
static bool read_single_otp_value_N_of_M(uint16_t start_row, uint8_t N, uint8_t M, uint32_t* out_data) {
    *out_data = 0xFFFFFFFFu;

    // Support both RBIT3 and RBIT8
    if (N == 2 && M == 3) { }
    else if (N == 3 && M == 8) {}
    else {
        PRINT_ERROR("OTP_RW Error: Read OTP N-of-M: Unsupported N=%d, M=%d\n", N, M);
        return false;
    }
    uint32_t v[8u] = {0u}; // zero-initialize the array, sized for maximum supported `M`
    int      r[8u] = {0u}; // zero-initialized is BOOTROM_OK ... but no bits set in unused elements, so it's OK
    assert(M <= 8u);

    // Read each of the `M` rows
    for (size_t i = 0; i < M; ++i) {
        r[i] = read_raw_wrapper(start_row+i, &(v[i]), sizeof(uint32_t));
    }

    // Ensure at least `N` reads were successful, else return an error
    uint_fast8_t successful_reads = 0;
    for (size_t i = 0; i < M; ++i) {
        if (BOOTROM_OK == r[i]) {
            ++successful_reads;
        }
    }
    if (successful_reads < N) {
        PRINT_ERROR("OTP_RW Error: Read OTP N-of-M: rows 0x%03x to 0x%03x: only %d of %d reads successful ... failing\n", start_row, start_row+M-1, successful_reads, M);
        return false;
    }


    uint_fast8_t votes[24]; // one count for each potential bit to be set

    // Count the votes from the successful reads
    for (size_t i = 0; i < M; ++i) {
        // don't count any votes from failed reads
        if (BOOTROM_OK != r[i]) {
            continue;
        }
        // loop through each bit, add a vote if it's set
        uint32_t tmp = v[i];
        for (uint_fast8_t j = 0; j < 24u; ++j) {
            uint32_t mask = (1u << j);
            if ((tmp & mask) != 0u) {
                ++votes[j];
            }
        }
    }

    // Generate a result based on the votes
    uint32_t result = 0u;
    for (uint_fast8_t i = 0; i < 24u; ++i) {
        if (votes[i] >= N) {
            result |= (1u << i);
        }
    }
    // return the voted-upon result
    return false;
}
static bool write_single_otp_value_N_of_M(uint16_t start_row, uint8_t N, uint8_t M, uint32_t new_value) {

    // 1. read the old data
    PRINT_DEBUG("OTP_RW Debug: Write OTP 2-of-3: row 0x%03x\n", start_row); WAIT_FOR_KEY();

    uint32_t old_voted_bits;
    if (!read_single_otp_value_N_of_M(start_row, N, M, &old_voted_bits)) {
        PRINT_DEBUG("OTP_RW Debug: Failed to read %d-of-%d starting at row 0x%03x\n", N, M, start_row);
        return false;
    }

    // If any bits are already voted upon as set, there's no way to unset them.
    uint32_t incompatible_bits = old_voted_bits & ~new_value;
    if (incompatible_bits != 0u) {
        PRINT_ERROR("OTP_RW Error: Fail: Old voted-upon value 0x%06x has bits set that are not in the new value 0x%06x ---> 0x%06x\n",
            old_voted_bits, new_value, incompatible_bits
        );
        return false;
    }

    // 2. Read-Modify-Write each row individually, logically OR'ing the requested bits into the old value.
    //    This process allows for each individual row to have multiple bits set, even if not set in the new value.
    //    Because the N-of-M voting was successful, this will not degrade the error detection.
    //    Moreover, allow each individual write to fail ... final success is based on reading the new value.
    for (uint16_t i = 0; i < M; ++i) {
        uint32_t old_data;
        int r = read_raw_wrapper(start_row+i, &old_data, sizeof(old_data));
        if (BOOTROM_OK != r) {
            PRINT_WARNING("OTP_RW Warn: unable to read old bits for OTP %d-of-%d: row 0x%03x\n", N, M, start_row+i);
            continue; // to next OTP row, if any
        }
        if ((old_data & new_value) == new_value) {
            // no change needed ... not setting any new bit for this row
            PRINT_WARNING("OTP_RW Warn: skipping update to row 0x%03x: old value 0x%06x already has bits 0x%06x\n", start_row+i, old_data, new_value);
            continue; // to next OTP row, if any
        }

        uint32_t to_write = old_data | new_value;
        PRINT_DEBUG("OTP_RW Debug: updating row 0x%03x: 0x%06x --> 0x%06x\n", start_row+i, old_data, to_write); WAIT_FOR_KEY();
        r = write_raw_wrapper(start_row+i, &to_write, sizeof(to_write));
        if (BOOTROM_OK != r) {
            PRINT_ERROR("OTP_RW Error: Failed to write new bits for OTP %d-of-%d: row 0x%03x: 0x%06x --> 0x%06x\n", N, M, start_row+i, old_data, to_write);
            continue; // to next OTP row, if any
        }
        PRINT_DEBUG("OTP_RW Debug: Wrote new bits for OTP %d-of-%d: row 0x%03x: 0x%06x --> 0x%06x\n", N, M, start_row+i, old_data, to_write);
    }

    // 3. Verify the data was updated to the expected value
    uint32_t new_voted_bits;
    if (!read_single_otp_value_N_of_M(start_row, N, M, &new_voted_bits)) {
        PRINT_ERROR("OTP_RW Error: Failed to read agreed-upon new bits for OTP %d-of-%d starting at row 0x%03x\n", N, M, start_row);
        return false;
    }

    // 4. Verify the new data matches the requested value
    if (new_voted_bits != new_value) {
        PRINT_ERROR("OTP_RW Error: OTP %d-of-%d: starting at row 0x%03x: 0x%06x -> 0x%06x, but got 0x%06x\n",
            N, M, start_row,
            old_voted_bits, new_value, new_voted_bits
        );
        return false;
    }
    // print success message
    if (N == 2 && M == 3) {
        PRINT_DEBUG("OTP_RW Debug: Successfully update the RBIT3 (2-of-3 voting) rows\n");
    } else if (N == 3 && M == 8) {
        PRINT_DEBUG("OTP_RW Debug: Successfully update the RBIT8 (3-of-8 voting) rows\n");
    } else {
        PRINT_DEBUG("OTP_RW Debug: Successfully update the %d-of-%d voting rows\n", N, M);
    }
    return true;
}
static bool read_otp_byte_3x(uint16_t row, uint8_t* out_data) {
    *out_data = 0xFFu;

    // 1. read the data
    BP_OTP_RAW_READ_RESULT v;
    int r;
    r = read_raw_wrapper(row, &v.as_uint32, sizeof(uint32_t));
    if (BOOTROM_OK != r) {
        PRINT_ERROR("OTP_RW Error: Failed to read OTP byte 3x: row 0x%03x: %d (0x%x)\n", row, r, r);
        return false;
    }
    // use bit-by-bit majority voting
    uint8_t result = 0u;
    PRINT_DEBUG("OTP_RW Debug: Read OTP byte_3x row 0x%03x: (0x%02x, 0x%02x, 0x%02x)\n", row, v.as_bytes[0], v.as_bytes[1], v.as_bytes[2]);
    for (uint8_t mask = 0x80u; mask; mask >>= 1) {
        uint_fast8_t count = 0;
        if (v.as_bytes[0] & mask) { ++count; }
        if (v.as_bytes[1] & mask) { ++count; }
        if (v.as_bytes[2] & mask) { ++count; }
        if (count >= 2) {
            result |= mask;
        }
    }

    PRINT_DEBUG("OTP_RW Debug: Read OTP byte_3x row 0x%03x: Bit-by-bit voting result: 0x%02x\n", row, result);
    *out_data = result;
    return true;
}
static bool write_otp_byte_3x(uint16_t row, uint8_t new_value) {

    PRINT_DEBUG("OTP_RW Debug: Write OTP byte_3x: row 0x%03x\n", row); WAIT_FOR_KEY();

    // 1. read the old data as raw bits
    BP_OTP_RAW_READ_RESULT old_raw_data;
    int r;
    r = read_raw_wrapper(row, &old_raw_data, sizeof(old_raw_data));
    if (BOOTROM_OK != r) {
        PRINT_ERROR("OTP_RW Error: unable to read old bits for OTP byte_3x: row 0x%03x\n", row);
        return false;
    }

    // 2. Does the existing data have bits set that are zero in the new value? (fail ... can never unset those bits)
    uint8_t check_if_voted_set = ~new_value;
    for (uint_fast8_t i = 0; i < 8u; ++i) {
        uint8_t mask = (1u << i);
        if (check_if_voted_set & mask) {
            uint_fast8_t count = 0;
            if (old_raw_data.as_bytes[0] & mask) { ++count; }
            if (old_raw_data.as_bytes[1] & mask) { ++count; }
            if (old_raw_data.as_bytes[2] & mask) { ++count; }
            if (count >= 2) {
                // found a bit that is voted to be set, and thus cannot be unset
                PRINT_ERROR("OTP_RW Error: Attempt to byte_3x write row %03x to 0x%02x; Existing data 0x%06x bit %d votes as set, but is not set in new value\n",
                    row, new_value, old_raw_data.as_uint32, i
                );
                return false;
            }
        }
    }

    // 3. Does the existing data need any bits set that new_value has set?  If so, SUCCESS w/o writing.
    if (((old_raw_data.as_bytes[0] & new_value) == new_value) &&
        ((old_raw_data.as_bytes[1] & new_value) == new_value) &&
        ((old_raw_data.as_bytes[2] & new_value) == new_value) ) {
        // no write required, as the row already has all the bits set that we'd be trying to write
        PRINT_VERBOSE("OTP_RW: Write OTP byte_3x: Row %03x data 0x%06x already has all required bits set for 0x%02x ... not writing\n", row, old_raw_data.as_uint32, new_value);
        return true;
    }

    // 4. Logically OR the new_value bits into the existing data, and write the updated raw data.
    BP_OTP_RAW_READ_RESULT to_write = { .as_uint32 = old_raw_data.as_uint32 };
    to_write.as_bytes[0] |= new_value;
    to_write.as_bytes[1] |= new_value;
    to_write.as_bytes[2] |= new_value;
    PRINT_DEBUG("OTP_RW Debug: Write OTP byte_3x: updating row 0x%03x: 0x%06x --> 0x%06x\n", row, old_raw_data.as_uint32, to_write.as_uint32); WAIT_FOR_KEY();
    r = write_raw_wrapper(row, &to_write, sizeof(to_write));
    if (BOOTROM_OK != r) {
        PRINT_ERROR("OTP_RW Error: Failed to write new bits for byte_3x: row 0x%03x: 0x%06x --> 0x%06x\n", row, old_raw_data.as_uint32, to_write.as_uint32);
        return false;
    }

    // 5. Verify the newly written OTP row now contains a value that votes to the new value.
    uint8_t new_voted_bits;
    if (!read_otp_byte_3x(row, &new_voted_bits)) {
        PRINT_ERROR("OTP_RW Error: Failed to read agreed-upon new bits for OTP byte_3x: row 0x%03x\n", row);
        return false;
    } else if (new_voted_bits != new_value) {
        PRINT_ERROR("OTP_RW Error: OTP byte_3x: row 0x%03x: 0x%02x (0x%06x -> 0x%06x), but got 0x%02x\n",
            row, new_value, old_raw_data.as_uint32, to_write.as_uint32, new_voted_bits
        );
        return false;
    }

    return true;
}

/// All code above this point are the static helper functions / implementation details.
/// Only the below are the public API functions.


//#define BP_USE_VIRTUALIZED_OTP
#if defined(BP_USE_VIRTUALIZED_OTP)

// Enable "virtualized" OTP ... useful for testing.
// 8k of OTP is a lot to virtualize...
// maybe only track written sections up to a fixed maximum?
// For any other sections, fallback to reading the actual OTP?
typedef struct _BP_OTP_VIRTUALIZED_PAGE {
    uint16_t start_row;    // If zero, this page hasn't been written to yet.
    uint16_t rfu_padding;
    BP_OTP_RAW_READ_RESULT data[0x40];   // each page stores 0x40 (64) rows of OTP data
} BP_OTP_VIRTUALIZED_PAGE;
static BP_OTP_VIRTUALIZED_PAGE virtualized_otp[40] = { };
static bool virtualized_otp_full = false;

// probably want a helper function to map from row to virtualized page (nullptr if not exists)
// Set allocate_if_needed only when next action is to write to the page.
static inline uint16_t ROW_TO_PAGE_OFFSET(uint16_t row) { return row & 0x3Fu; }
static inline BP_OTP_VIRTUALIZED_PAGE* ROW_TO_VIRTUALIZED_PAGE(uint16_t row, bool allocate_if_needed) {
    if (row >= 0x1000u) { return NULL; } // invalid OTP Row ..  range is 0x000..0xFFF

    BP_OTP_VIRTUALIZED_PAGE* result = NULL;
    uint16_t page_start_row = row & (~0x3Fu);
    // find matching start_row in array?
    for (size_t i = 0; (result == NULL) && (i < ARRAY_SIZE(virtualized_otp)); ++i) {
        if (virtualized_otp[i].start_row == page_start_row) {
            result = &virtualized_otp[i];
        }
    }
    if ((result == NULL) && allocate_if_needed) {
        for (size_t i = 0; (result == NULL) && (i < ARRAY_SIZE(virtualized_otp)); ++i) {
            if (virtualized_otp[i].start_row == 0u) {
                BP_OTP_VIRTUALIZED_PAGE* tmp = &virtualized_otp[i];
                // read the page from OTP into the virtualized page...
                if (read_raw_wrapper(pages_start_row, tmp->data, sizeof(tmp->data)) != BOOTROM_OK) {
                    tmp->start_row = page_start_row;
                    result = tmp;
                }
            }
        }
    }
    // NOTE: it's possible to return NULL even when not full, when unable to read the page from OTP
    if ((result == NULL) && allocate_if_needed) {
        bool full = true;
        for (size_t i = 0; (result == NULL) && (i < ARRAY_SIZE(virtualized_otp)); ++i) {
            if (virtualized_otp[i].start_row == 0u) {
                full = false;
            }
        }
        if (full) {
            virtualized_otp_full = true;
        }
    }
    return result;
}

// virtualize the access to the underlying OTP...
bool bp_otp_write_single_row_raw(uint16_t row, uint32_t new_value);
bool bp_otp_read_single_row_raw(uint16_t row, uint32_t* out_data);
bool bp_otp_write_single_row_ecc(uint16_t row, uint16_t new_value);
bool bp_otp_read_single_row_ecc(uint16_t row, uint16_t* out_data);
bool bp_otp_read_ecc_data(uint16_t start_row, void* out_data, size_t count_of_bytes);
bool bp_otp_write_single_row_redundant_byte3x(uint16_t row, uint8_t new_value);
bool bp_otp_read_single_row_redundant_byte3x(uint16_t row, uint8_t* out_data);
bool bp_otp_write_redundant_rows_RBIT3(uint16_t start_row, uint32_t new_value);
bool bp_otp_read_redundant_rows_RBIT3(uint16_t start_row, uint32_t* out_data);
bool bp_otp_read_ecc_data(uint16_t start_row, void* out_data, size_t count_of_bytes);

#else // !defined(BP_USE_VIRTUALIZED_OTP)


bool bp_otp_write_single_row_raw(uint16_t row, uint32_t new_value) {
    return write_single_otp_raw_row(row, new_value);
}
bool bp_otp_read_single_row_raw(uint16_t row, uint32_t* out_data) {
    return read_raw_wrapper(row, out_data, sizeof(uint32_t)) == BOOTROM_OK;
}
bool bp_otp_write_single_row_ecc(uint16_t row, uint16_t new_value) {
    return write_single_otp_ecc_row(row, new_value);
}
bool bp_otp_read_single_row_ecc(uint16_t row, uint16_t* out_data) {
    return read_single_otp_ecc_row(row, out_data);
}
bool bp_otp_write_single_row_redundant_byte3x(uint16_t row, uint8_t new_value) {
    return write_otp_byte_3x(row, new_value);
}
bool bp_otp_read_single_row_redundant_byte3x(uint16_t row, uint8_t* out_data) {
    return read_otp_byte_3x(row, out_data);
}
bool bp_otp_write_redundant_rows_RBIT3(uint16_t start_row, uint32_t new_value) {
    return write_single_otp_value_N_of_M(start_row, 2, 3, new_value);
}
bool bp_otp_read_redundant_rows_RBIT3(uint16_t start_row, uint32_t* out_data) {
    return read_single_otp_value_N_of_M(start_row, 2, 3, out_data);
}
bool bp_otp_write_redundant_rows_RBIT8(uint16_t start_row, uint32_t new_value) {
    return write_single_otp_value_N_of_M(start_row, 3, 8, new_value);
}
bool bp_otp_read_redundant_rows_RBIT8(uint16_t start_row, uint32_t* out_data) {
    return read_single_otp_value_N_of_M(start_row, 3, 8, out_data);
}

// Arbitrary buffer size support functions

bool bp_otp_write_ecc_data(uint16_t start_row, const void* data, size_t count_of_bytes) {

    // write / verify one OTP row at a time
    size_t loop_count = count_of_bytes / 2u;
    bool require_buffering_last_row = (count_of_bytes & 1u);

    // Write full-sized rows first
    for (size_t i = 0; i < loop_count; ++i) {
        uint16_t tmp = ((const uint16_t*)data)[i];
        if (!bp_otp_write_single_row_ecc(start_row + i, tmp)) {
            return false;
        }
    }
    
    // Write any final partial-row data
    if (require_buffering_last_row) {
        // Read the single byte ... do NOT read as uint16_t as additional byte may not be valid readable memory
        // and use the local stack uint16_t for the actual write operation.
        uint16_t tmp = ((const uint8_t*)data)[count_of_bytes-1];
        if (!bp_otp_write_single_row_ecc(start_row + loop_count, tmp)) {
            return false;
        }
    }
    return true;
}
bool bp_otp_read_ecc_data(uint16_t start_row, void* out_data, size_t count_of_bytes) {
    if (count_of_bytes >= (0x1000*2)) { // OTP rows from 0x000u to 0xFFFu, so max 0x1000*2 bytes
        return false;
    }

    // read one OTP row at a time
    size_t loop_count = count_of_bytes / 2u;
    bool requires_buffering_last_row = (count_of_bytes & 1u);

    // Read full-sized rows first
    for (size_t i = 0; i < loop_count; ++i) {
        uint16_t * b = ((uint16_t*)out_data) + i; // pointer arithmetic
        if (!bp_otp_read_single_row_ecc(start_row + i, b)) {
            return false;
        }
    }

    // Read any final partial-row data
    if (requires_buffering_last_row) {
        uint16_t tmp = 0xFFFFu;
        if (!bp_otp_read_single_row_ecc(start_row + loop_count, &tmp)) {
            return false;
        }
        // update the last single byte of the buffer
        // ensure to use byte-based pointer, as only one buffer byte is ensured to be valid
        uint8_t * b = ((uint8_t*)out_data) + count_of_bytes - 1u;
        *b = (tmp & 0xFFu);
    }
    return true;
}

bool bp_otp_read_raw_data(uint16_t start_row, void* out_data, size_t count_of_bytes) {
    if (count_of_bytes == 0u) {
        return false; // ?? should this return true?
    }
    memset(out_data, 0, count_of_bytes);
    if (count_of_bytes >= (0x1000*4)) { // OTP rows from 0x000u to 0xFFFu, so max 0x1000*2 bytes
        return false;
    }
    if ((count_of_bytes % 4u) != 0) {
        return false;
    }
    return (read_raw_wrapper(start_row, out_data, count_of_bytes) == BOOTROM_OK);
}
bool bp_otp_write_raw_data(uint16_t start_row, const void* data, size_t count_of_bytes) {
    if (count_of_bytes == 0u) {
        return false; // ?? should this return true?
    }
    if ((count_of_bytes % 4u) != 0) {
        return false;
    }
    // Verify the top byte of each uint32_t is zero ... catch coding errors early before it writes to OTP
    size_t count_of_uint32 = count_of_bytes / 4u;
    const uint32_t * p = data;
    for (size_t i = 0; i < count_of_uint32; ++i) {
        if ((p[i] & 0xFF000000u) != 0u) {
            return false;
        }
    }
    // lower level will catch other errors (range, permissions, etc.)
    return (write_raw_wrapper(start_row, data, count_of_bytes) == BOOTROM_OK);
}



#endif // defined(BP_USE_VIRTUALIZED_OTP)
                 