#define BP_DEBUG_OVERRIDE_DEFAULT_CATEGORY BP_DEBUG_CAT_CERTIFICATE

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pirate.h"
#include "command_struct.h"       // File system related
#include "fatfs/ff.h"       // File system related
#include "pirate/storage.h" // File system related
#include "ui/ui_cmdln.h"    // This file is needed for the command line parsing functions
// #include "ui/ui_prompt.h" // User prompts and menu system
// #include "ui/ui_const.h"  // Constants and strings
#include "ui/ui_help.h"    // Functions to display help in a standardized way
#include "system_config.h" // Stores current Bus Pirate system configuration
#include "pirate/amux.h"   // Analog voltage measurement functions
#include "pirate/button.h" // Button press functions
#include "msc_disk.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"
#include "mbedtls/error.h"
#include "mbedtls/md_internal.h"
#include "mbedtls/oid.h"
#include "cert.h"
#include "mbedtls/md.h"
#include "mbedtls/pem.h"
#include "pico/unique_id.h"
#include "otp/bp_otp.h"
#include "pubkey/pubkey.h"
#include <boot/bootrom_constants.h>
#include <pico/bootrom.h>

 
#define BYTES_PER_OTP_PAGE 128
#define ROWS_PER_OTP_PAGE   64

#define BP_OTP_CERT_ROW 0x100  

// This array of strings is used to display help USAGE examples for the dummy command
static const char* const usage[] = { "cert [-d display] [-b burn <filename>] ",
                                     "Validate Bus Pirate x509 cert: cert",
                                     "Display cert and public key: cert -d",
                                     "Burn cert from DER file: cert -b cert.der" };

static const struct ui_help_options options[] = {
/*    { 1, "", T_HELP_DUMMY_COMMANDS },    // section heading
    { 0, "init", T_HELP_DUMMY_INIT },    // init is an example we'll find by position
    { 0, "test", T_HELP_DUMMY_TEST },    // test is an example we'll find by position
    { 1, "", T_HELP_DUMMY_FLAGS },       // section heading for flags
    { 0, "-b", T_HELP_DUMMY_B_FLAG },    //-a flag, with no optional string or integer
    { 0, "-i", T_HELP_DUMMY_I_FLAG },    //-b flag, with optional integer
    { 0, "-f", T_HELP_DUMMY_FILE_FLAG }, //-f flag, a file name string*/
    {0, "-h", T_HELP_FLAG}
};

static void print_x509_info(const mbedtls_x509_crt *cert, char *buf, int buflen) {
    // Print the subject name
    mbedtls_x509_dn_gets(buf, buflen, &cert->subject);
    printf("Subject: %s\r\n", buf);

    // Print the issuer name
    mbedtls_x509_dn_gets(buf, buflen, &cert->issuer);
    printf("Issuer: %s\r\n", buf);

    // Print the validity period
    const mbedtls_x509_time *valid_from = &cert->valid_from;
    const mbedtls_x509_time *valid_to = &cert->valid_to;
    printf("Valid from: %04d-%02d-%02d %02d:%02d:%02d\r\n",
           valid_from->year, valid_from->mon, valid_from->day,
           valid_from->hour, valid_from->min, valid_from->sec);
    printf("Valid to: %04d-%02d-%02d %02d:%02d:%02d\r\n",
           valid_to->year, valid_to->mon, valid_to->day,
           valid_to->hour, valid_to->min, valid_to->sec);

    // Print the serial number
    mbedtls_x509_serial_gets(buf, buflen, &cert->serial);
    printf("Serial Number: %s\r\n", buf);
}

// find the cert length
// TODO - rename this to reflect the variable-length encoding format used, as it's applicable to things other than DER
//      - rename to reflect this reports the required bytes for the encoded variable-length field, INCLUDING the header bytes
//      - TODO: should this not also send back the pointer to the actual buffer, without the header bytes?
static size_t cert_length(const char *der, size_t buffer_size) {
    /*
    The first byte must be 0x30.
    The next byte is the length (maybe):
        If der[1] <= 0x80, then der[1] is count of remaining bytes (up to 128 more bytes).                     Add two   for the "header" bytes.
        If der[1] == 0x81, then der[2] + 128 is the count of remaining bytes (up to 255 more bytes).           Add three for the "header" bytes. 
        If der[1] == 0x82, then der[2..3] store big-endian count of remaining bytes (up to 65535 more bytes).  Add four  for the "header" bytes.
    */
    size_t data_length = 0;
    uint8_t header_byte_count = 0;
    if (buffer_size < 2) {
        // insufficient buffer to determine buffer length
        // do nothing ...
    } else if ((der[1] <= 0x80) && (buffer_size >= 2)) {
        header_byte_count = 2;
        data_length       = der[1];
    } else if ((der[1] == 0x80) && (buffer_size >= 3)) {
        header_byte_count = 3;
        data_length       = der[2] + 128u;
    } else if ((der[1] == 0x81) && (buffer_size >= 4)) {
        // length is stored in BIG ENDIAN format in der[2..3]
        header_byte_count = 4;
        data_length       = ((der[2] << 8) | der[3]);
    } else {
        // unhandled DER format
    }
    // This API is currently just checking the total size required for the buffer to contain the entirety of the header + data field
    return data_length + header_byte_count;
}

