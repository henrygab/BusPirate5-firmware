#pragma once

// This header file must define the following macros,
// which are used by the SaferOTP library:
//
// void PRINT_FATAL(...);
// void PRINT_ERROR(...);
// void PRINT_WARNING(...);
// void PRINT_INFO(...);
// void PRINT_VERBOSE(...);
// void PRINT_DEBUG(...);
// void MY_DEBUG_WAIT_FOR_KEY(void);


// The BusPirate project uses RTT for debug input/output.
// It uses the following header to define these PRINT_* macros.
// e.g., #define PRINT_FATAL(...)   BP_DEBUG_PRINT(BP_DEBUG_LEVEL_FATAL,   BP_DEBUG_DEFAULT_CATEGORY, __VA_ARGS__)
#define BP_DEBUG_OVERRIDE_DEFAULT_CATEGORY  BP_DEBUG_CAT_OTP
#include "debug_rtt.h"

// And provide an implementation of for the `WAIT_FOR_KEY()`
// macro that uses RTT to wait for a keypress.
#define MY_DEBUG_WAIT_FOR_KEY() SaferOtp_WaitForKey_impl()
void SaferOtp_WaitForKey_impl(void);
