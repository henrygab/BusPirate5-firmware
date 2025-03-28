#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BP_OTPDIR_ENTRY_TYPE_USB_WHITELABEL   ((BP_OTPDIR_ENTRY_TYPE){ .id = 0x01u, .encoding_type = BP_OTPDIR_DATA_ENCODING_TYPE_ECC                           })
#define BP_OTPDIR_ENTRY_TYPE_BP_CERTIFICATE   ((BP_OTPDIR_ENTRY_TYPE){ .id = 0x02u, .encoding_type = BP_OTPDIR_DATA_ENCODING_TYPE_ECC                           })

void bp_otp_apply_whitelabel_data(void);
bool bp_otp_lock_whitelabel(void);

#ifdef __cplusplus
}
#endif