// returns TRUE if the cert serial number could be fully copied to the specified buffer.
// returns FALSE on all other errors.
static bool cert_serial_get(const mbedtls_x509_crt *cert, char * serial_out, size_t serial_buffersize) {

    memset(serial_out, 0, serial_buffersize);

    bool failure = false;
    uint8_t bytes_copied = 0;
    // is the buffer large enough to hold the serial number?
    size_t cert_serial_length = cert->serial.len;
    for (uint i = 0; (!failure) && (i < cert_serial_length); ++i) {
        // What is this nasty hack?  Why are we skipping the first byte if it's zero?
        // Why is this copying all other bytes of zero, but not the first one?
        // Was the intent to trim all the leading zero data? (if so, should this be `cnt == 0` instead of `i == 0`?)
        // FINAL: Presuming intent was to trim all leading zero bytes
        if (bytes_copied == 0 && cert->serial.p[i] == 0x0) {
            continue;
        }

        if (bytes_copied >= serial_buffersize) {
            // buffer is too small ... mark this as a failure
            failure = true;
        } else {
            serial_out[bytes_copied] = cert->serial.p[i];
            bytes_copied++;
        }
    }

    // If anything failed, zero out the buffer
    if (failure) {
        memset(serial_out, 0, serial_buffersize);
        bytes_copied = 0;
    }
    return !failure;
}

bool cert_extract_oid(const mbedtls_x509_name *name, const char *oid, char *buf, uint buflen) {
    size_t i, j;
    unsigned char c;
    char s[MBEDTLS_X509_MAX_DN_NAME_SIZE], *p;

    memset(s, 0, sizeof(s));

    //name = dn;
    //p = buf;
    while (name != NULL) {
        // Next line's `MBEDTLS_OID_SIZE(oid)` is AN UNSAFE MACRO, USED IN UNSAFE MANNER
        // Specifically, MBEDTLS_OID_SIZE(x) is defined as `(sizeof(x) - 1)`.
        // This means the parameter is PRESUMED to be a local array.
        // However, here, it is passed a pointer to a string, which is NOT an array.
        // Therefore, `MBEDTLS_OID_SIZE(oid)` will always return sizeof(pointer) - 1.
        // This is likely NOT the intended result.
        if (name->oid.p != NULL &&
            name->oid.len == MBEDTLS_OID_SIZE(oid) &&
            memcmp(name->oid.p, oid, MBEDTLS_OID_SIZE(oid)) == 0
        ) {           
            for (i = 0, j = 0; i < name->val.len; i++, j++) {
                if (j >= buflen - 1) {
                    return MBEDTLS_ERR_X509_BUFFER_TOO_SMALL;
                }

                c = name->val.p[i];
                // Special characters requiring escaping, RFC 1779
                if (c && strchr(",=+<>#;\"\\", c)) {
                    if (j + 1 >= buflen - 1) {
                        return MBEDTLS_ERR_X509_BUFFER_TOO_SMALL;
                    }
                    buf[j++] = '\\';
                }
                if (c < 32 || c >= 127) {
                    buf[j] = '?';
                } else {
                    buf[j] = c;
                }
            }
            buf[j] = '\0';
            //printf("OID: %s\r\n", s);
            return true;

        }
        name = name->next;
    }

    return false;
}

// TODO: common OTP function to verify status of rows as ecc-writable (one set bit is permitted w/o loss of fidelity)
//       Or, is this just looking for unused rows?
int32_t otp_verify_empty(uint32_t start_row, uint32_t length_rows, bool debug) {
    hard_assert(false);
}

