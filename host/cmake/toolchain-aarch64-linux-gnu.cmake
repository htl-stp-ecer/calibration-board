# Cross-toolchain für aarch64-linux-gnu.  Wird via -DCMAKE_TOOLCHAIN_FILE
# eingebunden — alle nachgeladenen Subprojekte (raccoon-transport, spdlog
# via FetchContent) erben die Einstellungen automatisch.

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_AR           aarch64-linux-gnu-ar)
set(CMAKE_RANLIB       aarch64-linux-gnu-ranlib)
set(CMAKE_STRIP        aarch64-linux-gnu-strip)

# CMake soll NICHT im Host-Sysroot nach Libs/Includes suchen — nur in den
# vom Sub-Projekt selbst (FetchContent) gebauten Artefakten.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
