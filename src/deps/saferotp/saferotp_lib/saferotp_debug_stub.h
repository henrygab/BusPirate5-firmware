#pragma once

// // macros used elsewhere
// #define PRINT_FATAL(...)
// #define PRINT_ERROR(...)
// #define PRINT_WARNING(...)
// #define PRINT_INFO(...)
// #define PRINT_VERBOSE(...)
// #define PRINT_DEBUG(...)

// The BusPirate project uses RTT for debug input/output.
// Use that header to define the PRINT_* macros.
#define BP_DEBUG_OVERRIDE_DEFAULT_CATEGORY  BP_DEBUG_CAT_OTP
#include "debug_rtt.h"

// And provide an implementation of for the `WAIT_FOR_KEY()`
// macro that uses RTT to wait for a keypress.
#define MY_DEBUG_WAIT_FOR_KEY() SaferOtp_WaitForKey_impl()
void SaferOtp_WaitForKey_impl(void);