// write the cert to OTP
// 1. Find a region of OTP that is empty, unlocked, and writable as ECC data
//    Round up to page size if locking the region (avoids locking someone else's stuff!)
// 2. Write the certificate
// 3. Lock the pages used by the certificate
int32_t otp_write_verify_ecc(uint16_t start_row, uint16_t length_bytes, char *data, bool lock, bool debug, bool simulate) {
    int32_t ret;

    if(simulate && debug) {
        printf("Simulating OTP write, no data will be written...\r\n");
    }

    // NO!!! Not permitted to access past the buffer's size.  Caller MUST provide a sufficiently sized buffer,
    // or this function must use stack to write the last byte with an extra byte of padding.
    // ensure ecc data is multiple of 2
    // if (length_bytes % 2 != 0) length_bytes++;

    // calculate the number of rows needed
    uint32_t length_rows = (length_bytes / 2u) + ((length_bytes % 2u) ? 1u : 0u);

    #pragma region    // find writable region of OTP of sufficient size ... and empty / writable with ECC data; writable defined as ... ????

    // are the rows empty and writable as ECC data?
    ret = otp_verify_empty(start_row, length_rows, debug);
    if (ret != 0) {
        if(debug) {
            printf("OTP rows are not empty\r\n");
        }
        if(!simulate) {
            return ret;
        }
    }

    // Write protection, secure-mode vs. non-secure mode vs. bootloader, etc. ... once the area is known writable, just try to write it.

    // are the rows write protected?
    // calculate the number of rows needed
    // find the protection row index

    // Calculate the number of pages required
    // NOTE: If allowing to write to non-page-aligned region, this will need further adjustment
    uint32_t num_pages = (length_bytes / BYTES_PER_OTP_PAGE) + ((length_bytes % BYTES_PER_OTP_PAGE) == 0u ? 0u : 1u);

    // find the protection row index
    uint32_t start_page = start_row/64;
    uint32_t end_page = (start_page + num_pages)-1;
    uint32_t lock_start_row = 0xf80+(start_page*2);
    uint32_t lock_end_row = 0xf80+(end_page*2)+1;
    if (lock) {
        if (debug) {
            printf("Number of OTP pages required: %d\r\n", num_pages);    
            printf("Protected pages: 0x%03x : 0x%03x\r\n", start_page, end_page);
            printf("First protection row to program: 0x%03x\r\n", lock_start_row);
            printf("Last protection row to program: 0x%03x\r\n", lock_end_row);
        }
        ret = otp_verify_empty(lock_start_row, num_pages*2, debug); 
        if (ret != 0) {
            if (debug) {
                printf("Protection rows are not empty\r\n");
            }
            if (!simulate) {
                return ret;
            }
        } else {
            if (debug) {
                printf("Protection rows are empty\r\n");
            }
        }
    }
    #pragma endregion // find writable region of OTP of sufficient size ... and empty / writable with ECC data; writable defined as ... ????

    if (debug) {
        printf("OK. Ready to burn!\r\n");
    }

    // write to OTP (bp_otp_write_ecc_... functions automatically verify the writes)
    if (!simulate) {
        // write the certificate itself with ECC to the discovered location
        if (!bp_otp_write_ecc_data(start_row, data, length_bytes)) {
            if(debug) printf("Failed to write to OTP\r\n");
            return -1;
        }
    }

    if (debug) {
        printf("OTP written and verified\r\n");
    }

    // burn the protection rows
    // must be 4 bytes, last is unused
    // TODO: helper function that locks a single OTP page
    if (lock) {
        if (debug) {
            printf("NYI - locking of pages\r\n");
        }
        ret -400;
        // char raw_write_data_lock_0[4] = {0x3F, 0x3F, 0x3F, 0x00};
        // char raw_write_data_lock_1[4] = {0x15, 0x15, 0x15, 0x00};
        // for (uint32_t i=0; i < (num_pages); i++) {
        //     // burn lock_0
        //     uint32_t lock_0_row = lock_start_row + (i*2);
        //     if(debug) printf("Writing protection row 0x%03x\r\n", lock_0_row);
        //     if(!simulate){
        //         ret = cert_otp_row_write_verify_raw(lock_0_row, raw_write_data_lock_0);
        //         if (ret != 0) {
        //             if(debug) printf("Failed to write/verify protection row 0x%03x\r\n", lock_0_row);
        //             return ret;
        //         }
        //     }
        //     // burn lock_1
        //     uint32_t lock_1_row = (lock_start_row + (i*2)+1);
        //     if(debug) printf("Writing protection row 0x%03x\r\n", lock_1_row);
        //     if(!simulate){
        //         ret = cert_otp_row_write_verify_raw(lock_1_row, raw_write_data_lock_1);
        //         if (ret != 0) {
        //             if(debug) printf("Failed to write/verify protection row 0x%03x\r\n", lock_1_row);
        //             return ret;
        //         }
        //     }
        // }
        // if(debug) printf("OTP protection rows verified\r\n");
    }
    
    if(debug) printf("Success! Data burned to OTP\r\n");
    return 0x00;
}

// write the cert to OTP
// Um ... what is the contract for this?  What are the inputs?  How long are each of the buffers?
// Are these null-terminated strings on input?  If no, why not `(void*)` instead of `char*`?

