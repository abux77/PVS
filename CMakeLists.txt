# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)
project(pvs C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

# The existing upstream C renderer is vendored with a bounded output adapter.
# No network download or generated dependency patch is required.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(GLOB PVS_FACTORY_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/data/*.txt")
set(PVS_STORAGE_DEFAULTS "${CMAKE_CURRENT_BINARY_DIR}/generated/storage_defaults.h")
add_custom_command(OUTPUT "${PVS_STORAGE_DEFAULTS}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_LIST_DIR}/tools/embed_storage.py"
            "${CMAKE_CURRENT_LIST_DIR}/data" "${PVS_STORAGE_DEFAULTS}"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/tools/embed_storage.py" ${PVS_FACTORY_FILES}
    VERBATIM
)

option(PVS_AUDIO_PIO "Use the previous PIO audio backend for comparison" OFF)
if(PVS_AUDIO_PIO)
    set(PVS_AUDIO_SOURCE src/audio_pio.c)
else()
    set(PVS_AUDIO_SOURCE src/audio_pwm.c)
endif()

add_executable(pvs
    src/main.c src/oled.c src/oled_i2c.c src/build_info.c src/pvs_memory.c src/pvs.c src/matrix.c src/pots.c src/ws2812.c
    ${PVS_AUDIO_SOURCE} src/storage.c src/sam_pvs.c
    src/sam/sam_reciter.c src/sam/sam_parser.c src/sam/sam_renderer.c
    src/storage_format.c src/storage_usb.c src/flash_disk.c src/usb_descriptors.c
    third_party/sam/render.c
    third_party/fatfs/ff.c third_party/fatfs/ffunicode.c
    "${PVS_STORAGE_DEFAULTS}"
)
# Deliberately refresh the number/timestamp on every build invocation.
add_custom_target(pvs_build_id
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_LIST_DIR}/tools/build_id.py"
            "${CMAKE_CURRENT_BINARY_DIR}/generated/pvs_build_generated.h"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/generated/pvs_build_generated.h"
    VERBATIM)
add_dependencies(pvs pvs_build_id)
target_include_directories(pvs PRIVATE include src src/sam third_party/sam third_party/fatfs
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_options(pvs PRIVATE -Wall -Wextra)
target_compile_definitions(pvs PRIVATE
    PICO_STDIO_USB_USE_DEFAULT_DESCRIPTORS=0
    PICO_STDIO_USB_ENABLE_IRQ_BACKGROUND_TASK=0
    PICO_STDIO_USB_ENABLE_TINYUSB_INIT=0
    PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=0
    PICO_STDIO_USB_STDOUT_TIMEOUT_US=0
    PICO_STACK_SIZE=4096
)
target_link_libraries(pvs pico_stdlib hardware_adc hardware_dma hardware_pio hardware_pwm
    hardware_clocks hardware_irq hardware_flash hardware_sync pico_flash
    pico_unique_id tinyusb_device tinyusb_board)
pico_generate_pio_header(pvs "${CMAKE_CURRENT_LIST_DIR}/src/audio_pwm.pio")
pico_generate_pio_header(pvs "${CMAKE_CURRENT_LIST_DIR}/src/matrix.pio")
pico_generate_pio_header(pvs "${CMAKE_CURRENT_LIST_DIR}/src/oled_i2c.pio")
pico_generate_pio_header(pvs "${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio")
pico_include_in_generated_section(pvs post_end
    "${CMAKE_CURRENT_LIST_DIR}/cmake/storage_reservation.ld")
pico_add_link_depend(pvs "${CMAKE_CURRENT_LIST_DIR}/cmake/storage_reservation.ld")
pico_enable_stdio_usb(pvs 1)
pico_enable_stdio_uart(pvs 0)
pico_add_extra_outputs(pvs)
