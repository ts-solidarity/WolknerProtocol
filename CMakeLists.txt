cmake_minimum_required(VERSION 3.20)
project(WolknerProtocol LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Concurrent)

add_executable(wolknerprotocol
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/ProcessMem.cpp
    src/ProcessMem.h
    src/Scanner.cpp
    src/Scanner.h
    resources/fonts.qrc
)

target_link_libraries(wolknerprotocol PRIVATE Qt6::Widgets Qt6::Concurrent)

if(WIN32)
    enable_language(RC)
    # Windows-only system libraries used by ProcessMem.cpp
    target_link_libraries(wolknerprotocol PRIVATE psapi)
    # Embed icon + version info into the PE so Windows Explorer/taskbar
    # show Serge instead of the generic "no icon" placeholder.
    target_sources(wolknerprotocol PRIVATE resources/wolknerprotocol.rc)
    set_target_properties(wolknerprotocol PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
