#define BP_DEBUG_OVERRIDE_DEFAULT_CATEGORY BP_DEBUG_CAT_OTP

#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "pico/bootrom.h" // required for rom_func_otp_access()

#include "saferotp.h"
#include "saferotp_ecc.h"
#include "saferotp_debug_stub.h"


// Set this global variable anywhere in the code
// to immediately wait for keypress prior to writing
// to the OTP fuses.  This will catch ***ALL*** writes
// that use this library.
static volatile bool g_WaitForKey_otp_rw = false;
#define WAIT_FOR_KEY()                 \
    do {                               \
        if (g_WaitForKey_otp_rw) {     \
            MY_DEBUG_WAIT_FOR_KEY();   \
        }                              \
    } while (0)

#pragma region    // internal static function prototypes
static bool is_valid_otp_range_raw(uint16_t starting_row, size_t raw_byte_count);
static bool hw_write_raw_otp_wrapper(uint16_t starting_row, const void* buffer, size_t buffer_size);
static bool hw_read_raw_otp_wrapper(uint16_t starting_row, void* buffer, size_t buffer_size);
static bool virt_initialize(uint64_t ignored_pages_mask);
static bool virt_override_restore(uint16_t starting_row, const void* buffer, size_t buffer_size);
static bool virt_override_save(uint16_t starting_row, void* buffer, size_t buffer_size);
static bool virt_write_raw_otp_wrapper(uint16_t starting_row, const void* buffer, size_t buffer_size);
static bool virt_read_raw_otp_wrapper(uint16_t starting_row, void* buffer, size_t buffer_size);
static bool write_raw_wrapper(uint16_t starting_row, const void* buffer, size_t buffer_size);
static bool read_raw_wrapper(uint16_t starting_row, void* buffer, size_t buffer_size);
static bool read_single_otp_ecc_row(uint16_t row, uint16_t * data_out);
static bool write_single_otp_ecc_row(uint16_t row, uint16_t data);
static bool write_single_otp_raw_row(uint16_t row, uint32_t data);
static bool read_single_otp_value_N_of_M(uint16_t start_row, uint8_t N, uint8_t M, uint32_t* out_data);
static bool write_single_otp_value_N_of_M(uint16_t start_row, uint8_t N, uint8_t M, uint32_t new_value);
static bool read_otp_byte_3x(uint16_t row, uint8_t* out_data);
static bool write_otp_byte_3x(uint16_t row, uint8_t new_value);
#pragma endregion // internal static function prototypes

// BUGBUG / TODO: enable "virtual" OTP, by writing to memory buffer instead of OTP fuses,
//                and tracking the written values in a separate buffer (+bitmask indicating which rows were written).
//                This will allow testing of the OTP code without actually writing to the OTP fuses.
#pragma region    // OTP HAL layer ... to allow for virtualized OTP

typedef struct _BP_VIRTUALIZED_OTP_BUFFER {
    SAFEROTP_RAW_READ_RESULT rows[NUM_OTP_ROWS]; // 0x1000 == 4096 rows, requiring 4 bytes each == 16k statically allocated buffer (!!)
} BP_VIRTUALIZED_OTP_BUFFER;
static BP_VIRTUALIZED_OTP_BUFFER g_virtual_otp = { };
static bool g_virtual_otp_initialized = false;

