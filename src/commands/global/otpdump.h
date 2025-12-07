#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

// NOTE: RP2350 SDK headers have lots of names starting with `OTP_`.
//       To make it easier to see local symbols and to avoid naming
//       conflicts, names here are instead prefixed with `XOTP_`.

#pragma region    // XOTP_ECC_TYPE enum, XOTP_DIRENTRY_TYPE struct, and XOTP_DIRECTORY_ITEM struct 

// Supported types of OTP entries for the OTP directory
// Bit 15,14 === must be zero when read and when written
// Bit 13,12 === XOTP_ECC_TYPE enumeration
//               0b00 == Stored with ECC
//               0b01 == Stored as raw data (undefined redundancy)
//               0b10 == Stored as raw data, with three copies of the same byte in each OTP row
//               0b11 == Stored as raw data, with multiple copies at the row level (e.g., RBIT-3, RBIT-8)
typedef enum _XOTP_ECC_TYPE { // two-bit field indicating how data is stored
    OTP_ECC_TYPE_STANDARD  = 0x0u, // ECC; single-bit correction, dual-bit detection
    OTP_ECC_TYPE_UNDEFINED = 0x1u, // RAW; redundancy unspecified
    OTP_ECC_TYPE_3X_1BYTE  = 0x2u, // RAW; each row stores three copies of a single byte of data
    OTP_ECC_TYPE_MULTI_ROW = 0x3u, // RAW; multiple copies of the same row (e.g., RBIT-3 or RBIT-8)
} XOTP_ECC_TYPE;
typedef struct _XOTP_DIRENTRY_TYPE {
    union {
        uint16_t as_uint16;
        struct {
            uint16_t              : 12; // this field, by itself, is not guaranteed to be unique and thus not particularly useful to expose
            uint16_t ecc_type     :  2; // 0b00 == ECC, 0b01 == Raw, 0b10 == 3x 1byte, 0b11 == RBIT-3/RBIT-8
            uint16_t must_be_zero :  2; // RFU; if non-zero, the entry is invalid
        };
    };
} XOTP_DIRENTRY_TYPE;
static_assert(sizeof(XOTP_DIRENTRY_TYPE) == sizeof(uint16_t), "XOTP_DIRENTRY_TYPE must be exactly sizeof(uint16_t)");

typedef struct _XOTP_DIRECTORY_ITEM {
    XOTP_DIRENTRY_TYPE EntryType;
    uint16_t StartRow;
    uint16_t RowCount; // MAY BE ZERO ... if so, StartRow may contain any value.
    uint16_t CRC16;
} XOTP_DIRECTORY_ITEM;
#pragma endregion // XOTP_ECC_TYPE, XOTP_DIRENTRY_TYPE, and XOTP_DIRECTORY_ITEM Definitions
#pragma region    // Helper functions for iterating through the OTP directory, reading data

// NOTE: there is a distinct iterator tracked for each core.
void xotp_reset_directory_iterator();
// Returns true when the next directory entry was found.
// Returns false when:
// * An explicit "sealed" directory entry is found (no more entries allowed to be added)
// * An all-zero (blank) entry is found
// * An entry with invalid CRC16 is found
// Entries which cannot be read due to ECC errors are silently skipped.
// Thus, if programming fails, use RAW mode to write data that has ECC errors.
bool xotp_get_next_directory_entry(XOTP_DIRECTORY_ITEM* out_direntry);
// Helper function that simply calls `xotp_get_next_directory_entry()`,
// skipping entries that are not of the requested type.
bool xotp_find_directory_item(XOTP_DIRENTRY_TYPE type, XOTP_DIRECTORY_ITEM* out_direntry);
// Helper function to read the data referenced by a direntry item.
// Note that `direntry->ecctype` indicates if data is read with ECC or as raw data.
// The entire buffer will be zero'd prior to filling with OTP data.
// The buffer length provided must be exactly equal to the required length for the entry.
//
// Returns true if:
// * all the data indicated by the `direntry` was successfully read
// Returns false for any of:
// * invalid parameters
//     * the direntry CRC16 is invalid
//     * null pointer parameters
//     * buffer length insufficient to store all the data
// * Read of any OTP row resulted in an error
bool xotp_get_directory_item_data(const XOTP_DIRECTORY_ITEM* direntry, void* out_buffer, size_t buffer_len);

#pragma endregion // Helper functions for iterating through the OTP directory, reading data...
#pragma region    // Individual XOTP_DIRENTRY_TYPE definitions
static const XOTP_DIRENTRY_TYPE XOTP_DIRTYPE_BLANK           = { .as_uint16 = 0x0000u }; // unprogrammed page ... no data ... end of directory list
static const XOTP_DIRENTRY_TYPE XOTP_DIRTYPE_USB_WHITELABEL  = { .as_uint16 = 0x0001u }; // ECC, structure used to whitelabel (branding) bootrom
static const XOTP_DIRENTRY_TYPE XOTP_DIRTYPE_PROVENANCE_X509 = { .as_uint16 = 0x0002u }; // ECC, x509 certificate showing provenance of the device
static const XOTP_DIRENTRY_TYPE XOTP_DIRTYPE_ECC_ERROR       = { .as_uint16 = 0xFFFFu }; // ECC, No data, ECC error occurred when reading
#pragma endregion // XOTP_DIRENTRY_TYPE Definitions
#pragma region    // Low-level OTP read/write helper functions
// ... read / verify redundancy per XOTP_ECC_TYPE ... ?
// ... maybe separate functions for each XOTP_ECC_TYPE ... ?
#pragma endregion // Low-level OTP read/write helper functions


// command handler
void otpdump_handler(struct command_result* res);