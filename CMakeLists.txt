cmake_minimum_required(VERSION 3.20)
project(Win32CustomWidgets LANGUAGES CXX)

add_library(Win32CustomWidgets STATIC src/Theme.cpp src/Style.cpp src/Geometry.cpp
                                      src/Runtime.cpp src/Paint.cpp src/Button.cpp src/Display.cpp)
add_library(Win32CustomWidgets::Win32CustomWidgets ALIAS Win32CustomWidgets)
target_include_directories(Win32CustomWidgets PUBLIC include)
target_compile_features(Win32CustomWidgets PUBLIC cxx_std_20)
target_link_libraries(Win32CustomWidgets PUBLIC user32 gdi32 gdiplus comctl32 oleacc)

enable_testing()
add_executable(Win32CustomWidgetsTests tests/test_theme.cpp)
target_link_libraries(Win32CustomWidgetsTests PRIVATE Win32CustomWidgets::Win32CustomWidgets)
add_test(NAME Win32CustomWidgetsTests COMMAND Win32CustomWidgetsTests)

add_executable(StyleGeometryTests tests/test_style_geometry.cpp)
target_link_libraries(StyleGeometryTests PRIVATE Win32CustomWidgets::Win32CustomWidgets)
add_test(NAME StyleGeometryTests COMMAND StyleGeometryTests)

add_executable(RuntimeTests tests/test_runtime.cpp)
target_link_libraries(RuntimeTests PRIVATE Win32CustomWidgets::Win32CustomWidgets)
add_test(NAME RuntimeTests COMMAND RuntimeTests)

add_executable(ButtonDisplayTests tests/test_button_display.cpp)
target_link_libraries(ButtonDisplayTests PRIVATE Win32CustomWidgets::Win32CustomWidgets)
add_test(NAME ButtonDisplayTests COMMAND ButtonDisplayTests)
