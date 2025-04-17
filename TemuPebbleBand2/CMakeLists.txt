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
set(sdkVersion 2.1.0)
set(toolchainVersion 13_3_Rel1)
set(picotoolVersion 2.1.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(TemuPebbleBand2 C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# ======================================

add_executable(TemuPebbleBand2)

# must match with pio filename and executable name from above
pico_generate_pio_header(TemuPebbleBand2 ${CMAKE_CURRENT_LIST_DIR}/hsync.pio)
pico_generate_pio_header(TemuPebbleBand2 ${CMAKE_CURRENT_LIST_DIR}/vsync.pio)
pico_generate_pio_header(TemuPebbleBand2 ${CMAKE_CURRENT_LIST_DIR}/rgb.pio)


# Add any user requested libraries
# must match with executable name
target_link_libraries(TemuPebbleBand2 
    pico_stdlib
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
target_sources(TemuPebbleBand2 PRIVATE TemuPebbleBand2.c vga16_graphics.c)


pico_add_extra_outputs(TemuPebbleBand2)