################################
# pico-random-test-client
################################
add_executable(pico-random-test-client
        pico-random-test-client.c
        ../libmodbus/src/modbus.c
        ../libmodbus/src/modbus-data.c
        ../libmodbus/src/modbus-pico-tcp.c
)
target_compile_definitions(pico-random-test-client PRIVATE
        PICO_W
        # DEBUG               # modbus_set_debug(TRUE);
        # PICO_TCP_DEBUG      # to enable DEBUG_printf()
        PICO_TCP_ASSERT     # fail on internal errors
)
target_include_directories(pico-random-test-client PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts and wifi settings
        ${CMAKE_CURRENT_LIST_DIR}/../libmodbus/src # for modbus
)
target_link_libraries(pico-random-test-client
        pico_cyw43_arch_lwip_threadsafe_background
        pico_stdlib
        pico_multicore
)

pico_add_extra_outputs(pico-random-test-client)
pico_enable_stdio_usb(pico-random-test-client 1)
pico_enable_stdio_uart(pico-random-test-client 1)

################################
# pico-random-test-server
################################
add_executable(pico-random-test-server
    pico-random-test-server.c
    ../libmodbus/src/modbus.c
    ../libmodbus/src/modbus-data.c
    ../libmodbus/src/modbus-pico-tcp.c
)
target_compile_definitions(pico-random-test-server PRIVATE
    PICO_W
    # DEBUG               # modbus_set_debug(TRUE);
    # PICO_TCP_DEBUG      # to enable DEBUG_printf()
    PICO_TCP_ASSERT     # fail on internal errors
)
target_include_directories(pico-random-test-server PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts and wifi settings
    ${CMAKE_CURRENT_LIST_DIR}/../libmodbus/src # for modbus
)
target_link_libraries(pico-random-test-server
    pico_cyw43_arch_lwip_threadsafe_background
    pico_stdlib
    pico_multicore
)

pico_add_extra_outputs(pico-random-test-server)
pico_enable_stdio_usb(pico-random-test-server 1)
pico_enable_stdio_uart(pico-random-test-server 1)

################################
# pico-unit-test-server
################################
add_executable(pico-unit-test-server
    pico-unit-test-server.c
    ../libmodbus/src/modbus.c
    ../libmodbus/src/modbus-data.c
    ../libmodbus/src/modbus-pico-tcp.c
    )
target_compile_definitions(pico-unit-test-server PRIVATE
    PICO_W              # for lib_modbus
    # DEBUG               # modbus_set_debug(TRUE);
    # PICO_TCP_DEBUG      # to enable DEBUG_printf()
    PICO_TCP_ASSERT     # fail on internal errors
)
target_include_directories(pico-unit-test-server PRIVATE
${CMAKE_CURRENT_LIST_DIR}
${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts and wifi settings
${CMAKE_CURRENT_LIST_DIR}/../libmodbus/src # for modbus
)
target_link_libraries(pico-unit-test-server
pico_cyw43_arch_lwip_threadsafe_background
pico_stdlib
pico_multicore
)

pico_add_extra_outputs(pico-unit-test-server)
pico_enable_stdio_usb(pico-unit-test-server 1)
pico_enable_stdio_uart(pico-unit-test-server 1)

################################
# pico-unit-test-client
################################
add_executable(pico-unit-test-client
    pico-unit-test-client.c
    ../libmodbus/src/modbus.c
    ../libmodbus/src/modbus-data.c
    ../libmodbus/src/modbus-pico-tcp.c
)
target_compile_definitions(pico-unit-test-client PRIVATE
    PICO_W
    # DEBUG               # modbus_set_debug(TRUE);
    # PICO_TCP_DEBUG      # to enable DEBUG_printf()
    PICO_TCP_ASSERT     # fail on internal errors
)
target_include_directories(pico-unit-test-client PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts and wifi settings
    ${CMAKE_CURRENT_LIST_DIR}/../libmodbus/src # for modbus
)
target_link_libraries(pico-unit-test-client
    pico_cyw43_arch_lwip_threadsafe_background
    pico_stdlib
    pico_multicore
)

pico_add_extra_outputs(pico-unit-test-client)
pico_enable_stdio_usb(pico-unit-test-client 1)
pico_enable_stdio_uart(pico-unit-test-client 1)

################################
# pico-bandwidth-server
################################
add_executable(pico-bandwidth-server
    pico-bandwidth-server.c
    ../libmodbus/src/modbus.c
    ../libmodbus/src/modbus-data.c
    ../libmodbus/src/modbus-pico-tcp.c
)
target_compile_definitions(pico-bandwidth-server PRIVATE
    PICO_W
    # DEBUG               # modbus_set_debug(TRUE);
    # PICO_TCP_DEBUG      # to enable DEBUG_printf()
    PICO_TCP_ASSERT     # fail on internal errors
)
target_include_directories(pico-bandwidth-server PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts and wifi settings
    ${CMAKE_CURRENT_LIST_DIR}/../libmodbus/src # for modbus
)
target_link_libraries(pico-bandwidth-server
    pico_cyw43_arch_lwip_threadsafe_background
    pico_stdlib
    pico_multicore
)

pico_add_extra_outputs(pico-bandwidth-server)
pico_enable_stdio_usb(pico-bandwidth-server 1)
pico_enable_stdio_uart(pico-bandwidth-server 1)

################################
# pico-bandwidth-client
################################

add_executable(pico-bandwidth-client
    pico-bandwidth-client.c
    ../libmodbus/src/modbus.c
    ../libmodbus/src/modbus-data.c
    ../libmodbus/src/modbus-pico-tcp.c
)
target_compile_definitions(pico-bandwidth-client PRIVATE
    PICO_W
    # DEBUG               # modbus_set_debug(TRUE);
    # PICO_TCP_DEBUG      # to enable DEBUG_printf()
    PICO_TCP_ASSERT     # fail on internal errors
)
target_include_directories(pico-bandwidth-client PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts and wifi settings
    ${CMAKE_CURRENT_LIST_DIR}/../libmodbus/src # for modbus
)
target_link_libraries(pico-bandwidth-client
    pico_cyw43_arch_lwip_threadsafe_background
    pico_stdlib
    pico_multicore
)

pico_add_extra_outputs(pico-bandwidth-client)
pico_enable_stdio_usb(pico-bandwidth-client 1)
pico_enable_stdio_uart(pico-bandwidth-client 1)
