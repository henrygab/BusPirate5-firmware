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


#pragma region    // BP_OTP_DATA_ENCODING_TYPE
// OTP is scary enough to be worth fully hiding the details from outside code, and to make
// it REALLY hard to accidentally use wrong values.  Here, we want to ONLY support these
// few types of data encoding.  Access to the underlying values is an extra pointer dereference,
// and lots of extra typing to define these values.  For OTP ... I consider it worthwhile.
typedef struct _BP_OTP_DATA_ENCODING_TYPE BP_OTP_DATA_ENCODING_TYPE;

// Single row, 24-bits of raw data, no error correction or detection
extern const BP_OTP_DATA_ENCODING_TYPE * const BP_OTP_DATA_ENCODING_TYPE_RAW;
// Single row, 16-bits of data, ECC detects 2-bit errors, corrects 1-bit errors
// and BRBP allows writing even where a row has a single bit already set to 1
// (even if stored value would normally store zero for that bit).
extern const BP_OTP_DATA_ENCODING_TYPE * const BP_OTP_DATA_ENCODING_TYPE_ECC;
// Single row, 8-bits of data, stored triple-redundant.
// Majority voting (2-of-3) is applied for each bit independently.
extern const BP_OTP_DATA_ENCODING_TYPE * const BP_OTP_DATA_ENCODING_TYPE_BYTE3X;
// Identical data stored in three consecutive OTP rows.
// Majority voting (2-of-3) is applied for each bit independently.
// APIs should be provided the first (lowest) OTP row number.
extern const BP_OTP_DATA_ENCODING_TYPE * const BP_OTP_DATA_ENCODING_TYPE_RBIT3;
// Identical data stored in eight consecutive OTP rows.
// Special voting is applied for each bit independently:
// If 3 or more rows have the bit set, then that bit is considered set.
// This is used ONLY for the critical boot rows.
// APIs should be provided the first (lowest) OTP row number.
extern const BP_OTP_DATA_ENCODING_TYPE * const BP_OTP_DATA_ENCODING_TYPE_RBIT8;
// Identical to BP_OTP_DATA_ENCODING_TYPE_ECC, with the added requirement
// that the record's length includes at least one trailing zero byte.
// This is useful for ASCII / UTF-8 strings.
extern const BP_OTP_DATA_ENCODING_TYPE * const BP_OTP_DATA_ENCODING_TYPE_ECC_ASCII_STRING;
// Value is encoded directly within the directory entry as an (up to) 32-bit value.
// The least significant 16 bits are stored within the `start_row` field.
// The most significant 16 bits are stored within the `byte_count` field.
extern const BP_OTP_DATA_ENCODING_TYPE * const BP_OTP_DATA_ENCODING_TYPE_EMBEDED_IN_DIRENTRY;
#pragma endregion // BP_OTP_DATA_ENCODING_TYPE

#pragma region    // BP_OTP_DIRENTRY_TYPE

// For now, do the same abstraction for BP_OTP_DATA_ENCODING_TYPE
// Yes, it's more typing.  The typesafety it provides is worth it.
typedef struct _BP_OTP_DIRENTRY_TYPE BP_OTP_DIRENTRY_TYPE;
// No data is stored in this row.  Used to allow unwritten space to later be used to appended to the directory entries.
extern const BP_OTP_DATA_ENCODING_TYPE * const BP_OTP_DATA_ENCODING_TYPE_UNUSED;
// Single row, 24-bits of raw data (returned as a 32-bit value), no error correction or detection
// Note that multiple rows do NOT pack the data ... each row takes 4 bytes.
extern const BP_OTP_DATA_ENCODING_TYPE * const BP_OTP_DATA_ENCODING_TYPE_RAW;
// Single row, 16-bits of data, ECC detects 2-bit errors, corrects 1-bit errors
// and BRBP allows writing even where a row has a single bit already set to 1
// (even if stored value would normally store zero for that bit).
extern const BP_OTP_DATA_ENCODING_TYPE * const BP_OTP_DATA_ENCODING_TYPE_ECC;
// Single row, 8-bits of data, stored triple-redundant.
// Majority voting (2-of-3) is applied for each bit independently.
extern const BP_OTP_DATA_ENCODING_TYPE * const BP_OTP_DATA_ENCODING_TYPE_BYTE3X;
// Identical data stored in three consecutive OTP rows.
// Majority voting (2-of-3) is applied for each bit independently.
extern const BP_OTP_DATA_ENCODING_TYPE * const BP_OTP_DATA_ENCODING_TYPE_RBIT3;
// Identical data stored in eight consecutive OTP rows.
// Special voting is applied for each bit independently:
// If 3 or more rows have the bit set, then that bit is considered set.
// This is used ONLY for the critical boot rows.
extern const BP_OTP_DATA_ENCODING_TYPE * const BP_OTP_DATA_ENCODING_TYPE_RBIT8;
// Identical to BP_OTP_DATA_ENCODING_TYPE_ECC, with the added requirement
// that the record's length includes at least one trailing zero byte.
// This is useful for ASCII / UTF-8 strings.
extern const BP_OTP_DATA_ENCODING_TYPE * const BP_OTP_DATA_ENCODING_TYPE_ECC_ASCII_STRING;
// Value is encoded directly within the directory entry as an (up to) 32-bit value.
// The least significant 16 bits are stored within the `start_row` field.
// The most significant 16 bits are stored within the `byte_count` field.
extern const BP_OTP_DATA_ENCODING_TYPE * const BP_OTP_DATA_ENCODING_TYPE_EMBEDED_IN_DIRENTRY;

#pragma endregion // BP_OTP_DIRENTRY_TYPE


// When reading OTP data, the result is a 32-bit value.
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
typedef BP_OTP_RAW_READ_RESULT OTP_RAW_READ_RESULT; // TODO -- remove this old name for the struct
static_assert(sizeof(BP_OTP_RAW_READ_RESULT) == sizeof(uint32_t));

uint32_t bp_otp_calculate_ecc(uint16_t x); // [[unsequenced]]
uint32_t bp_otp_decode_raw(uint32_t data); // [[unsequenced]]

#if RPI_PLATFORM == RP2040
    // These functions actually access the hardware,
    // so code calling it on RP2040 is probably in error?
    // This is a nicer error message than a linker error....
    __attribute__((deprecated)) inline void bp_otp_apply_whitelabel_data(void) { }
    __attribute__((deprecated)) inline bool bp_otp_lock_whitelabel(void) { return false; }

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

#elif RPI_PLATFORM == RP2350
    void bp_otp_apply_whitelabel_data(void);
    bool bp_otp_lock_whitelabel(void);

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
    // NOT RECOMMENDED DUE TO LIKELIHOOD OF UNDETECTED ERRORS:
    // Reads a single OTP row raw, returning the 24-bits of data without interpretation.
    // Not recommended for general use.
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

#endif


#ifdef __cplusplus
}
#endif

