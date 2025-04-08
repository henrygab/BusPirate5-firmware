#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <pico/unique_id.h> // for pico_get_unique_board_id() and pico_unique_board_id_t
#include <pico/stdlib.h> // for sleep_ms()
#include "saferotp.h"
#include "saferotp_ecc.h"
#include "debug_rtt.h"

#define BP_DEBUG_OVERRIDE_DEFAULT_CATEGORY BP_DEBUG_CAT_OTP

// GCC is awesome ... this is the ___type-safe___ array element count macro
#define ARRAY_SIZE(arr) \
    (sizeof(arr) / sizeof((arr)[0]) \
     + sizeof(typeof(int[1 - 2 * \
           !!__builtin_types_compatible_p(typeof(arr), \
                 typeof(&arr[0]))])) * 0)


void test_otp_subsystem(void) {

    // 
    sleep_ms(500); // sleep for 4 seconds ... allow time for debugger to connect, etc.
    BP_DEBUG_PRINTF("OTP subsystem test\n");

    // First, let's read the existing OTP values from pages 0x000 .. 0x139
    BP_DEBUG_PRINTF("Caching first 0x140 OTP pages (real values)\n");
    SAFEROTP_RAW_READ_RESULT real_values[0x140]; // ~1.1k stack...
    for (uint32_t i = 0; i < ARRAY_SIZE(real_values); ++i) {
        if (!saferotp_read_single_row_raw(i, &real_values[i].as_uint32)) {
            BP_DEBUG_PRINTF("OTP Failed to read row %03x\n", i);
            real_values[i].as_uint32 = 0xFFFFFFFFu;
        }
    }

    // Initialize the OTP virtualization layer
    BP_DEBUG_PRINTF("Initializing virtualized OTP layer\n");
    if (!saferotp_virtualization_init_pages(0u)) {
        BP_DEBUG_PRINTF("OTP Failed to initialize virtualization layer\n");
        return;
    }

    // read some of those sectors using the virtualization layer
    BP_DEBUG_PRINTF("Reading the first eight virtualized OTP rows (serial number)\n");
    typedef union _cpu_serial_number_t {
        pico_unique_board_id_t pico_id;
        uint64_t as_uint64;
        uint8_t  as_uint8[8];
    } cpu_serial_number_t;
    _Static_assert(sizeof(pico_unique_board_id_t) == sizeof(uint64_t), "Unique ID size mismatch");
    cpu_serial_number_t id1 = {0};
    pico_get_unique_board_id(&id1.pico_id);
    cpu_serial_number_t id1r = { .as_uint8 = { // reverse the byte order ...
        id1.as_uint8[7], id1.as_uint8[6], id1.as_uint8[5], id1.as_uint8[4],
        id1.as_uint8[3], id1.as_uint8[2], id1.as_uint8[1], id1.as_uint8[0],
    }};
    cpu_serial_number_t id2 = {0};
    if (!saferotp_read_ecc_data(0, &id2, sizeof(id2))) {
        BP_DEBUG_PRINTF("OTP Failed to read virtualized serial number\n");
        return;
    } else if ((id2.as_uint64 != id1.as_uint64) && (id2.as_uint64 != id1r.as_uint64)) {
        BP_DEBUG_PRINTF("OTP virtualized serial number mismatch: %016X != %016X\n", id2.as_uint64, id1.as_uint64);
        return;
    }

    // read back the original cached data
    BP_DEBUG_PRINTF("Re-reading the first 0x140 OTP pages (virtualized values)\n");
    for (uint32_t i = 0; i < ARRAY_SIZE(real_values); ++i) {
        SAFEROTP_RAW_READ_RESULT tmp = { 0 };
        if (!saferotp_read_single_row_raw(i, &tmp.as_uint32)) {
            BP_DEBUG_PRINTF("OTP Failed to read virtualilzed row %03x\n", i);
            return;
        } else if (tmp.as_uint32 != real_values[i].as_uint32) {
            BP_DEBUG_PRINTF("OTP virtualized row %03x mismatch: %08X != %08X\n", i, tmp.as_uint32, real_values[i].as_uint32);
            return;
        }
    }

    // Write to many OTP rows, using ECC data
    BP_DEBUG_PRINTF("Writing (virtualized) OTP rows 0x200 .. 0x21F\n");
    uint16_t write_start_row = 0x200;
    for (uint16_t row = 0x200; row < 0x220; ++row) {
        uint32_t raw_data = saferotp_calculate_ecc(row);
        if (!saferotp_write_single_row_ecc(row, row)) {
            BP_DEBUG_PRINTF("OTP Failed to write row %03x with data %06x\n", row, raw_data);
            return;
        }
    }
    // Verify the data "stuck", using ECC data
    BP_DEBUG_PRINTF("Verifying data written to virtualized rows `stuck`\n");
    for (uint16_t row = 0x200; row < 0x220; ++row) {
        uint32_t raw_data = saferotp_calculate_ecc(row);
        SAFEROTP_RAW_READ_RESULT tmp_raw = { 0 };
        uint16_t tmp = 0;
        if (!saferotp_read_single_row_raw(row, &tmp_raw.as_uint32)) {
            BP_DEBUG_PRINTF("OTP Failed to read virtualized row %03x raw\n", row);
            return;
        } else if (tmp_raw.as_uint32 == raw_data) {
            // OK, this is good!
        } else if (tmp_raw.as_uint32 == real_values[row].as_uint32) {
            BP_DEBUG_PRINTF("OTP virtualized row %03x pretended to write, but retained original value of %06X ?!?!\n", row, tmp_raw.as_uint32);
            return;
        } else {
            BP_DEBUG_PRINTF("OTP virtualized row %03x was written %06x, but now stores %06x ?!?!\n", row, raw_data, tmp_raw.as_uint32 );
            return;
        }

        if (!saferotp_read_single_row_ecc(row, &tmp)) {
            BP_DEBUG_PRINTF("OTP Failed to read virtualized row %03x\n", row);
            return;
        } else if (tmp != row) {
            BP_DEBUG_PRINTF("OTP virtualized row %03x mismatch (ECC): read %04X\n", row, tmp);
            return;
        }
    }
    // Verify cannot write to the same row with different data
    BP_DEBUG_PRINTF("Verifying unable to write inverted data\n");
    for (uint16_t row = 0x200; row < 0x220; ++row) {
        uint32_t raw_data = saferotp_calculate_ecc(row);
        // now try to write the inverted data to the same row (as raw)
        uint32_t raw_data_inverted = (~raw_data) & 0xFFFFFFu;
        if (saferotp_write_single_row_raw(row, raw_data_inverted)) {
            BP_DEBUG_PRINTF("OTP Permitted write of row %03x containing %06x --> %06x --- ERROR!\n", row);
            return;
        }
    }

    // Just to mess with restoration, write zero to the first 0x10 pages
    BP_DEBUG_PRINTF("Writing 0xFFFFFFu to first sixteen (virtualized) OTP rows\n");
    if (true) {
        uint32_t all_bits_set = 0xFFFFFFu;
        for (uint16_t i = 0; i < 0x10u; ++i) {
            if (!saferotp_write_single_row_raw(i, all_bits_set)) {
                BP_DEBUG_PRINTF("OTP Failed to overwrite row %03x with 0xFFFFFF\n", i);
                return;
            }
        }
        for (uint16_t i = 0; i < 0x10u; ++i) {
            uint32_t tmp = 0;
            if (!saferotp_read_single_row_raw(i, &tmp)) {
                BP_DEBUG_PRINTF("OTP Failed to read 0xFFFFFF overwritten row %03x\n", i);
                return;
            } else if (tmp != all_bits_set) {
                BP_DEBUG_PRINTF("OTP failed to keep value 0xFFFFFF for virtualized row %03x: %06X\n", i, tmp);
                return;
            }
        }
    }
    // Restore back original values
    BP_DEBUG_PRINTF("Using restore functionality to restore original OTP values\n");
    if (!saferotp_virtualization_restore(0, real_values, sizeof(real_values))) {
        BP_DEBUG_PRINTF("OTP Failed to restore original values\n");
        return;
    }
    // Verify the data was restored
    BP_DEBUG_PRINTF("Verifying restored data\n");
    for (uint16_t i = 0; i < 0x10u; ++i) {
        uint32_t tmp = 0;
        if (!saferotp_read_single_row_raw(i, &tmp)) {
            BP_DEBUG_PRINTF("OTP Failed to read 0xFFFFFF overwritten row %03x\n", i);
            return;
        } else if (tmp != real_values[i].as_uint32) {
            BP_DEBUG_PRINTF("OTP failed to restore row %03x ... expected %06x, but got %06x\n", i, real_values[i].as_uint32, tmp);
            return;
        }
    }

    BP_DEBUG_PRINTF("Self-test of OTP subsystem: SUCCESS\n");
    return;
}
