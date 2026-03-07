project(AD9826_SPI)

add_library(${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(${PROJECT_NAME}
    pico_stdlib
    hardware_pwm
)

target_sources(${PROJECT_NAME}
    PRIVATE
        AD9826_SPI.c
        AD9826_SPI.h

)