// returns TRUE on successful write, FALSE on failures
static bool hw_write_raw_otp_wrapper(uint16_t starting_row, const void* buffer, size_t buffer_size) {
    // TODO: Check BOOTLOCK7 to determine if bootrom will require ownership of BOOTLOCK2 (OTP)
    //       This would return error BOOTROM_ERROR_LOCK_REQUIRED (-19) if this ever occurs.
    otp_cmd_t cmd;
    cmd.flags = starting_row;
    cmd.flags |= OTP_CMD_WRITE_BITS;
    PRINT_DEBUG("OTP WRITE Debug: about to write OTP starting at row %03x %d bytes (0x%x rows\n", starting_row, buffer_size, (buffer_size/sizeof(uint32_t)));
    WAIT_FOR_KEY();
    int r = rom_func_otp_access((uint8_t*)buffer, buffer_size, cmd);
    if (r != BOOTROM_OK) {
        PRINT_ERROR("OTP WRITE Error: Failed to write raw OTP values starting at row %03x (%d bytes / 0x%x rows), error %d (0x%x)\n", starting_row, buffer_size, (buffer_size/sizeof(uint32_t)), r, r);
    }
    return (BOOTROM_OK == r);
}
// returns TRUE on successful read, FALSE on failures
static bool hw_read_raw_otp_wrapper(uint16_t starting_row, void* buffer, size_t buffer_size) {
    // TODO: Check BOOTLOCK7 to determine if bootrom will require ownership of BOOTLOCK2 (OTP)
    //       This would return error BOOTROM_ERROR_LOCK_REQUIRED (-19) if this ever occurs.
    otp_cmd_t cmd;
    cmd.flags = starting_row;
    int r = rom_func_otp_access((uint8_t*)buffer, buffer_size, cmd);
    PRINT_DEBUG("OTP READ Debug: about to write OTP starting at row %03x %d bytes (0x%x rows\n", starting_row, buffer_size, (buffer_size/sizeof(uint32_t)));
    if (r != BOOTROM_OK) {
        PRINT_ERROR("OTP READ Error: Failed to write raw OTP values starting at row %03x (%d bytes / 0x%x rows), error %d (0x%x)\n", starting_row, buffer_size, (buffer_size/sizeof(uint32_t)), r, r);
    }
    return (BOOTROM_OK == r);
}

// Enable "virtualized" OTP ... useful for testing.
// 16k of OTP is a lot to virtualize... 
// but RP2350 has 512k, of which >256k is currently free, so go with SIMPLE!
// * [ ] At initialization:
//   * [ ] Detect if OTP virtualization file exists on media (and correct size)
//   * [ ] If so, read that file into memory
//   * [ ] If not read from file (or failed), read all rows of OTP as part of the initialization
// * [ ] All OTP reads get memcpy() from that buffer
// * [ ] All OTP writes get logical-OR'd into that buffer
// * [ ] At clean shutdown, write the virtualized OTP data to NAND
//
// STRETCH GOALS:
// Applying OTP page permissions:
// * [ ] Use memory protection to prevent access to memory-mapped regions (OTP_DATA_BASE ...)
// * [ ] Support initially presuming all access is from secure mode
//   * YAGNI - support for non-secure and bootloader modes ... keep it simple!
// * [ ] OTP Access Keys == YAGNI
//   * Plus, it'd be major investment to virtualize, using access permissions, traps, etc.
// * [ ] Support for hard-locked pages
//   * PAGEn_LOCK0 == YAGNI ... deals with OTP access keys ... which cannot easily be supported
//   * PAGEn_LOCK1 == Permissions for secure mode are in least significant two bits
//   * Values: 0x0 == R/W, 0x1 == R/O, 0x3 == NO ACCESS
//   * All other values == YAGNI
// * [ ] Support for soft-locked pages
//   * SW_LOCK0 .. SW_LOCK63 == registers to read the permissions from
//   * Permissions for secure mode are in least significant two bits
//   * Values: 0x0 == R/W, 0x1 == R/O, 0x3 == NO ACCESS
//   * All other values == YAGNI
//

static_assert(NUM_OTP_ROWS == 0x1000u, "NUM_OTP_ROWS must be 0x1000");
static_assert(NUM_OTP_ROWS <= UINT16_MAX, "NUM_OTP_ROWS must be less than 0xFFFF ... or else must update range checks for overflow conditions");
static bool is_valid_otp_range_raw(uint16_t starting_row, size_t raw_byte_count) {
    if (starting_row >= NUM_OTP_ROWS) {
        // Can only access from 0x000 .. (NUM_OTP_ROWS-1)
        return false;
    }
    if (raw_byte_count % sizeof(uint32_t) != 0u) {
        // Must be aligned to 4-byte boundaries
        return false;
    }
    if (raw_byte_count > NUM_OTP_ROWS * sizeof(uint32_t)) {
        // even if started at zero, this would be too large
        // separate check to avoid overflows in later checks
        return false;
    }
    // Above checks ensure this is safe to store in uint16_t
    uint16_t row_count = raw_byte_count / sizeof(uint32_t);
    if (row_count == 0u) {
        return false;
    }
    if ((NUM_OTP_ROWS - row_count) > starting_row) {
        // this would overflow past the last OTP row
        return false;
    }
    // All checks passed.
    return true;
}

