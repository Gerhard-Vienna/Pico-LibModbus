add_executable(pico-hello-server
    pico-hello-server.c
    ../libmodbus/src/modbus.c
    ../libmodbus/src/modbus-data.c
    ../libmodbus/src/modbus-pico-tcp.c
)
target_compile_definitions(pico-hello-server PRIVATE
    PICO_W  # for libmodbus
)
target_include_directories(pico-hello-server PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts and wifi settings
    ${CMAKE_CURRENT_LIST_DIR}/../libmodbus/src # for modbus
)
target_link_libraries(pico-hello-server
    pico_cyw43_arch_lwip_threadsafe_background
    pico_stdlib
    pico_multicore
)

pico_add_extra_outputs(pico-hello-server)
pico_enable_stdio_usb(pico-hello-server 1)
pico_enable_stdio_uart(pico-hello-server 1)

add_executable(pico-hello-client
    pico-hello-client.c
    ../libmodbus/src/modbus.c
    ../libmodbus/src/modbus-data.c
    ../libmodbus/src/modbus-pico-tcp.c
)
target_compile_definitions(pico-hello-client PRIVATE
    PICO_W  # for libmodbus
)
target_include_directories(pico-hello-client PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts and wifi settings
    ${CMAKE_CURRENT_LIST_DIR}/../libmodbus/src # for modbus
)
target_link_libraries(pico-hello-client
    pico_cyw43_arch_lwip_threadsafe_background
    pico_stdlib
)

pico_add_extra_outputs(pico-hello-client)
pico_enable_stdio_usb(pico-hello-client 1)
pico_enable_stdio_uart(pico-hello-client 1)


add_executable(pico-server-example
        pico-server-example.c
        ../libmodbus/src/modbus.c
        ../libmodbus/src/modbus-data.c
        ../libmodbus/src/modbus-pico-tcp.c
)
target_compile_definitions(pico-server-example PRIVATE
        PICO_W  # for libmodbus
)
target_include_directories(pico-server-example PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts and wifi settings
        ${CMAKE_CURRENT_LIST_DIR}/../libmodbus/src # for modbus
)
target_link_libraries(pico-server-example
        pico_cyw43_arch_lwip_threadsafe_background
        pico_stdlib
        pico_multicore
        hardware_rtc
        hardware_adc
)

pico_add_extra_outputs(pico-server-example)
pico_enable_stdio_usb(pico-server-example 1)
pico_enable_stdio_uart(pico-server-example 1)



add_executable(pico-weather-server
    pico-weather-server.c
    bme280.c
    ../libmodbus/src/modbus.c
    ../libmodbus/src/modbus-data.c
    ../libmodbus/src/modbus-pico-tcp.c
)
target_compile_definitions(pico-weather-server PRIVATE
    PICO_W  # for libmodbus
)
target_include_directories(pico-weather-server PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts and wifi settings
    ${CMAKE_CURRENT_LIST_DIR}/../libmodbus/src # for modbus
)
target_link_libraries(pico-weather-server
    pico_cyw43_arch_lwip_threadsafe_background
    pico_stdlib
    pico_multicore
    hardware_i2c
)

pico_add_extra_outputs(pico-weather-server)
pico_enable_stdio_usb(pico-weather-server 1)
pico_enable_stdio_uart(pico-weather-server 1)
