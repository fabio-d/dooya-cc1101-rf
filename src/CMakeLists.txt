add_executable(dooya-cc1101-rf
    cc1101.cpp
    console.cpp
    main.cpp
)

target_link_libraries(dooya-cc1101-rf
    hardware_spi
    pico_stdlib
)

pico_enable_stdio_usb(dooya-cc1101-rf 1)
pico_enable_stdio_uart(dooya-cc1101-rf 0)

pico_add_extra_outputs(dooya-cc1101-rf)