static bool virt_initialize(uint64_t ignored_pages_mask) {
    // Initialize the virtualized OTP pages
    if (g_virtual_otp_initialized) {
        PRINT_ERROR("OTP VIRT Error: Attempt to re-initialize already-virtualized OTP data\n");
        return false;
    }
    memset(&g_virtual_otp, 0, sizeof(g_virtual_otp));
    // read all 16k of OTP into the virtualized buffer
    size_t error_count = 0u;
    uint16_t page = 0;
    for (; page < NUM_OTP_PAGES; ++page) {
        uint64_t tst_mask = 1u;
        tst_mask <<= page;
        // skip values from this page if requested
        if ((ignored_pages_mask & tst_mask) != 0u) {
            // caller requested to ignored this page, so skip it
            continue;
        }
        uint16_t row = page * NUM_OTP_PAGE_ROWS; // starting row for this page
        for (uint16_t i = 0; i < NUM_OTP_PAGE_ROWS; ++i, ++row) {
            if (!hw_read_raw_otp_wrapper(row, &g_virtual_otp.rows[row], sizeof(SAFEROTP_RAW_READ_RESULT))) {
                // can easily scan for errors later by just checking if any of the high bits were set
                g_virtual_otp.rows[row].as_uint32 = 0xFFFFFFFFu; // ensure the stored value is an error
                error_count++;
            }
        }
    }
    if (error_count > 0u) {
        PRINT_WARNING("OTP VIRT Warning: Failed to read %d rows of OTP data into virtualized buffer\n", error_count);
        // loop and print failing indices?
        for (uint16_t row = 0; row < 0x1000u; ++row) {
            if ((g_virtual_otp.rows[row].as_uint32 & 0xFFu) != 0u){
                PRINT_WARNING("OTP VIRT Warning: -->  Row 0x%03x (%02x:%02x) failed to read\n",
                    row,
                    (row / NUM_OTP_PAGE_ROWS), (row % NUM_OTP_PAGE_ROWS)
                );
            }
        }
    }
    g_virtual_otp_initialized = true;
    return true;
}
static bool virt_override_restore(uint16_t starting_row, const void* buffer, size_t buffer_size) {
    // callers can then save/restore OTP state, such as from storage / file system
    if (!is_valid_otp_range_raw(starting_row, buffer_size)) {
        PRINT_ERROR("OTP VIRT Error: Invalid (start row / raw byte count): 0x%03x %zu\n", starting_row, buffer_size);
        return false;
    }
    // NOTE: This simply replaces the values, even if doing so would not otherwise have been a valid write.
    //       Allows resetting pages to zero (bits from 1 -> 0), bypasses permissions, etc.
    memcpy(&g_virtual_otp.rows[starting_row], buffer, buffer_size);
    return true;
}
static bool virt_override_save(uint16_t starting_row, void* buffer, size_t buffer_size) {
    // callers can then save/restore OTP state, such as from storage / file system
    if (!is_valid_otp_range_raw(starting_row, buffer_size)) {
        PRINT_ERROR("OTP VIRT Error: Invalid (start row / raw byte count): 0x%03x %zu\n", starting_row, buffer_size);
        return false;
    }
    memcpy(buffer, &g_virtual_otp.rows[starting_row], buffer_size);
    return true;
}

