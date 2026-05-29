cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME Firmware)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# ──────────────────────────── User code ────────────────────────────
# Application logic and project-owned drivers live outside Core/ and Drivers/
# so they survive STM32CubeMX regeneration. Convention: lowercase folders for
# our code, capitalised for ST-generated code.

file(GLOB_RECURSE USER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/app/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/drivers/*/src/*.c"
)

# Third-party: CEVA sh2 driver for BNO08x (submodule under lib/sh2)
file(GLOB SH2_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/sh2/*.c"
)

# Third-party: ST USB Device library (Core + CDC class) — vendored copy
# from STM32Cube_FW_F7 V1.17.4 since the .ioc does not yet enable the
# USB_DEVICE middleware.
file(GLOB USBD_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/usbd/core/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/usbd/cdc/src/*.c"
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${USER_SOURCES}
    ${SH2_SOURCES}
    ${USBD_SOURCES}
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/app/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/drivers/bno08x/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/drivers/paa5100/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/sh2
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/usbd/core/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/usbd/cdc/inc
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
