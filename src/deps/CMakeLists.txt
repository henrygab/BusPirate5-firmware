
if(${PICO_PLATFORM} MATCHES "rp2350")
  message(STATUS "Adding SaferOTP library")
  add_subdirectory(saferotp)
endif()
