#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>

#if RPI_PLATFORM == RP2350
    //#include "hardware/structs/otp.h"
    //#include "pico/bootrom.h"
    //#include "hardware/regs/addressmap.h"
    //#include "hardware/regs/otp.h"
#endif


// Use prefix `bp_otp_` for functions in this file
// Use prefix `BP_OTP_` for enums and typedef'd structs in this file

#ifdef __cplusplus
extern "C" {
#endif

// Note: looking at these results is only for debugging purposes.
//       Code can simply check for error by checking if the top 8 bits are non-zero.
//       (any bit set in the top 8 bits indicates error)
typedef enum _BP_OTP_ECC_ERROR {
    // only low 24 bits can contain data
    // Thus, all values from 0x00000000u .. 0x00FFFFFFu are successful results
    BP_OTP_ECC_ERROR_INVALID_INPUT                   = 0xFF010000u,
    BP_OTP_ECC_ERROR_DETECTED_MULTI_BIT_ERROR        = 0xFF020000u,
    BP_OTP_ECC_ERROR_BRBP_NEITHER_DECODING_VALID     = 0xFF030000u, // BRBP = 0b10 or 0b01, but neither decodes precisely
    BP_OTP_ECC_ERROR_NOT_VALID_SINGLE_BIT_FLIP       = 0xFF050000u, // 
    BP_OTP_ECC_ERROR_INVALID_ENCODING                = 0xFF040000u, // Syndrome alone generates data, but too many bit flips...
    BP_OTP_ECC_ERROR_BRBP_DUAL_DECODINGS_POSSIBLE    = 0x80000000u, // TODO: prove code makes this impossible to hit
    BP_OTP_ECC_ERROR_INTERNAL_ERROR_BRBP_BIT         = 0x80010000u, // TODO: prove code makes this impossible to hit
    BP_OTP_ECC_ERROR_INTERNAL_ERROR_PERFECT_MATCH    = 0x80020000u, // TODO: prove code makes this impossible to hit
    BP_OTP_ECC_ERROR_POTENTIALLY_READABLE_BY_BOOTROM = 0xFF990000u, // TODO: prove or disprove bootrom behavior for these cases (all have 3 or 5 flipped bits)
} BP_OTP_ECC_ERROR;

typedef enum _BP_OTPDIR_DATA_ENCODING_TYPE {
    // No data is associated with this row.
    //  Single row, 24-bits of raw data, no error correction or detection
    BP_OTPDIR_DATA_ENCODING_TYPE_NONE                    = 0x0u,
    // Single row, 24-bits of raw data, no error correction or detection
    BP_OTPDIR_DATA_ENCODING_TYPE_RAW                     = 0x1u,
    // Single row, 8-bits of data, stored triple-redundant.
    // Majority voting (2-of-3) is applied for each bit independently.
    BP_OTPDIR_DATA_ENCODING_TYPE_BYTE3X                  = 0x2u,
    // Identical data stored in three consecutive OTP rows.
    // Majority voting (2-of-3) is applied for each bit independently.
    // APIs should be provided the first (lowest) OTP row number.
    BP_OTPDIR_DATA_ENCODING_TYPE_RBIT3                   = 0x3u,
    // Identical data stored in eight consecutive OTP rows.
    // Special voting is applied for each bit independently:
    // If 3 or more rows have the bit set, then that bit is considered set.
    // This is used ONLY for the critical boot rows.
    // APIs should be provided the first (lowest) OTP row number.
    BP_OTPDIR_DATA_ENCODING_TYPE_RBIT8                   = 0x4u,
    // Single row, 16-bits of data, ECC detects 2-bit errors, corrects 1-bit errors
    // and BRBP allows writing even where a row has a single bit already set to 1
    // (even if stored value would normally store zero for that bit).
    BP_OTPDIR_DATA_ENCODING_TYPE_ECC                     = 0x5u,
    // Identical to BP_OTP_DATA_ENCODING_TYPE_ECC, with the added requirement
    // that the record's length includes at least one trailing zero byte.
    // This is useful for ASCII / UTF-8 strings.
    BP_OTPDIR_DATA_ENCODING_TYPE_ECC_ASCII_STRING        = 0x6u,
    // Value is encoded directly within the directory entry as an (up to) 32-bit value.
    // The least significant 16 bits are stored within the `start_row` field.
    // The most significant 16 bits are stored within the `byte_count` field.
    BP_OTPDIR_DATA_ENCODING_TYPE_EMBEDED_IN_DIRENTRY     = 0x7u,
} BP_OTPDIR_DATA_ENCODING_TYPE;

#pragma region    // BP_OTP_DIRENTRY_TYPE
// TODO: REQUIRES --std:c23 or --std:gcc23 -- Fix these to be constexpr   
// TODO: REQUIRES --std:c23 or --std:gcc23 -- Fix initialization to use above constexpr structs

typedef struct _BP_OTPDIR_ENTRY_TYPE {
    union {
        uint16_t as_uint16_t;
        struct {
            uint16_t id            : 8; // can be extended to 12-bits if ever exceed 255 types for the given encoding type
            uint16_t must_be_zero  : 4;
            uint16_t encoding_type : 4; // e.g., ECC, ECC_String, BYTE3X, RBIT3, RBIT8, RAW
        };
    };
} BP_OTPDIR_ENTRY_TYPE;
static_assert(sizeof(BP_OTPDIR_ENTRY_TYPE) == sizeof(uint16_t));

#define BP_OTPDIR_ENTRY_TYPE_END              ((BP_OTPDIR_ENTRY_TYPE){ .id = 0x00u, .encoding_type = BP_OTPDIR_DATA_ENCODING_TYPE_NONE                        })
#define BP_OTPDIR_ENTRY_TYPE_USB_WHITELABEL   ((BP_OTPDIR_ENTRY_TYPE){ .id = 0x01u, .encoding_type = BP_OTPDIR_DATA_ENCODING_TYPE_ECC                         })
#define BP_OTPDIR_ENTRY_TYPE_BP_CERTIFICATE   ((BP_OTPDIR_ENTRY_TYPE){ .id = 0x02u, .encoding_type = BP_OTPDIR_DATA_ENCODING_TYPE_ECC                         })


// When reading ECC OTP data using RAW reads, the is an opaque 32-bit value.
// Sometimes it's convenient to view as a uint32_t,
// other times it's convenient to view the individual fields.
// (ab)use anonymous unions to allow whichever use is convenient.
typedef struct _BP_OTP_RAW_READ_RESULT {
    // anonymous structs are supported in C11
    union {
        uint32_t as_uint32;
        uint8_t  as_bytes[4];
        struct {
            uint8_t lsb;
            uint8_t msb;
            union {
                struct {
                    uint8_t hamming_ecc : 5; // aka syndrome during correction
                    uint8_t parity_bit : 1;
                    uint8_t bit_repair_by_polarity : 2;
                };
                uint8_t correction;
            };
            uint8_t is_error; // 0: ok
        };
    };
} BP_OTP_RAW_READ_RESULT;
static_assert(sizeof(BP_OTP_RAW_READ_RESULT) == sizeof(uint32_t));

// Given the 16-bit value to be stored using ECC encoding,
// Get the 32-bit value (castable to BP_OTP_RAW_READ_RESULT),
// containing both the hamming_ecc and parity_bit fields.
// This function will always leave the BRBP bits as zero.
uint32_t bp_otp_calculate_ecc(uint16_t x); // [[unsequenced]]

// Given the raw value read from an OTP row containing ECC encoded data,
// this function corrects all single-bit errors, detects all double-bit errors,
// and will even detect many (most?) other bit corruptions.
// Returns a value with AT LEAST one of the 8 most significant bits set,
// as only 16 bit values can be stored using the ECC encoding.
uint32_t bp_otp_decode_raw(uint32_t data); // [[unsequenced]]

#if RPI_PLATFORM == RP2040
    // These functions actually access the hardware,
    // so code calling it on RP2040 is probably in error?
    // This is a nicer error message than a linker error....
    __attribute__((deprecated)) inline bool bp_otp_write_single_row_raw(uint16_t row, uint32_t new_value)                      { return false; }
    __attribute__((deprecated)) inline bool bp_otp_read_single_row_raw(uint16_t row, uint32_t* out_data)                       { return false; }
    __attribute__((deprecated)) inline bool bp_otp_write_single_row_ecc(uint16_t row, uint16_t new_value)                      { return false; }
    __attribute__((deprecated)) inline bool bp_otp_read_single_row_ecc(uint16_t row, uint16_t* out_data)                       { return false; }
    __attribute__((deprecated)) inline bool bp_otp_read_ecc_data(uint16_t start_row, void* out_data, size_t count_of_bytes)    { return false; }
    __attribute__((deprecated)) inline bool bp_otp_write_ecc_data(uint16_t start_row, const void* data, size_t count_of_bytes) { return false; }
    __attribute__((deprecated)) inline bool bp_otp_write_single_row_byte3x(uint16_t row, uint8_t new_value)                    { return false; }
    __attribute__((deprecated)) inline bool bp_otp_read_single_row_byte3x(uint16_t row, uint8_t* out_data)                     { return false; }
    __attribute__((deprecated)) inline bool bp_otp_write_redundant_rows_2_of_3(uint16_t start_row, uint32_t new_value)         { return false; }
    __attribute__((deprecated)) inline bool bp_otp_read_redundant_rows_2_of_3(uint16_t start_row, uint32_t* out_data)          { return false; }

    __attribute__((deprecated)) void bp_otpdir_reset_iterator(void) {}
    __attribute__((deprecated)) bool bp_otpdir_find_next_entry(void) { return false; }
    __attribute__((deprecated)) BP_OTPDIR_ENTRY_TYPE bp_otpdir_get_current_entry_type(void) { return BP_OTPDIR_ENTRY_TYPE_END; }
    __attribute__((deprecated)) size_t bp_otpdir_get_current_entry_buffer_size(void) { return 0u;}
    __attribute__((deprecated)) bool bp_otpdir_get_current_entry_data(void* buffer, size_t buffer_size) { return false; }
    __attribute__((deprecated)) bool bp_otpdir_add_ecc_entry(BP_OTPDIR_ENTRY_TYPE entryType, uint16_t start_row, size_t valid_byte_count) { return false; }

    __attribute__((deprecated)) inline void bp_otp_apply_whitelabel_data(void) { }
    __attribute__((deprecated)) inline bool bp_otp_lock_whitelabel(void) { return false; }

#elif RPI_PLATFORM == RP2350

    #pragma region    // OTP Read / Write functions

    // RP2350 OTP can encode data in multiple ways:
    // * 24 bits of raw data (no error correction / detection)
    // * 16 bits of ECC protected data (using the RP2350's standard ECC to correct 1-bit, detect 2-bit errors)
    // *  8 bits of triple-redundant data (using a majority vote to correct each bit, aka 2-of-3 voting in a single OTP ROW)
    // * Using multiple rows for N-of-M voting (e.g., boot critical fields might be recorded in eight rows...)
    // There are many edge cases when reading or writing an OTP row,

    // NOT RECOMMENDED DUE TO LIKELIHOOD OF UNDETECTED ERRORS:
    // Writes a single OTP row with 24-bits of data.  No ECC / BRBP is used.
    // Returns false if the data could not be written (e.g., if any bit is
    // already set to 1, and the new value sets that bit to zero).
    bool bp_otp_write_single_row_raw(uint16_t row, uint32_t new_value);
    // Reads a single OTP row raw, returning the 24-bits of data without interpretation.
    // Using raw encoding is NOT recommended for general use.  However,
    // reading of an OTP row as RAW can help differentiate between
    // being unable to access the OTP row (e.g., locked) vs. ECC errors
    // making the resulting values undecodable.
    bool bp_otp_read_single_row_raw(uint16_t row, uint32_t* out_data);

    // Writes a single OTP row with 16-bits of data, protected by ECC.
    // Writes will not fail due to a single bit already being set to one.
    // Returns false if the data could not be written (e.g., if more than
    // one bit was already written to 1).
    bool bp_otp_write_single_row_ecc(uint16_t row, uint16_t new_value);
    // Reads a single OTP row, applies bit recovery by polarity,
    // and corrects any single-bit errors using ECC.  Returns the
    // corrected 16-bits of data.
    // Returns false if the data could not be successfully read (e.g.,
    // uncorrectable errors detected, etc.)
    bool bp_otp_read_single_row_ecc(uint16_t row, uint16_t* out_data);

    // Write the supplied buffer to OTP, starting at the specified
    // OTP row and continuing until the buffer is fully written.
    // Allows writing an odd number of bytes, so caller does not have to
    // do extra work to ensure buffer is always an even number of bytes.
    // In this case, the extra byte written will be zero.
    bool bp_otp_write_ecc_data(uint16_t start_row, const void* data, size_t count_of_bytes);
    // Fills the supplied buffer with ECC data, starting at the specified
    // OTP row and continuing until the buffer is filled.
    // Allows reading an odd number of bytes, so caller does not have to
    // do extra work to ensure buffer is always an even number of bytes.
    bool bp_otp_read_ecc_data(uint16_t start_row, void* out_data, size_t count_of_bytes);

    // Writes a single OTP row with 8-bits of data stored with 3x redundancy.
    // For each bit of the new value that is zero:
    //   the existing OTP row is permitted to have that bit set to one in
    //   at most one of the three locations, without causing a failure.
    // After writing the new value, the function reads the value back to
    // ensure that (with voting applied) the new value was correctly stored.
    // This style of storage is mostly used for flags that are independently
    // updated over multiple OTP writes. 
    bool bp_otp_write_single_row_byte3x(uint16_t row, uint8_t new_value);
    // Reads a single OTP row with 8-bits of data stored with 3x redundancy.
    // Returns the 8-bits of data, after applying 2-of-3 voting.
    bool bp_otp_read_single_row_byte3x(uint16_t row, uint8_t* out_data);

    // Writes three consecutive rows of OTP data with same 24-bit data.
    // For each bit with a new value of zero:
    //   the existing OTP rows are permitted to have that bit set to one
    //   in one of the three rows, without this function failing.
    // After writing the new values, the function reads the value back
    // and will return false if the value (with voting applied) is not
    // the expected new value.
    bool bp_otp_write_redundant_rows_2_of_3(uint16_t start_row, uint32_t new_value);
    // Reads three consecutive rows of raw OTP data (24-bits), and applies
    // 2-of-3 voting for each bit independently.
    // Returns the 24-bits of voted-upon data.
    bool bp_otp_read_redundant_rows_2_of_3(uint16_t start_row, uint32_t* out_data);
    #pragma endregion // OTP Read / Write functions

    // Resets the OTP directory iterator to the first entry.
    // For now, iterator state is kept per-CPU.  May change later to externally-allocated state.
    void bp_otpdir_reset_iterator(void);
    // Moves the current iterator to the next entry.
    // Returns FALSE when no more entries will be enumerated.
    bool bp_otpdir_find_next_entry(void);
    // Returns the type of the current entry.
    // If the iterator is at the end, then the type is BP_OTPDIR_DATA_ENCODING_TYPE_NONE.
    BP_OTPDIR_ENTRY_TYPE bp_otpdir_get_current_entry_type(void);
    // Returns the buffer size (in bytes) required to get the data referenced by the current entry.
    // Gives a consistent API for all the various data encoding schemes (RAW, byte3x, RBIT3, RBIT8, etc.)
    size_t bp_otpdir_get_current_entry_buffer_size(void);
    // Reads the data from OTP on behalf of the caller.  If the data is successfully read (and validated,
    // for all types except RAW), the data will be in the caller-supplied buffer.
    // Automatically handles the various data encoding schemes (RAW, byte3x, RBIT3, RBIT8, etc.)
    bool bp_otpdir_get_current_entry_data(void* buffer, size_t buffer_size);

    // Adds a new entry to the OTP directory.
    // Verification includes:
    // * entry type encoding is either ECC or ECC_STRING
    // * valid_byte_count is reasonable
    // * all rows would exist within the user data OTP rows
    // * all rows are readable and encode valid ECC-encoded data
    // * for ECC_ASCII_STRING, the first NULL byte corresponds to the valid_byte_count (must be NULL terminated, and valid_byte_count must include NULL character)
    bool bp_otpdir_add_ecc_entry(BP_OTPDIR_ENTRY_TYPE entryType, uint16_t start_row, size_t valid_byte_count);


    void bp_otp_apply_whitelabel_data(void);
    bool bp_otp_lock_whitelabel(void);

#else
    #error "Unsupported platform"
#endif


#ifdef __cplusplus
}
#endif

