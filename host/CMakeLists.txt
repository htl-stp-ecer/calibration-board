cmake_minimum_required(VERSION 3.20)
project(raccoon_calib_bridge
        VERSION 0.1.0
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(CALIB_BRIDGE_WERROR "Treat warnings as errors" OFF)

# ── Abhängigkeiten ────────────────────────────────────────────────────
# raccoon-transport: Git-Submodule unter third_party/raccoon-transport.
# Fallback: angrenzendes Schwester-Verzeichnis (häufig im Dev-Tree der
# raccoon-Workspace).  Das lokale Submodule wird bevorzugt, damit
# Production-Builds reproduzierbar sind.
set(RACCOON_TRANSPORT_SUBMODULE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/raccoon-transport")
set(RACCOON_TRANSPORT_SIBLING   "${CMAKE_CURRENT_SOURCE_DIR}/../../raccoon-transport")

if(EXISTS "${RACCOON_TRANSPORT_SUBMODULE}/CMakeLists.txt")
    message(STATUS "raccoon-transport: using submodule at ${RACCOON_TRANSPORT_SUBMODULE}")
    add_subdirectory("${RACCOON_TRANSPORT_SUBMODULE}" raccoon_transport_build)
elseif(EXISTS "${RACCOON_TRANSPORT_SIBLING}/CMakeLists.txt")
    message(WARNING "raccoon-transport submodule not initialised — falling back to sibling at ${RACCOON_TRANSPORT_SIBLING}")
    message(WARNING "Run: git submodule update --init --recursive  for a reproducible build")
    add_subdirectory("${RACCOON_TRANSPORT_SIBLING}" raccoon_transport_build)
else()
    message(FATAL_ERROR
        "raccoon-transport not found.  Init the submodule:\n"
        "    git submodule update --init --recursive\n"
        "or place the repo at ${RACCOON_TRANSPORT_SIBLING}")
endif()

include(FetchContent)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.3
)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

# ── Bibliothek mit der gesamten Bridge-Logik ──────────────────────────
add_library(calib_bridge_lib STATIC
    src/calib_bridge/Application.cpp
    src/calib_bridge/Config.cpp
    src/calib_bridge/FrameDecoder.cpp
    src/calib_bridge/Publisher.cpp
    src/calib_bridge/SerialPort.cpp
)
target_include_directories(calib_bridge_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/shared
)
target_link_libraries(calib_bridge_lib
    PUBLIC
        raccoon::transport
        spdlog::spdlog_header_only
)

# ── Executable ────────────────────────────────────────────────────────
add_executable(raccoon-calib-bridge src/main.cpp)
target_link_libraries(raccoon-calib-bridge PRIVATE calib_bridge_lib)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(_warnflags -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
    if(CALIB_BRIDGE_WERROR)
        list(APPEND _warnflags -Werror)
    endif()
    target_compile_options(calib_bridge_lib       PRIVATE ${_warnflags})
    target_compile_options(raccoon-calib-bridge   PRIVATE ${_warnflags})
endif()

install(TARGETS raccoon-calib-bridge
        RUNTIME DESTINATION bin)
install(FILES systemd/raccoon-calib-bridge.service
        DESTINATION lib/systemd/system)
