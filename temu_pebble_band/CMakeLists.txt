# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(temu_pebble_band C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(temu_pebble_band)

pico_set_program_name(temu_pebble_band "temu_pebble_band")
pico_set_program_version(temu_pebble_band "0.1")

# Generate PIO header
pico_generate_pio_header(temu_pebble_band ${CMAKE_CURRENT_LIST_DIR}/blink.pio)

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(temu_pebble_band 0)
pico_enable_stdio_usb(temu_pebble_band 0)

# Add the standard library to the build
target_link_libraries(temu_pebble_band
        pico_stdlib)

# Add the standard include files to the build
target_include_directories(temu_pebble_band PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

# must match with pio filename and executable name from above
pico_generate_pio_header(temu_pebble_band ${CMAKE_CURRENT_LIST_DIR}/hsync.pio)
pico_generate_pio_header(temu_pebble_band ${CMAKE_CURRENT_LIST_DIR}/vsync.pio)
pico_generate_pio_header(temu_pebble_band ${CMAKE_CURRENT_LIST_DIR}/rgb.pio)


# Add any user requested libraries
# must match with executable name
target_link_libraries(temu_pebble_band 
    pico_divider 
    pico_multicore 
    pico_bootsel_via_double_reset 
    hardware_pio 
    hardware_dma 
    hardware_spi 
    hardware_adc 
    hardware_irq 
    hardware_clocks 
    hardware_pll)

# must match with executable name and source file names
target_sources(temu_pebble_band PRIVATE temu_pebble_band.c vga16_graphics.c)


pico_add_extra_outputs(temu_pebble_band)

