cmake_minimum_required(VERSION 3.20)
project(WcwConsumerSmoke LANGUAGES CXX)

if(NOT DEFINED WCW_SOURCE_DIR)
    message(FATAL_ERROR "WCW_SOURCE_DIR must point to win32-custom-widgets")
endif()

add_subdirectory("${WCW_SOURCE_DIR}" win32-custom-widgets)

add_executable(WcwConsumerSmoke WIN32 main.cpp)
target_link_libraries(WcwConsumerSmoke PRIVATE Win32CustomWidgets::Win32CustomWidgets)

include(CTest)
add_test(NAME WcwConsumerRuntime COMMAND WcwConsumerSmoke)