bool cert_write_to_otp_new(
    const void* cert_der,   size_t cert_der_buffersize,   // each in count of bytes, enforced by void*
    const void* pubkey_der, size_t pubkey_der_buffersize, // each in count of bytes, enforced by void*
    void* bufv,        size_t buffersize,            // each in count of bytes, enforced by void*
    bool debug
) {
    uint8_t* buf = (uint8_t*)bufv;


    uint16_t ecc_row = BP_OTP_CERT_ROW;
    int8_t ret;
    mbedtls_x509_crt cert;
    mbedtls_pk_context public_key;
    unsigned char hash[32];
    
    size_t pubkey_der_len = cert_length(pubkey_der, pubkey_der_buffersize); 
    if (pubkey_der_len == 0) {
        if (debug) {
            printf("Invalid pubkey length (zero)\r\n");
        }
        return 0x100;
    }
    if (pubkey_der_len > pubkey_der_buffersize) {
        if (debug) {
            printf("Invalid pubkey length (too long for buffer)\r\n");
        }
        return 0x200;
    }

    uint32_t cert_der_len = cert_length(cert_der, cert_der_buffersize);
    if (cert_der_len == 0) {
        if (debug) {
            printf("Invalid cert length\r\n");
        }
        return 0x101;
    }
    if (cert_der_len > cert_der_buffersize) {
        if (debug) {
            printf("Invalid cert length (too long for buffer)\r\n");
        }
        return 0x201;
    }


    // WARNING!  These ALLOCATE memory, and require explicit calls to release that memory.
    //           AVOID writing code with lots of early-exit locations.
    //           Instead, try for a single exit point, or wrap the remaining operations
    //           in its own function so that any return path will still call the corresponding
    //           memory release functions.
    mbedtls_x509_crt_init(&cert);
    mbedtls_pk_init(&public_key);

    // preflight check!
    // is cert valid?
    ret = mbedtls_x509_crt_parse(&cert, cert_der, cert_der_len);
    if (ret != 0){
        if (debug) {
            printf("Error parsing cert\r\n");
        }
        return ret;
    }

    ret = mbedtls_pk_parse_public_key(&public_key, pubkey_der, pubkey_der_len);
    if (ret != 0) {
        if (debug) {
            printf("Error parsing public key\r\n");
        }
        mbedtls_x509_crt_free(&cert);
        return ret;
    }
#if 0
    // Compute the SHA-256 hash of the TBS (to-be-signed) part of the certificate
    const mbedtls_md_info_t *mdinfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    ret = mbedtls_md(mdinfo, cert.tbs.p, cert.tbs.len, hash);
    if (ret != 0) {
        if(debug) printf("Error computing hash\r\n");
        mbedtls_x509_crt_free(&cert);
        mbedtls_pk_free(&public_key);
        return ret;   
    }

    // Verify the certificate signature using the public key
    ret = mbedtls_pk_verify(&public_key, MBEDTLS_MD_SHA256, hash, 0, cert.sig.p, cert.sig.len);
    if (ret != 0) {
        if(debug) printf("Error verifying cert\r\n");
        mbedtls_x509_crt_free(&cert);
        mbedtls_pk_free(&public_key);
        return ret;   
    }
    
    if(debug) printf("Certificate verified!\r\n");

    // is cert serial match the one in OTP?
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    char cert_serial[PICO_UNIQUE_BOARD_ID_SIZE_BYTES];
    cert_serial_get(&cert, cert_serial, sizeof(cert_serial));
    //verify that the serial number in the certificate matches the RP2040 unique serial number
    if (!memcmp(cert_serial, id.id, PICO_UNIQUE_BOARD_ID_SIZE_BYTES) == 0) {
        if(debug) printf("Serial number mismatch\r\n");
        mbedtls_x509_crt_free(&cert);
        mbedtls_pk_free(&public_key);
        return 0x200;
    }
    if(debug) printf("Serial number verified!\r\n");
#else
    if(debug) printf("WARNING: No validation of certificate or serial number!\r\n");

#endif
    
    // extract the cert info and save to OTP  "info" page
    //struct for loop processing OID info
    const struct oid_info{
        const char token;
        const char *oid;
        const mbedtls_x509_name *name;
    } oid_info[] = {
        {0x01, MBEDTLS_OID_AT_STATE, &cert.subject},
        {0x02, MBEDTLS_OID_AT_LOCALITY, &cert.subject},
        {0x03, MBEDTLS_OID_AT_ORGANIZATION, &cert.subject},
        {0x04, MBEDTLS_OID_AT_COUNTRY, &cert.subject},
        {0x05, MBEDTLS_OID_AT_ORGANIZATION, &cert.issuer},
    };

    size_t offset = 0;
    char oid_buf[20];
    for(uint32_t i = 0; i < count_of(oid_info); i++){
        if (cert_extract_oid(oid_info[i].name, oid_info[i].oid, oid_buf, sizeof(oid_buf))) {
            //printf("OID %d: %s\r\n", oid_info[i].token, oid_buf);
            // UNSAFE: No buffer size checking (buf)...
            //         This is a buffer overflows waiting to happen,
            //         either now, or when someone modifies the code.
            buf[offset++] = oid_info[i].token;
            offset += snprintf(&buf[offset], buffersize - offset, "%s", oid_buf); // presumes buffersize > offset ... else buffer overflow
            buf[offset++] = 0x00; // doesn't snprintf() ensure null-termination?  Why is this here?
        } else {
            printf("OID %d: Not found\r\n", oid_info[i].token);
            return 0x600;
        }
    }
    do {
        const mbedtls_x509_time *valid_from = &cert.valid_from;
        // UNSAFE: buffer size checks are inadequate
        buf[offset++] = 0x06;
        offset += snprintf(buf + offset, buffersize - offset,
                        "%04d-%02d-%02d %02d:%02d:%02d",
                        valid_from->year, valid_from->mon, valid_from->day,
                        valid_from->hour, valid_from->min, valid_from->sec); 
        buf[offset++] = 0x00;
    } while(0);

    // print the buffer that was generated by the above code
    for(uint32_t i = 0; i < offset; i++) {
        if(buf[i]>=' ' && buf[i] <= '~'){
            printf("%c", buf[i]);
        }else{
            printf(":0x%02X:", buf[i]);
        }
    }
    printf("\r\n");

    // burn to OTP
    if (debug) {
        printf("Writing info to OTP\r\n");
    }
    uint16_t info_row = 0x2c0; // BUGBUG -- what is this magic number?  Where is it from?
    // WHY WAS THIS USING BOTH OTP_CMD_ECC_BITS __AND__ OTP_CMD_WRITE_BITS ????
    if (!bp_otp_write_ecc_data(info_row, buf, offset)) {
        if (debug) {
            printf("Failed to write info to OTP\r\n");
        }
        mbedtls_pk_free(&public_key);
        mbedtls_x509_crt_free(&cert);
        return 0x700;
    }

    if (debug) {
        printf("Info written to OTP\r\n");
    }
    return 0;

    // burn cert to OTP
    if (!bp_otp_write_ecc_data(ecc_row, cert_der, cert_der_len)) {
        if (debug) {
            printf("Failed to write cert to OTP\r\n");
        }
        mbedtls_pk_free(&public_key);
        mbedtls_x509_crt_free(&cert);
        return 0x780;
    }
    
    if (debug) {
        printf("OTP cert verified\r\n");
    }
    mbedtls_pk_free(&public_key);
    mbedtls_x509_crt_free(&cert);    
    return 0x00;
}

/*
* Base64 encoding/decoding (RFC1341)
* Copyright (c) 2005-2011, Jouni Malinen <j@w1.fi>
*
* This software may be distributed under the terms of the BSD license.
* See README for more details.
*/

// 2016-12-12 - Gaspard Petit : Slightly modified to return a std::string 
// instead of a buffer allocated with malloc.
// 2025 - For Bus Pirate project

