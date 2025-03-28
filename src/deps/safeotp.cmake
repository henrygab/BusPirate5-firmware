add_library(buspirate_saferotp STATIC
        saferotp/safer_otp_direntry.c
        saferotp/safer_otp_ecc.c
        saferotp/safer_otp_rw.c
)
target_compile_options(    buspirate_saferotp PRIVATE   -Wpedantic -Wall -Werror -O3 -Wno-unknown-pragmas -Wno-inline -Wno-unused-function)
target_include_directories(buspirate_saferotp PRIVATE   saferotp)
target_include_directories(buspirate_saferotp INTERFACE saferotp)
set_property(TARGET        buspirate_saferotp PROPERTY  POSITION_INDEPENDENT_CODE ON)
