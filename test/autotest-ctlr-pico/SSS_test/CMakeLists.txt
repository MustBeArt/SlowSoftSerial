# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.12)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_SYSTEM_NAME Generic)

# initalize pico_sdk from installed location
# (note this can come from environment, CMake cache etc)
set(PICO_SDK_PATH "/Users/kb5mu/Documents/pico/pico-sdk")

# Pull in Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(SSS_test C CXX)

# Initialise the Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(SSS_test
    SSS_test.c
)

pico_set_program_name(SSS_test "SSS_test")
pico_set_program_version(SSS_test "0.1")

pico_enable_stdio_uart(SSS_test 1)
pico_enable_stdio_usb(SSS_test 0)
#pico_enable_stdio_semihosting(SSS_test 1)

# Add the standard library to the build
target_link_libraries(SSS_test pico_stdlib)

pico_add_extra_outputs(SSS_test)