static bool virt_write_raw_otp_wrapper(uint16_t starting_row, const void* buffer, size_t buffer_size) {
    if (!g_virtual_otp_initialized) {
        PRINT_ERROR("OTP VIRT Error: Attempt to write virtualized OTP data without initialization\n");
        return false;
    }
    // belt and suspenders ... even if caller did this
    if (!is_valid_otp_range_raw(starting_row, buffer_size)) {
        PRINT_ERROR("OTP VIRT WRITE Error: Invalid (start row / raw byte count): 0x%03x %zu\n", starting_row, buffer_size);
        return false;
    }
    // TODO: Check BOOTLOCK7 to determine if bootrom will require ownership of BOOTLOCK2 (OTP)
    size_t row_count = buffer_size / sizeof(uint32_t);
    // process each row in order (per RP2350 datasheet ... )
    for (size_t i = 0; i < row_count; ++i) {
        // TODO: Any permissions checks, when implemented....

        // verify the existing value was readable ... else refuse to modify it.
        SAFEROTP_RAW_READ_RESULT *current = &g_virtual_otp.rows[starting_row + i];
        if (current->is_error) {
            PRINT_ERROR("OTP VIRT WRITE Error: Attempt to write virtualized OTP row 0x%03x, which previously failed to read (start row %03x, buffer size %zx)\n", starting_row+i, starting_row, buffer_size);
            return false;
        }
        // OTP bits can only transition from zero to one (0 --> 1).
        // Verify none of the bits would transition from (1 --> 0).
        const SAFEROTP_RAW_READ_RESULT *new_value = (const SAFEROTP_RAW_READ_RESULT *)(  &(((const uint32_t*)buffer)[i]) );
        if ((current->as_uint32 | new_value->as_uint32) != new_value->as_uint32) {
            PRINT_ERROR("OTP VIRT WRITE Error: Attempt to write virtualized OTP row 0x%03x from %06x -> %06x, which would flip bits from 0 --> 1 (start row %03x, buffer size %zx)\n",
                starting_row+i,
                current->as_uint32, new_value->as_uint32,
                starting_row, buffer_size
            );
            return false;
        }
        // Update the individual row's data
        current->as_uint32 = new_value->as_uint32;
    }
    return true;
}
// returns TRUE on successful read, FALSE on failures
static bool virt_read_raw_otp_wrapper(uint16_t starting_row, void* buffer, size_t buffer_size) {
    if (!g_virtual_otp_initialized) {
        PRINT_ERROR("OTP VIRT Error: Attempt to write virtualized OTP data without initialization\n");
        return false;
    }
    // belt and suspenders ... even if caller did this
    if (!is_valid_otp_range_raw(starting_row, buffer_size)) {
        PRINT_ERROR("OTP VIRT READ Error: Invalid (start row / raw byte count): 0x%03x %zu\n", starting_row, buffer_size);
        return false;
    }
    // TODO: Check BOOTLOCK7 to determine if bootrom will require ownership of BOOTLOCK2 (OTP)
    size_t row_count = buffer_size / sizeof(uint32_t);
    // process each row in order (per RP2350 datasheet ... )
    for (size_t i = 0; i < row_count; ++i) {
        // TODO: Any permissions checks, when implemented....

        // verify the existing value was readable ... else return an error
        SAFEROTP_RAW_READ_RESULT *current = &g_virtual_otp.rows[starting_row + i];
        if (current->is_error) {
            PRINT_ERROR("OTP VIRT READ Error: Attempt to write virtualized OTP row 0x%03x, which previously failed to read (start row %03x, buffer size %zx)\n", starting_row+i, starting_row, buffer_size);
            return false; // report the error
        }
        // Else return the value from the virtualized buffer
        uint32_t * to_write = &(((uint32_t*)buffer)[i]);
        *to_write = current->as_uint32;
    }
    return true;
}

#pragma endregion // OTP HAL layer ... to allow for virtualized OTP

