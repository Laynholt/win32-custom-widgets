cmake_minimum_required(VERSION 3.20)
project(Win32CustomWidgets LANGUAGES CXX)
if(NOT DEFINED PROJECT_IS_TOP_LEVEL)
    if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
        set(PROJECT_IS_TOP_LEVEL ON)
    else()
        set(PROJECT_IS_TOP_LEVEL OFF)
    endif()
endif()

add_library(Win32CustomWidgets STATIC src/Theme.cpp src/Style.cpp src/Geometry.cpp
                                      src/Runtime.cpp src/Accessibility.cpp src/Paint.cpp src/Button.cpp src/Display.cpp
                                      src/TextBox.cpp src/NumericModel.cpp src/NumericBox.cpp
                                      src/BooleanControl.cpp src/Slider.cpp src/ProgressBar.cpp
                                      src/ComboModel.cpp src/ComboBox.cpp src/ScrollModel.cpp
                                      src/ScrollView.cpp src/Tooltip.cpp)
add_library(Win32CustomWidgets::Win32CustomWidgets ALIAS Win32CustomWidgets)
target_include_directories(Win32CustomWidgets PUBLIC include)
target_compile_features(Win32CustomWidgets PUBLIC cxx_std_20)
target_link_libraries(Win32CustomWidgets PUBLIC user32 gdi32 gdiplus comctl32 oleacc)

option(WCW_BUILD_DEMO "Build the Win32 Custom Widgets gallery" ${PROJECT_IS_TOP_LEVEL})
if(WCW_BUILD_DEMO)
    add_executable(Win32CustomWidgetsDemo WIN32 demo/main.cpp)
    target_link_libraries(Win32CustomWidgetsDemo PRIVATE Win32CustomWidgets::Win32CustomWidgets)
    target_compile_definitions(Win32CustomWidgetsDemo PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    set_target_properties(Win32CustomWidgetsDemo PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
endif()

if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
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

add_executable(TextNumericTests tests/test_text_numeric.cpp)
target_link_libraries(TextNumericTests PRIVATE Win32CustomWidgets::Win32CustomWidgets)
add_test(NAME TextNumericTests COMMAND TextNumericTests)

add_executable(ValueControlsTests tests/test_value_controls.cpp)
target_link_libraries(ValueControlsTests PRIVATE Win32CustomWidgets::Win32CustomWidgets)
add_test(NAME ValueControlsTests COMMAND ValueControlsTests)

add_executable(ComboTests tests/test_combo.cpp)
target_link_libraries(ComboTests PRIVATE Win32CustomWidgets::Win32CustomWidgets)
add_test(NAME ComboTests COMMAND ComboTests)

add_executable(ScrollTests tests/test_scroll.cpp)
target_link_libraries(ScrollTests PRIVATE Win32CustomWidgets::Win32CustomWidgets)
add_test(NAME ScrollTests COMMAND ScrollTests)

add_executable(TooltipTests tests/test_tooltip.cpp)
target_link_libraries(TooltipTests PRIVATE Win32CustomWidgets::Win32CustomWidgets)
add_test(NAME TooltipTests COMMAND TooltipTests)

add_executable(AccessibilityTests tests/test_accessibility.cpp)
target_link_libraries(AccessibilityTests PRIVATE Win32CustomWidgets::Win32CustomWidgets)
add_test(NAME AccessibilityTests COMMAND AccessibilityTests)
endif()