static const unsigned char base64_table[65] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
* base64_encode - Base64 encode
* @src: Data to be encoded
* @len: Length of the data to be encoded
* @out_len: Pointer to output length variable, or %NULL if not used
* Returns: Allocated buffer of out_len bytes of encoded data,
* or empty string on failure
*/
bool base64_encode(const unsigned char *src, size_t len, const char *header)
{
    unsigned char *out, *pos;
    const unsigned char *end, *in;
    char line[4];
    uint8_t char_cnt = 0;

    end = src + len;
    in = src;

    printf("-----BEGIN %s-----\r\n", header);
    //pos = out;
    while (end - in >= 3) {
        line[0] = base64_table[in[0] >> 2];
        line[1] = base64_table[((in[0] & 0x03) << 4) | (in[1] >> 4)];
        line[2] = base64_table[((in[1] & 0x0f) << 2) | (in[2] >> 6)];
        line[3] = base64_table[in[2] & 0x3f];
        in += 3;
        for(int i = 0; i < 4; i++) {
            printf("%c", line[i]);
            if(char_cnt >=63){
                printf("\r\n");
                char_cnt = 0;
            }else{
                char_cnt++;
            }
        }
    }

    if (end - in) {
        line[0] = base64_table[in[0] >> 2];
        if (end - in == 1) {
            line[1] = base64_table[(in[0] & 0x03) << 4];
            line[2] = '=';
            line[3] = '=';
        }
        else {
            line[1] = base64_table[((in[0] & 0x03) << 4) | (in[1] >> 4)];
            line[2] = base64_table[(in[1] & 0x0f) << 2];
            line[3] = '=';
        }
        for(int i = 0; i < 4; i++) {
            printf("%c", line[i]);
            if(char_cnt >=63){
                printf("\r\n");
                char_cnt = 0;
            }else{
                char_cnt++;
            }
        }        
    }

    printf("\r\n-----END %s-----\r\n", header);

    return true;
}

