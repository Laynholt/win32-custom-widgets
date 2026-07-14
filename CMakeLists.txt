cmake_minimum_required(VERSION 3.21)
project(Win32CustomWidgets LANGUAGES CXX)

add_library(Win32CustomWidgets STATIC src/Theme.cpp src/Style.cpp src/Geometry.cpp
                                      src/Runtime.cpp src/Accessibility.cpp src/Paint.cpp src/Button.cpp src/Display.cpp
                                      src/TextBox.cpp src/NumericModel.cpp src/NumericBox.cpp
                                      src/BooleanControl.cpp src/Slider.cpp src/ProgressBar.cpp
                                      src/ComboModel.cpp src/ComboBox.cpp src/ScrollModel.cpp
                                      src/ScrollView.cpp src/Tooltip.cpp src/MenuModel.cpp src/Menu.cpp)
add_library(Win32CustomWidgets::Win32CustomWidgets ALIAS Win32CustomWidgets)
target_include_directories(Win32CustomWidgets PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(Win32CustomWidgets PUBLIC cxx_std_20)
target_compile_definitions(Win32CustomWidgets PUBLIC
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(Win32CustomWidgets PUBLIC user32 gdi32 gdiplus comctl32 oleacc)

option(WCW_BUILD_DEMO "Build the Win32 Custom Widgets gallery" ${PROJECT_IS_TOP_LEVEL})
if(WCW_BUILD_DEMO)
    add_executable(Win32CustomWidgetsDemo WIN32 demo/main.cpp)
    target_link_libraries(Win32CustomWidgetsDemo PRIVATE Win32CustomWidgets::Win32CustomWidgets)
    set_target_properties(Win32CustomWidgetsDemo PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
endif()

if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
function(wcw_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE Win32CustomWidgets::Win32CustomWidgets)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

wcw_add_test(Win32CustomWidgetsTests tests/test_theme.cpp)
wcw_add_test(StyleGeometryTests tests/test_style_geometry.cpp)
wcw_add_test(RuntimeTests tests/test_runtime.cpp)
wcw_add_test(ButtonDisplayTests tests/test_button_display.cpp)
wcw_add_test(TextNumericTests tests/test_text_numeric.cpp)
wcw_add_test(ValueControlsTests tests/test_value_controls.cpp)
wcw_add_test(ComboTests tests/test_combo.cpp)
wcw_add_test(ScrollTests tests/test_scroll.cpp)
wcw_add_test(TooltipTests tests/test_tooltip.cpp)
wcw_add_test(AccessibilityTests tests/test_accessibility.cpp)
wcw_add_test(MenuTests tests/test_menu.cpp)

add_test(NAME ConsumerSmoke
    COMMAND ${CMAKE_CTEST_COMMAND}
        --build-and-test "${CMAKE_CURRENT_SOURCE_DIR}/tests/consumer"
                         "${CMAKE_CURRENT_BINARY_DIR}/consumer-smoke"
        --build-generator "${CMAKE_GENERATOR}"
        --build-config "$<CONFIG>"
        --build-options
            "-DWCW_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DWCW_BUILD_DEMO=OFF"
            "-DBUILD_TESTING=ON"
        --test-command ${CMAKE_CTEST_COMMAND} -C "$<CONFIG>" --output-on-failure)
endif()
