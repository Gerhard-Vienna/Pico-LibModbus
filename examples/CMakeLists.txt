add_executable(pico_server_example
        pico_server_example
        ../libmodbus/src/modbus.c
        ../libmodbus/src/modbus-data.c
        ../libmodbus/src/modbus-pico-tcp.c
)
target_compile_definitions(pico_server_example PRIVATE
        PICO_W  # for libmodbus
)
target_include_directories(pico_server_example PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts and wifi settings
        ${CMAKE_CURRENT_LIST_DIR}/../libmodbus/src # for modbus
)
target_link_libraries(pico_server_example
        pico_cyw43_arch_lwip_threadsafe_background
        pico_stdlib
        pico_multicore
        hardware_rtc
        hardware_adc
)

pico_add_extra_outputs(pico_server_example)
pico_enable_stdio_usb(pico_server_example 1)
pico_enable_stdio_uart(pico_server_example 1)



add_executable(pico_weather_server
    pico_weather_server
    bme280
    ../libmodbus/src/modbus.c
    ../libmodbus/src/modbus-data.c
    ../libmodbus/src/modbus-pico-tcp.c
)
target_compile_definitions(pico_weather_server PRIVATE
    PICO_W  # for libmodbus
)
target_include_directories(pico_weather_server PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts and wifi settings
    ${CMAKE_CURRENT_LIST_DIR}/../libmodbus/src # for modbus
)
target_link_libraries(pico_weather_server
    pico_cyw43_arch_lwip_threadsafe_background
    pico_stdlib
    pico_multicore
    hardware_i2c
)

pico_add_extra_outputs(pico_weather_server)
pico_enable_stdio_usb(pico_weather_server 1)
pico_enable_stdio_uart(pico_weather_server 1)