void cert_handler(struct command_result* res) {
    if (ui_help_show(res->help_flag, usage, count_of(usage), &options[0], count_of(options))) {
        return;
    }
    static const unsigned char cert_der[1000] = { 0x30, 0x82, 0x03, 0x03, 0x30, 0x82, 0x01, 0xeb, 0xa0, 0x03, 0x02, 0x01, 0x02, 0x02, 0x09, 0x00, 0xf9, 0x04, 0x9d, 0x32, 0x5e, 0x76, 0x45, 0x4f, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0b, 0x05, 0x00, 0x30, 0x46, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02, 0x55, 0x53, 0x31, 0x17, 0x30, 0x15, 0x06, 0x03, 0x55, 0x04, 0x0a, 0x0c, 0x0e, 0x57, 0x68, 0x65, 0x72, 0x65, 0x20, 0x4c, 0x61, 0x62, 0x73, 0x20, 0x4c, 0x4c, 0x43, 0x31, 0x1e, 0x30, 0x1c, 0x06, 0x03, 0x55, 0x04, 0x03, 0x0c, 0x15, 0x68, 0x74, 0x74, 0x70, 0x73, 0x3a, 0x2f, 0x2f, 0x62, 0x75, 0x73, 0x70, 0x69, 0x72, 0x61, 0x74, 0x65, 0x2e, 0x63, 0x6f, 0x6d, 0x30, 0x20, 0x17, 0x0d, 0x32, 0x35, 0x30, 0x32, 0x31, 0x36, 0x31, 0x33, 0x30, 0x31, 0x30, 0x33, 0x5a, 0x18, 0x0f, 0x32, 0x31, 0x32, 0x35, 0x30, 0x31, 0x32, 0x33, 0x31, 0x33, 0x30, 0x31, 0x30, 0x33, 0x5a, 0x30, 0x3a, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02, 0x43, 0x4e, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x08, 0x0c, 0x0a, 0x42, 0x75, 0x73, 0x20, 0x50, 0x69, 0x72, 0x61, 0x74, 0x65, 0x31, 0x0a, 0x30, 0x08, 0x06, 0x03, 0x55, 0x04, 0x07, 0x0c, 0x01, 0x36, 0x31, 0x0a, 0x30, 0x08, 0x06, 0x03, 0x55, 0x04, 0x0a, 0x0c, 0x01, 0x32, 0x30, 0x82, 0x01, 0x22, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00, 0x03, 0x82, 0x01, 0x0f, 0x00, 0x30, 0x82, 0x01, 0x0a, 0x02, 0x82, 0x01, 0x01, 0x00, 0xc5, 0x04, 0xf7, 0x90, 0xd9, 0x14, 0x31, 0x8f, 0x17, 0x2e, 0xe8, 0xfa, 0xfc, 0x8e, 0xdd, 0x1f, 0x54, 0x48, 0xf7, 0xd9, 0xdf, 0xc2, 0x63, 0x2e, 0x9b, 0x91, 0xd9, 0x2a, 0x02, 0xbe, 0xde, 0x70, 0x74, 0xd7, 0x68, 0xfe, 0xf8, 0x44, 0x81, 0x0b, 0x62, 0x1e, 0x47, 0xe3, 0x7f, 0x19, 0x15, 0x91, 0x55, 0x70, 0x16, 0x7b, 0x50, 0xab, 0xc4, 0x24, 0xe4, 0x89, 0x43, 0x92, 0x05, 0x82, 0x05, 0x6b, 0x60, 0x71, 0xcd, 0x06, 0x3a, 0x9e, 0xdb, 0xe1, 0x3e, 0xb9, 0xa4, 0x27, 0x19, 0x10, 0x72, 0x7c, 0x00, 0xcb, 0x3b, 0x9d, 0xe2, 0xce, 0x93, 0xcf, 0xee, 0xcd, 0x8e, 0xd4, 0x18, 0xbf, 0xed, 0x38, 0x1e, 0x61, 0x8e, 0x04, 0x6b, 0x69, 0x34, 0x2a, 0xfb, 0xc5, 0x7e, 0xdb, 0x70, 0x6f, 0x16, 0x3a, 0x31, 0x1c, 0xbd, 0x2f, 0x1c, 0xae, 0x1e, 0xa8, 0x94, 0x0e, 0xe8, 0x0b, 0xee, 0x7d, 0x3d, 0x42, 0x9e, 0xce, 0xa5, 0x4a, 0xe8, 0x01, 0x02, 0xf4, 0x70, 0x87, 0x5c, 0xb7, 0x29, 0x91, 0x87, 0x95, 0x31, 0xa1, 0xa3, 0xb3, 0x17, 0xd7, 0x27, 0x6a, 0x55, 0x1f, 0xc2, 0x5e, 0xc1, 0xd0, 0x20, 0x59, 0x16, 0xaf, 0xec, 0x0d, 0xaa, 0x54, 0x5c, 0xf0, 0x2f, 0xce, 0xa7, 0x3e, 0x86, 0xb0, 0x23, 0x22, 0x7e, 0x2e, 0x27, 0xd6, 0x32, 0x40, 0x00, 0x5b, 0xa4, 0x87, 0xca, 0x76, 0xfb, 0xc7, 0xe9, 0x06, 0x39, 0x36, 0x63, 0x35, 0x05, 0x54, 0x33, 0x12, 0xcf, 0xfc, 0x12, 0x52, 0x37, 0x42, 0xbc, 0x22, 0x44, 0xbb, 0x32, 0x47, 0x02, 0xcd, 0x54, 0x76, 0xb9, 0xc5, 0x3b, 0x00, 0xe7, 0x21, 0x7a, 0x7e, 0x5a, 0x0c, 0x3f, 0xf1, 0x2f, 0x43, 0xd7, 0x4a, 0x9c, 0x7e, 0xbf, 0xd8, 0x2e, 0xf0, 0x52, 0xce, 0xbe, 0x77, 0xaa, 0x57, 0x6e, 0x7d, 0x0c, 0xdc, 0xec, 0xe5, 0xa0, 0x30, 0x44, 0x3a, 0x23, 0xbd, 0x02, 0x03, 0x01, 0x00, 0x01, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0b, 0x05, 0x00, 0x03, 0x82, 0x01, 0x01, 0x00, 0x70, 0xff, 0x4a, 0xdc, 0xe6, 0x5b, 0x8d, 0x2b, 0x0a, 0xb0, 0xc2, 0x0a, 0x7d, 0x2e, 0x99, 0x7c, 0x57, 0x4f, 0xac, 0x56, 0x17, 0x05, 0x08, 0x39, 0x75, 0x5d, 0xac, 0xb4, 0x45, 0x57, 0xb5, 0xbf, 0x99, 0xab, 0x10, 0x5c, 0x6c, 0x98, 0x3c, 0x11, 0x14, 0xb0, 0xe4, 0xc5, 0x95, 0x12, 0xec, 0x98, 0x19, 0x96, 0x7b, 0x95, 0xf3, 0xa1, 0x23, 0x38, 0xc7, 0x7e, 0x9c, 0x94, 0xb1, 0xf0, 0xac, 0x08, 0xf1, 0x5e, 0xd9, 0xb8, 0x91, 0xf1, 0x95, 0x0d, 0x34, 0xa7, 0x63, 0x31, 0x22, 0x81, 0x79, 0xc0, 0x0b, 0x78, 0x56, 0xb3, 0x98, 0x7d, 0xe4, 0xbd, 0xd5, 0x42, 0x56, 0xc4, 0x6a, 0x0d, 0xd7, 0x11, 0x12, 0x18, 0xb9, 0xa5, 0x86, 0xd6, 0x13, 0xff, 0xf5, 0xe4, 0xaa, 0x40, 0xcd, 0x90, 0x7b, 0xb0, 0xb5, 0x27, 0x18, 0xc8, 0x20, 0x11, 0x25, 0x11, 0xd0, 0x80, 0xbd, 0xde, 0x6d, 0xcd, 0xc2, 0x4d, 0xe9, 0x6e, 0x7b, 0x25, 0xaa, 0x0a, 0xb9, 0x05, 0x5e, 0x9d, 0xd0, 0x44, 0x7b, 0x2c, 0xab, 0xce, 0x12, 0x45, 0x34, 0x1e, 0x18, 0x3b, 0xd1, 0x5f, 0xc0, 0xba, 0x59, 0x15, 0x96, 0x24, 0x66, 0x9a, 0x9d, 0x8c, 0x0a, 0x8a, 0xeb, 0x18, 0x99, 0x0a, 0xf6, 0x03, 0x0f, 0xb1, 0xc4, 0x46, 0x49, 0x80, 0x12, 0x89, 0x61, 0x9a, 0xd1, 0x41, 0x8f, 0xfb, 0xf9, 0xc8, 0xca, 0x11, 0xd0, 0x75, 0xf0, 0x76, 0x3c, 0xd9, 0x0a, 0x58, 0x8f, 0x27, 0x75, 0x8c, 0x93, 0xff, 0x9a, 0x79, 0xb3, 0x0c, 0xb9, 0x03, 0xdf, 0xfa, 0xf5, 0x72, 0xc0, 0x24, 0x7c, 0xe7, 0x59, 0x4e, 0x7a, 0xca, 0x39, 0x17, 0xb4, 0xb1, 0x9a, 0xe6, 0xd7, 0x87, 0x58, 0x22, 0x0d, 0x33, 0x41, 0xe6, 0x1a, 0x38, 0x29, 0xc1, 0xf1, 0x55, 0xe3, 0x4a, 0x2d, 0xda, 0x37, 0x47, 0x09, 0xaf, 0xf9, 0x64, 0x13, 0x99, 0x95, 0xdb, 0xff, 0x5c };

    mbedtls_x509_crt cert;
    mbedtls_pk_context public_key;
    int ret;
    unsigned char hash[32];


    // if display cert and public key
    bool d_flag = cmdln_args_find_flag('d');

    if (cmdln_args_find_flag('b')) {
        // load cert der from file, burn to OTP
        printf("Writing cert to OTP\r\n");
        #if 0
        // open DER file
        command_var_t arg;
        char file[13];
        if(!cmdln_args_find_flag_string('b', &arg, sizeof(file), file)){
            printf("No file specified\r\n");
            system_config.error = true; // set the error flag
            return;
        }

        FIL file_handle;                                                  // file handle
        FRESULT result;   
        result = f_open(&file_handle, file, FA_READ); // open the file for reading
        if (result != FR_OK) {
            printf("Error opening file %s for reading\r\n", file);
            system_config.error = true; // set the error flag
            return;
        }

        // read the file
        UINT bytes_read; // somewhere to store the number of bytes read
        result = f_read(&file_handle, cert_der, sizeof(cert_der), &bytes_read); // read the data from the file
        if (result == FR_OK) {                                              // if the read was successful
            printf("Read %d bytes from file %s\r\n", bytes_read, file);
        } else {                                     // error reading file
            printf("Error reading file %s\r\n", file);
            system_config.error = true; // set the error flag
            goto cert_close_file;
        }
        #endif

        //burn the cert to OTP
        char buffer[1000]; // buffer to store file contents

        //const unsigned char bp_pubkey_der[] = { 0x30, 0x82, 0x01, 0x22, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00, 0x03, 0x82, 0x01, 0x0f, 0x00, 0x30, 0x82, 0x01, 0x0a, 0x02, 0x82, 0x01, 0x01, 0x00, 0x8a, 0x82, 0x32, 0xdf, 0x22, 0xbd, 0xa1, 0x6f, 0x27, 0xa1, 0xdf, 0x87, 0x10, 0x05, 0x9c, 0x57, 0x50, 0xcb, 0x4a, 0x72, 0x59, 0xd1, 0x24, 0x24, 0x27, 0x29, 0xaf, 0x7c, 0x1d, 0xc7, 0x06, 0x8d, 0xac, 0x6c, 0xac, 0x05, 0x57, 0x82, 0x9c, 0xb7, 0x42, 0xf1, 0x59, 0xf9, 0xab, 0xab, 0x49, 0x19, 0x3c, 0xce, 0x03, 0x13, 0x86, 0xef, 0x8c, 0x3e, 0x94, 0xc2, 0xd2, 0xcd, 0x37, 0x4a, 0x56, 0x68, 0xa2, 0xb9, 0xf9, 0xea, 0x9d, 0xf6, 0xdc, 0x31, 0x49, 0x6b, 0x94, 0xf8, 0xe0, 0x32, 0x70, 0x5d, 0x08, 0xcb, 0x91, 0xf4, 0x5e, 0x69, 0x63, 0x05, 0xcf, 0x7f, 0xb5, 0xc5, 0x63, 0xd3, 0x4b, 0xe7, 0xef, 0x89, 0xa8, 0xed, 0x8b, 0xdf, 0x35, 0x2c, 0x18, 0xc8, 0x6d, 0x75, 0x39, 0xe4, 0xa3, 0x4d, 0x4c, 0x99, 0x7a, 0x6e, 0xfb, 0x1c, 0xa1, 0x0b, 0xc5, 0xd6, 0x4e, 0x3b, 0x4e, 0xf7, 0xb0, 0x1f, 0x93, 0x0d, 0xd5, 0x7b, 0x1f, 0x75, 0xc1, 0x13, 0x51, 0xc4, 0x50, 0x15, 0x41, 0x18, 0x47, 0x32, 0xa6, 0x36, 0x8b, 0x47, 0x46, 0x65, 0x7b, 0xd1, 0x70, 0x1b, 0x25, 0xa3, 0x5d, 0xea, 0x4f, 0x28, 0x1c, 0x32, 0xe0, 0xc4, 0x79, 0x65, 0xe3, 0xa9, 0x1a, 0xb8, 0x39, 0xc8, 0x8b, 0x05, 0x60, 0xf5, 0x0b, 0xc9, 0xcb, 0x98, 0x44, 0xe2, 0x8e, 0x58, 0x91, 0x7d, 0xdf, 0xcc, 0xe5, 0xf1, 0x7d, 0x51, 0xa0, 0xb1, 0xa5, 0x98, 0x3e, 0xff, 0x2b, 0x5d, 0x34, 0xb8, 0x50, 0x00, 0x77, 0xd3, 0xf4, 0xe1, 0xb0, 0xa2, 0xbc, 0x3e, 0x85, 0xe7, 0xb9, 0x80, 0xa0, 0x08, 0xfb, 0x7f, 0x59, 0xd0, 0x26, 0xa9, 0x2a, 0x22, 0x80, 0x8f, 0x1f, 0x23, 0x45, 0x1b, 0x6e, 0x15, 0xa6, 0x5d, 0x8e, 0x90, 0xc2, 0xf5, 0x0a, 0x0c, 0x56, 0xf1, 0x85, 0xc1, 0x49, 0x80, 0x31, 0x30, 0x67, 0x02, 0x4c, 0x46, 0x9e, 0xa7, 0x02, 0x03, 0x01, 0x00, 0x01 };
        //const unsigned int bp_pubkey_der_len = 294;

        if (!cert_write_to_otp_new(
            cert_der, ARRAY_SIZE(cert_der),
            bp_pubkey_der, bp_pubkey_der_len,
            buffer, sizeof(buffer),
            true)) {
            printf("Failed to write cert to OTP\r\n\r\n");
            system_config.error = true; // set the error flag
            return;
        }  
        #if 0  
        // close the file
cert_close_file:
        result = f_close(&file_handle); // close the file
        if (result != FR_OK) {
            printf("Error closing file %s\r\n", file);
            system_config.error = true; // set the error flag
            return;
        }
        #endif
    }

    uint32_t pubkey_der_len = bp_pubkey_der_len;

    // 1. Read the first 4 bytes of the cert to get the length
    // 2. Use cert_length() to get full length of the cert
    // 3. If too long     


    unsigned char buf[100];
    if (!bp_otp_read_ecc_data(BP_OTP_CERT_ROW, buf, 4)) {
        printf("Failed to read cert length from OTP\r\n");
        return;
    }
    uint32_t cert_der_len = cert_length(buf, 4);
    if (cert_der_len == 0) {
        printf("Invalid cert length or no cert installed\r\n");
        return;
    } else if (cert_der_len > ARRAY_SIZE(buf)){
        printf("Cert length too long\r\n");
        return;
    }

    if (!bp_otp_read_ecc_data(BP_OTP_CERT_ROW, buf, cert_der_len)) {
        printf("Failed to read cert from OTP\r\n");
        return;
    }
    printf("Cert loaded from OTP\r\n");

    // NOTE: These allocate resources, so avoid multiple return points
    //       as each would need to cleanup....
    mbedtls_x509_crt_init(&cert);
    mbedtls_pk_init(&public_key);
    printf("Cert length: %d\r\nParsing certificate\r\n", cert_der_len);
    ret = mbedtls_x509_crt_parse(&cert, cert_der, cert_der_len);
    if (ret != 0) {
        mbedtls_strerror(ret, buf, ARRAY_SIZE(buf));
        printf("Failed to parse certificate: %s\r\n", buf);
        mbedtls_x509_crt_free(&cert);
        mbedtls_pk_free(&public_key);   
        return;
    }

    printf("Pubkey length: %d\r\nParsing public key\r\n", pubkey_der_len);
    ret = mbedtls_pk_parse_public_key(&public_key, bp_pubkey_der, pubkey_der_len);
    if (ret != 0) {
        mbedtls_strerror(ret, buf, 100);
        printf("Failed to parse public key: %s\r\n", buf);
        mbedtls_x509_crt_free(&cert);
        mbedtls_pk_free(&public_key);   
        return;
    }

    printf("Certificate contents:\r\n");
    print_x509_info(&cert, (char *)buf, sizeof(buf)); 

    // Compute the SHA-256 hash of the TBS (to-be-signed) part of the certificate
    printf("\r\nVerifying the SHA-256 signature\r\n");
    const mbedtls_md_info_t *mdinfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    ret = mbedtls_md(
        mdinfo,
        cert.tbs.p, cert.tbs.len, hash);
    if (ret != 0) {
        mbedtls_strerror(ret, buf, 100);
        printf("Failed to create hash: %s\r\n", buf);
        mbedtls_x509_crt_free(&cert);
        mbedtls_pk_free(&public_key);   
        return;
    }

    printf("Certificate signature length: %d\r\n", cert.sig.len);
    printf("SHA-256 hash length: %d\r\n", mdinfo->size);
    printf("SHA-256 hash: ");
    for(int i = 0; i < mdinfo->size; i++) {
        printf("%02X", hash[i]);
    }
    printf("\r\n\r\n");

    // Verify the certificate signature using the public key
    ret = mbedtls_pk_verify(
        &public_key,
        MBEDTLS_MD_SHA256,
        hash, 0,
        cert.sig.p, cert.sig.len
    );
    if (ret != 0) {
        mbedtls_strerror(ret, buf, 100);
        printf("Failed to verify: %s\r\n", buf);
        mbedtls_x509_crt_free(&cert);
        mbedtls_pk_free(&public_key);   
        return;
    };

    printf("Certificate verified successfully\r\n\r\n");

    printf("Bus Pirate Unique Serial Number: ");
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);    
    for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++) {
        printf("%02X%s", id.id[i], (i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES-1) ? ":" : "");
    }
    printf("\r\n");

    printf("Certificate Serial Number: ");
    char cert_serial[PICO_UNIQUE_BOARD_ID_SIZE_BYTES];
    cert_serial_get(&cert, cert_serial, sizeof(cert_serial));
    for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++) {
        printf("%02X%s", cert_serial[i], (i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES-1) ? ":" : "");
    }
    printf("\r\n");

    //verify that the serial number in the certificate matches the RP2040 unique serial number
    if (memcmp(cert_serial, id.id, PICO_UNIQUE_BOARD_ID_SIZE_BYTES) == 0) {
        printf("Serials match: hardware authenticated :)\r\n");
    } else {
        printf("Serial mismatch: authentication failed :(\r\n");
    }
    mbedtls_x509_crt_free(&cert);
    mbedtls_pk_free(&public_key);   

cert_dump:

    if (d_flag){
        printf("\r\n");
        printf("Certificate PEM:\r\n");
        // Print the PEM-encoded certificate
        base64_encode(cert_der, cert_der_len, "CERTIFICATE");
        printf("\r\n");  

        printf("Public key PEM:\r\n");
        base64_encode(bp_pubkey_der, pubkey_der_len, "PUBLIC KEY");
        printf("\r\n");  
    }

}