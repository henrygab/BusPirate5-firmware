
cmake_minimum_required(VERSION 3.19)
project(saferotp_lib)

add_library(saferotp_lib STATIC
        saferotp/safer_otp_direntry.c
        saferotp/safer_otp_ecc.c
        saferotp/safer_otp_rw.c
)
# add_library(buspirate_saferotp INTERFACE
#         saferotp/saferotp.h
#         saferotp/saferotp_ecc.h
# )

# INTERFACE for only consumers of the library
# PUBLIC    when both library and consumers use the directory for headers files
#
# Q: is this simpler form OK, or need it be $<BUILD_INTERFACE:saferotp_lib_SOURCE_DIR}/saferotp> ???
target_include_directories(saferotp_lib PRIVATE   saferotp)
target_include_directories(saferotp_lib INTERFACE saferotp)
target_compile_options(    saferotp_lib PRIVATE   -Wpedantic -Wall -Werror -O3 -Wno-unknown-pragmas -Wno-inline -Wno-unused-function)
set_property(TARGET        saferotp_lib PROPERTY  POSITION_INDEPENDENT_CODE ON)

