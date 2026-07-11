cmake_minimum_required(VERSION 3.20)
project(Win32CustomWidgets LANGUAGES CXX)

add_library(Win32CustomWidgets STATIC src/Theme.cpp)
add_library(Win32CustomWidgets::Win32CustomWidgets ALIAS Win32CustomWidgets)
target_include_directories(Win32CustomWidgets PUBLIC include)
target_compile_features(Win32CustomWidgets PUBLIC cxx_std_20)
target_link_libraries(Win32CustomWidgets PUBLIC user32 gdi32 gdiplus comctl32 oleacc)

enable_testing()
add_executable(Win32CustomWidgetsTests tests/test_theme.cpp)
target_link_libraries(Win32CustomWidgetsTests PRIVATE Win32CustomWidgets::Win32CustomWidgets)
add_test(NAME Win32CustomWidgetsTests COMMAND Win32CustomWidgetsTests)
