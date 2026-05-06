cmake_minimum_required(VERSION 3.21)
project(GigaAceVSC LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

find_package(WDK REQUIRED)

wdk_add_driver(GigaAceVSC
    driver.cpp
    device.cpp
    miniport.cpp
    miniportwavert.cpp
    miniportwavertstream.cpp
    sharedmemory.cpp
)

target_include_directories(GigaAceVSC PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(GigaAceVSC PRIVATE
    portcls.lib
    stdunk.lib
    ks.lib
)

target_compile_definitions(GigaAceVSC PRIVATE
    UNICODE
    _UNICODE
    DBG=1
)

target_compile_options(GigaAceVSC PRIVATE
    /W4
    /WX
    /wd4100
)
