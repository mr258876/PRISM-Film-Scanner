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
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(Project_PRISM_RP2040 C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(Project_PRISM_RP2040
        Project_PRISM_RP2040.c
        usb_task.c
        usb_descriptors.c
        persistent_params.c
)

pico_set_program_name(Project_PRISM_RP2040 "Project_PRISM_RP2040")
pico_set_program_version(Project_PRISM_RP2040 "0.1")

# Generate PIO header
pico_generate_pio_header(Project_PRISM_RP2040 ${CMAKE_CURRENT_LIST_DIR}/prism.pio)

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(Project_PRISM_RP2040 0)
pico_enable_stdio_usb(Project_PRISM_RP2040 0)

# Add the standard library to the build
target_link_libraries(Project_PRISM_RP2040
        pico_stdlib
        tinyusb_device
        tinyusb_board
        pico_multicore
        pico_unique_id
        pico_flash
        hardware_flash)

# Add the standard include files to the build
target_include_directories(Project_PRISM_RP2040 PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

### Custom modules
add_subdirectory(AD9826_SPI)

# Modules depend on peripherals only.
# target_link_libraries(Modules PUBLIC Peripherals)

# Add any user requested libraries
target_link_libraries(Project_PRISM_RP2040 
        hardware_pio
        hardware_dma
        AD9826_SPI
        )

pico_add_extra_outputs(Project_PRISM_RP2040)