// don't want to use that difficult-to-parse API in many places....
static bool write_raw_wrapper(uint16_t starting_row, const void* buffer, size_t buffer_size) {
    if (!is_valid_otp_range_raw(starting_row, buffer_size)) {
        PRINT_ERROR("OTP WRITE Error: Invalid (start row / raw byte count): 0x%03x %zu\n", starting_row, buffer_size);
        return false;
    }
    if (g_virtual_otp_initialized) {
        return virt_write_raw_otp_wrapper(starting_row, buffer, buffer_size);
    } else {
        return hw_write_raw_otp_wrapper(starting_row, buffer, buffer_size);
    }
}
static bool read_raw_wrapper(uint16_t starting_row, void* buffer, size_t buffer_size) {
    if (!is_valid_otp_range_raw(starting_row, buffer_size)) {
        PRINT_ERROR("OTP WRITE Error: Invalid (start row / raw byte count): 0x%03x %zu\n", starting_row, buffer_size);
        return false;
    }
    if (buffer_size % sizeof(uint32_t) != 0u) {
        PRINT_ERROR("OTP VIRT Error: Attempt to read virtualized OTP data with non-aligned size %d\n", buffer_size);
        return false;
    }
    if (g_virtual_otp_initialized) {
        return virt_read_raw_otp_wrapper(starting_row, buffer, buffer_size);
    } else {
        return hw_read_raw_otp_wrapper(starting_row, buffer, buffer_size);
    }
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
    if (!read_raw_wrapper(row, &existing_raw_data, sizeof(existing_raw_data))) {
        PRINT_ERROR("OTP_RW Error: Failed to read OTP raw row %03x\n", row);
        return false;
    }
    uint32_t decode_result = saferotp_decode_raw(existing_raw_data);
    if ((decode_result & 0xFF000000u) != 0u) {
        PRINT_ERROR("OTP_RW Error: Failed to decode OTP row %03x value 0x%06x: Result 0x%08x\n", row, existing_raw_data, decode_result);
        return false;
    }
    *data_out = (uint16_t)decode_result;
    return true;   
}
static bool write_single_otp_ecc_row(uint16_t row, uint16_t data) {

    // Allow writes to valid ECC encoded data, so long as it's possible to do so.
    // This means adjusting for BRBP bits that may already be set to 1,
    // and then checking if there's a single-bit error that can be ignored.
    // If so, try to write the data, even if some bits are already set!

    // 1. Read the existing raw data
    SAFEROTP_RAW_READ_RESULT existing_raw_data;
    if (!read_raw_wrapper(row, &existing_raw_data, sizeof(existing_raw_data))) {
        PRINT_ERROR("OTP_RW Error: Failed to read OTP raw row %03x\n", row);
        return false;
    }

    // 2. SUCCESS if existing raw data already encodes the value; Do not update the OTP row.
    //    TODO: Consider if existing data has a single bit flipped from one to zero ... could write
    //          the extra bit to try to reduce errors?
    uint32_t decode_result = saferotp_decode_raw(existing_raw_data.as_uint32);
    if (((decode_result & 0xFF000000u) == 0u) && ((uint16_t)decode_result == data)) {
        // already written, nothing more to do for this row
        PRINT_VERBOSE("OTP_RW: Row %03x already has data 0x%04x .. not writing\n", row, data);
        return true;
    }

    // 3. Do we have to adjust the encoded data due to existing BRBP bits?  Determine data to write (or fail if not possible)
    uint32_t data_to_write;
    do {
        SAFEROTP_RAW_READ_RESULT encoded_new_data      = { .as_uint32 = saferotp_calculate_ecc(data) };
        SAFEROTP_RAW_READ_RESULT encoded_new_data_brbp = { .as_uint32 = encoded_new_data.as_uint32 ^ 0x00FFFFFFu };
        
        // count how many bits would be flipped vs. the new data
        uint_fast8_t bits_to_flip      = __builtin_popcount(existing_raw_data.as_uint32 ^ encoded_new_data.as_uint32);
        uint_fast8_t bits_to_flip_brbp = __builtin_popcount(existing_raw_data.as_uint32 ^ encoded_new_data_brbp.as_uint32);

        if (bits_to_flip == 0u) {
            // Great!  Can write without adjusting for existing data.
            data_to_write = encoded_new_data.as_uint32;
        } else if (bits_to_flip_brbp == 0u) {
            // Great!  Can write without error by using BRBP bits.
            PRINT_VERBOSE("OTP_RW Info: Writing ECC OTP row %03x with data 0x%04x: Using BRBP bits\n", row, data);
            data_to_write = encoded_new_data_brbp.as_uint32;
        } else if (bits_to_flip == 1u) {
            // Can write, but will have an existing single-bit error in the data...
            PRINT_WARNING("OTP_RW Warn: Writing ECC OTP row %03x with data 0x%04x: Redundancy compromised, but writing is possible with 1-bit error.\n",
                row, data, encoded_new_data.as_uint32, existing_raw_data.as_uint32
            );
            data_to_write = encoded_new_data.as_uint32 | existing_raw_data.as_uint32;
        } else if (bits_to_flip_brbp == 1u) {
            // Can write, but will need to use BRBP bits, and there will still be a single-bit error in the data...
            PRINT_WARNING("OTP_RW Warn: Writing ECC OTP row %03x with data 0x%04x: Redundancy compromised, but writing is possible with BRBP *AND* 1-bit error.\n",
                row, data, encoded_new_data_brbp.as_uint32, existing_raw_data.as_uint32
            );
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
    if (!write_raw_wrapper(row, &data_to_write, sizeof(data_to_write))) {
        PRINT_ERROR("OTP_RW Error: Failed to write ECC OTP row %03x with data 0x%06x (ECC encoding of 0x%04x)\n",
            row, data, data_to_write, data_to_write
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
    if (!read_raw_wrapper(row, &existing_data, sizeof(existing_data))) {
        PRINT_ERROR("OTP_RW Warn: Failed to read OTP raw row %03x\n", row);
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
    if (!write_raw_wrapper(row, &data, sizeof(data))) {
        PRINT_ERROR("OTP_RW Warn: Failed to write OTP raw row %03x\n", row);
        return false;
    }

    // Verify the data was recorded ...
    if (!read_raw_wrapper(row, &existing_data, sizeof(existing_data))) {
        PRINT_ERROR("OTP_RW Warn: Failed to read OTP raw row %03x\n", row);
        return false;
    }
    if (existing_data != data) {
        PRINT_ERROR("OTP_RW Warn: Failed to verify OTP raw row %03x: Existing 0x%06x != new data 0x%06x\n", row, existing_data, data);
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
        // The below code ***should*** work for any N-of-M, so long as both N and M are <= 8.
        // However, it's not been tested, and is not used in any real-world scenario.
        // Thus: YAGNI ... and if you do need it, you'll need to test it yourself.
        PRINT_ERROR("OTP_RW Error: Read OTP N-of-M: Unsupported N=%d, M=%d\n", N, M);
        return false;
    }
    uint32_t v[8u] = {0u};    // zero-initialize the array, sized for maximum supported `M`
    bool     r[8u] = {false}; // zero-initialized is false
    assert(M <= 8u);

    // Read each of the `M` rows
    for (size_t i = 0; i < M; ++i) {
        r[i] = read_raw_wrapper(start_row+i, &(v[i]), sizeof(uint32_t));
    }

    // Ensure at least `N` reads were successful, else return an error
    uint_fast8_t successful_reads = 0;
    for (size_t i = 0; i < M; ++i) {
        if (r[i]) {
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
        // Could actually skip this, IFF failed reads were set to zero.
        // This is because the voting only considers bits set to one.
        if (!r[i]) {
            continue;
        }
        // loop through each bit, and add a vote if that bit is set
        uint32_t tmp = v[i];
        for (uint_fast8_t j = 0; j < 24u; ++j) {
            uint32_t mask = (1u << j);
            if ((tmp & mask) != 0u) {
                ++votes[j];
            }
        }
    }

    // Generate a result based on the votes (set if votes >= N)
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
    PRINT_DEBUG("OTP_RW Debug: Write OTP 2-of-3: row 0x%03x\n", start_row);
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
        if (!read_raw_wrapper(start_row+i, &old_data, sizeof(old_data))) {
            PRINT_WARNING("OTP_RW Warn: unable to read old bits for OTP %d-of-%d: row 0x%03x -- DEFERRING\n", N, M, start_row+i);
            continue; // to next OTP row, if any
        }
        if ((old_data & new_value) == new_value) {
            // no change needed ... not setting any new bit for this row
            PRINT_WARNING("OTP_RW Warn: skipping update to row 0x%03x: old value 0x%06x already has bits 0x%06x\n", start_row+i, old_data, new_value);
            continue; // to next OTP row, if any
        }

        // new_value may have additional bits set that are not intended to be set after voting.
        // This is OK ... validate voted-upon results after the writes are all done.
        uint32_t to_write = old_data | new_value;
        PRINT_DEBUG("OTP_RW Debug: updating row 0x%03x: 0x%06x --> 0x%06x\n", start_row+i, old_data, to_write);
        if (!write_raw_wrapper(start_row+i, &to_write, sizeof(to_write))){
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
    SAFEROTP_RAW_READ_RESULT v;
    if (!read_raw_wrapper(row, &v.as_uint32, sizeof(uint32_t))) {
        PRINT_ERROR("OTP_RW Error: Failed to read OTP byte 3x: row 0x%03x\n", row);
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

    PRINT_DEBUG("OTP_RW Debug: Write OTP byte_3x: row 0x%03x\n", row);

    // 1. read the old data as raw bits
    SAFEROTP_RAW_READ_RESULT old_raw_data;
    if (!read_raw_wrapper(row, &old_raw_data, sizeof(old_raw_data))) {
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
                // found a bit that already has enough votes to be set
                // and thus cannot be unset (stored as zero in the new raw data)
                PRINT_ERROR("OTP_RW Error: Attempt to byte_3x write row %03x to 0x%02x; Existing data 0x%06x bit %d votes as set, but is not set in new value\n",
                    row, new_value, old_raw_data.as_uint32, i
                );
                return false;
            }
        }
    }

    // 3. Does the existing data already have all necessary bits set?  If so, SUCCESS w/o writing.
    if (((old_raw_data.as_bytes[0] & new_value) == new_value) &&
        ((old_raw_data.as_bytes[1] & new_value) == new_value) &&
        ((old_raw_data.as_bytes[2] & new_value) == new_value) ) {
        // no write required, as the row already has all the bits set that we'd be trying to write
        PRINT_VERBOSE("OTP_RW: Write OTP byte_3x: Row %03x data 0x%06x already has all required bits set for 0x%02x ... not writing\n", row, old_raw_data.as_uint32, new_value);
        return true;
    }

    // 4. Logically OR the new_value bits into the existing data, and write the updated raw data.
    SAFEROTP_RAW_READ_RESULT to_write = { .as_uint32 = old_raw_data.as_uint32 };
    to_write.as_bytes[0] |= new_value;
    to_write.as_bytes[1] |= new_value;
    to_write.as_bytes[2] |= new_value;
    PRINT_DEBUG("OTP_RW Debug: Write OTP byte_3x: updating row 0x%03x: 0x%06x --> 0x%06x\n", row, old_raw_data.as_uint32, to_write.as_uint32);
    if (!write_raw_wrapper(row, &to_write, sizeof(to_write))) {
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

bool saferotp_virtualization_init_pages(uint64_t ignored_pages_mask) {
    virt_initialize(ignored_pages_mask);
    return true;
}
bool saferotp_virtualization_restore(uint16_t starting_row, const void* buffer, size_t buffer_size) {
    return virt_override_restore(starting_row, buffer, buffer_size);
}
bool saferotp_virtualization_save(uint16_t starting_row, void* buffer, size_t buffer_size) {
    return virt_override_save(starting_row, buffer, buffer_size);
}

// NOTE: On failure, the state of the OTP row(s) is UNDEFINED.
//       For example, some rows may have been written, while other rows failed to be written.
//       It's also possible that a single OTP row was partially written, and thus contains
//       an invalid value.
//       It is the caller's responsibility, upon a write failing, to perform any necessary
//       data cleanup.  For example, callers might raw write 0xFFFFFFu to at least some
//       of the rows, or otherwise mark the range as containing unreliable data.


bool saferotp_write_single_row_raw(uint16_t row, uint32_t new_value) {
    return write_single_otp_raw_row(row, new_value);
}
bool saferotp_read_single_row_raw(uint16_t row, uint32_t* out_data) {
    return read_raw_wrapper(row, out_data, sizeof(uint32_t));
}
bool saferotp_write_single_row_ecc(uint16_t row, uint16_t new_value) {
    return write_single_otp_ecc_row(row, new_value);
}
bool saferotp_read_single_row_ecc(uint16_t row, uint16_t* out_data) {
    return read_single_otp_ecc_row(row, out_data);
}
bool saferotp_write_single_row_redundant_byte3x(uint16_t row, uint8_t new_value) {
    return write_otp_byte_3x(row, new_value);
}
bool saferotp_read_single_row_redundant_byte3x(uint16_t row, uint8_t* out_data) {
    return read_otp_byte_3x(row, out_data);
}
bool saferotp_write_redundant_rows_RBIT3(uint16_t start_row, uint32_t new_value) {
    return write_single_otp_value_N_of_M(start_row, 2, 3, new_value);
}
bool saferotp_read_redundant_rows_RBIT3(uint16_t start_row, uint32_t* out_data) {
    return read_single_otp_value_N_of_M(start_row, 2, 3, out_data);
}
bool saferotp_write_redundant_rows_RBIT8(uint16_t start_row, uint32_t new_value) {
    return write_single_otp_value_N_of_M(start_row, 3, 8, new_value);
}
bool saferotp_read_redundant_rows_RBIT8(uint16_t start_row, uint32_t* out_data) {
    return read_single_otp_value_N_of_M(start_row, 3, 8, out_data);
}

// Arbitrary buffer size support functions ...
bool saferotp_write_ecc_data(uint16_t start_row, const void* data, size_t count_of_bytes) {

    // write / verify one OTP row at a time
    size_t loop_count = count_of_bytes / 2u;
    bool require_buffering_last_row = (count_of_bytes & 1u);

    // Write full-sized rows first
    for (size_t i = 0; i < loop_count; ++i) {
        uint16_t tmp = ((const uint16_t*)data)[i];
        if (!saferotp_write_single_row_ecc(start_row + i, tmp)) {
            return false;
        }
    }
    
    // Write any final partial-row data
    if (require_buffering_last_row) {
        // Read the single byte ... do NOT read as uint16_t as additional byte may not be valid readable memory
        // and use the local stack uint16_t for the actual write operation.
        uint16_t tmp = ((const uint8_t*)data)[count_of_bytes-1];
        if (!saferotp_write_single_row_ecc(start_row + loop_count, tmp)) {
            return false;
        }
    }
    return true;
}
bool saferotp_read_ecc_data(uint16_t start_row, void* out_data, size_t count_of_bytes) {
    if (count_of_bytes >= (0x1000*2)) { // OTP rows from 0x000u to 0xFFFu, so max 0x1000*2 bytes
        return false;
    }

    // read one OTP row at a time
    size_t loop_count = count_of_bytes / 2u;
    bool requires_buffering_last_row = (count_of_bytes & 1u);

    // Read full-sized rows first
    for (size_t i = 0; i < loop_count; ++i) {
        uint16_t * b = ((uint16_t*)out_data) + i; // pointer arithmetic
        if (!saferotp_read_single_row_ecc(start_row + i, b)) {
            return false;
        }
    }

    // Read any final partial-row data
    if (requires_buffering_last_row) {
        uint16_t tmp = 0xFFFFu;
        if (!saferotp_read_single_row_ecc(start_row + loop_count, &tmp)) {
            return false;
        }
        // update the last single byte of the buffer
        // ensure to use byte-based pointer, as only one buffer byte is ensured to be valid
        uint8_t * b = ((uint8_t*)out_data) + count_of_bytes - 1u;
        *b = (tmp & 0xFFu);
    }
    return true;
}

bool saferotp_read_raw_data(uint16_t start_row, void* out_data, size_t count_of_bytes) {
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
    return read_raw_wrapper(start_row, out_data, count_of_bytes);
}
bool saferotp_write_raw_data(uint16_t start_row, const void* data, size_t count_of_bytes) {
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
    return write_raw_wrapper(start_row, data, count_of_bytes);
}

                 