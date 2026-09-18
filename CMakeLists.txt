cmake_minimum_required(VERSION 3.13)

# GCC emits Windows drive-letter paths when CMake runs under MSYS. GNU Make
# parses the colon as a target separator on the next incremental build, so let
# CMake perform dependency scanning on this host instead.
if(CMAKE_HOST_WIN32 OR CMAKE_HOST_SYSTEM_NAME STREQUAL "MSYS")
    set(CMAKE_DEPENDS_USE_COMPILER FALSE CACHE BOOL "" FORCE)
endif()

set(BOREALIS_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/third_party/borealis/library)
set(PLATFORM_SWITCH ON CACHE BOOL "" FORCE)
set(USE_DEKO3D ON CACHE BOOL "" FORCE)
set(USE_GLFW OFF CACHE BOOL "" FORCE)
set(USE_SDL2 OFF CACHE BOOL "" FORCE)
set(USE_SYSTEM_FMT OFF CACHE BOOL "" FORCE)
set(USE_SYSTEM_TINYXML2 OFF CACHE BOOL "" FORCE)
set(USE_SYSTEM_TWEENY OFF CACHE BOOL "" FORCE)
set(BRLS_UNITY_BUILD OFF CACHE BOOL "" FORCE)
include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

project(switch_drive VERSION 0.2.8 LANGUAGES C CXX)

set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(APP_TITLE "Switch Drive")
set(APP_AUTHOR "kore")
set(APP_ICON ${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg)
set(ROMFS_DIR ${CMAKE_BINARY_DIR}/romfs)

add_subdirectory(third_party/borealis/library)

set(SWITCH_DRIVE_SOURCES
    ${BOREALIS_LIBRARY}/lib/platforms/switch/switch_wrapper.c
    switch/source/app_controller.cpp
    switch/source/core.cpp
    switch/source/download.cpp
    switch/source/i18n.cpp
    switch/source/installer.cpp
    switch/source/main.cpp
    switch/source/network.cpp
    switch/source/nsz.cpp
    switch/source/qr.cpp
    switch/source/ui.cpp
    switch/source/ui_model.cpp
)

program_target(switch-drive "${SWITCH_DRIVE_SOURCES}")
set_target_properties(switch-drive PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS ON
)
target_include_directories(switch-drive PRIVATE switch/include)
target_compile_definitions(switch-drive PRIVATE __SWITCH__)
target_compile_options(switch-drive PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
    -ffunction-sections
    -fdata-sections
)
target_link_libraries(switch-drive PRIVATE
    borealis
    curl
    jansson
    mbedcrypto
    zstd
    z
)

file(MAKE_DIRECTORY ${ROMFS_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/romfs/ DESTINATION ${ROMFS_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/third_party/borealis/resources/img/sys DESTINATION ${ROMFS_DIR}/img)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/third_party/borealis/resources/material DESTINATION ${ROMFS_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/third_party/borealis/resources/i18n/en-US DESTINATION ${ROMFS_DIR}/i18n)
gen_dksh(${ROMFS_DIR}/shaders)

add_custom_target(switch-drive.nro ALL
    DEPENDS switch-drive
    COMMAND ${NX_NACPTOOL_EXE}
        --create "${APP_TITLE}" "${APP_AUTHOR}" "${PROJECT_VERSION}"
        switch-drive.nacp
    COMMAND ${NX_ELF2NRO_EXE}
        switch-drive.elf switch-drive.nro
        --icon=${APP_ICON}
        --nacp=switch-drive.nacp
        --romfsdir=${ROMFS_DIR}
    BYPRODUCTS switch-drive.nacp switch-drive.nro
    VERBATIM
)
