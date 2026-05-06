cmake_minimum_required(VERSION 3.21)
project(GigaAceVirtualSoundCard VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Network)

add_executable(GigaAceVirtualSoundCard
    src/main.cpp
    src/ui/mainwindow.cpp
    src/core/ace_decoder.c
    src/core/decoder.cpp
    src/core/shared_bridge.cpp
    src/core/pcm_buffer.cpp
    src/core/demo_source.cpp
    src/core/pcap_source.cpp
    src/core/engine.cpp
    src/core/wasapi_output.cpp
)

add_library(GigaAceASIO SHARED
    src/asio/GigaAceASIO.def
    src/asio/asio_com.cpp
    src/asio/gigaace_asio.cpp
    src/core/shared_bridge.cpp
)

add_executable(GigaAceReplay
    src/tools/gigaace_replay.cpp
    src/core/ace_decoder.c
    src/core/shared_bridge.cpp
)

add_executable(GigaAceSetup WIN32
    src/tools/gigaace_setup.cpp
)

target_include_directories(GigaAceVirtualSoundCard PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
)

target_link_libraries(GigaAceVirtualSoundCard PRIVATE
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Network
    ws2_32
    winmm
)

target_compile_definitions(GigaAceVirtualSoundCard PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
)

target_include_directories(GigaAceASIO PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
)

target_compile_definitions(GigaAceASIO PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(GigaAceASIO PRIVATE
    ole32
    advapi32
    shlwapi
    user32
)

target_include_directories(GigaAceReplay PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
)

target_compile_definitions(GigaAceReplay PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_compile_definitions(GigaAceSetup PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(GigaAceSetup PRIVATE
    shell32
    ole32
    advapi32
    uuid
)

option(BUILD_DRIVER "Build the kernel-mode driver" OFF)
if(BUILD_DRIVER)
    add_subdirectory(driver)
endif()
