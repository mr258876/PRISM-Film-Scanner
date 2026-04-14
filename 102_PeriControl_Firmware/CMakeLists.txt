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
# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
set(PICO_BOARD pico CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(Project_PRISM_PeriControl C CXX ASM)

set(PICO_DEFAULT_BOOT_STAGE2_FILE
    ${PICO_SDK_PATH}/src/rp2040/boot_stage2/boot2_generic_03h.S
)
pico_sdk_init()

add_executable(Project_PRISM_PeriControl
        Project_PRISM_PeriControl.c
        command_dispatcher.c
        exposure_sync.c
        uart_task.c
        persistent_params.c
        led_pwm.c
        tmc2209_bus.c
        stepper_task.c
)

pico_generate_pio_header(Project_PRISM_PeriControl ${CMAKE_CURRENT_LIST_DIR}/exposure_sync.pio)

pico_set_program_name(Project_PRISM_PeriControl "Project_PRISM_PeriControl")
pico_set_program_version(Project_PRISM_PeriControl "0.1")

pico_enable_stdio_uart(Project_PRISM_PeriControl 0)
pico_enable_stdio_usb(Project_PRISM_PeriControl 0)

target_link_libraries(Project_PRISM_PeriControl
        pico_stdlib
        pico_unique_id
        pico_flash
        hardware_flash
        hardware_pio
        hardware_pwm
        hardware_uart
        hardware_gpio
        hardware_clocks)

target_include_directories(Project_PRISM_PeriControl PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(Project_PRISM_PeriControl)
