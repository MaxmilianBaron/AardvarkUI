cmake_minimum_required(VERSION 3.21)
project(AardvarkUI VERSION 0.3.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "AardvarkUI requires Windows and Direct3D 9.")
endif()

option(AARDVARKUI_BUILD_EXAMPLES "Build the interactive demo" ${PROJECT_IS_TOP_LEVEL})
option(AARDVARKUI_BUILD_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})

add_library(AardvarkUI STATIC src/ui.cpp src/widgets.cpp src/data_table.cpp)
add_library(AardvarkUI::AardvarkUI ALIAS AardvarkUI)
target_compile_features(AardvarkUI PUBLIC cxx_std_17)
target_compile_definitions(AardvarkUI PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(AardvarkUI PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(AardvarkUI PUBLIC d3d9 gdi32 user32)
if(MSVC)
    target_compile_options(AardvarkUI PRIVATE /W4 /permissive- /utf-8)
endif()

if(AARDVARKUI_BUILD_EXAMPLES)
    add_executable(aardvark_ui_demo WIN32 examples/demo.cpp examples/demo.rc)
    target_link_libraries(aardvark_ui_demo PRIVATE AardvarkUI shell32 gdiplus ole32)
    target_include_directories(aardvark_ui_demo PRIVATE examples)
endif()
if(AARDVARKUI_BUILD_TESTS)
    enable_testing()
    add_executable(aardvark_ui_tests tests/ui_tests.cpp)
    target_link_libraries(aardvark_ui_tests PRIVATE AardvarkUI)
    add_test(NAME ui_contracts COMMAND aardvark_ui_tests)
    add_executable(aardvark_table_tests tests/table_tests.cpp)
    target_link_libraries(aardvark_table_tests PRIVATE AardvarkUI)
    add_test(NAME table_contracts COMMAND aardvark_table_tests)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS AardvarkUI EXPORT AardvarkUITargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT AardvarkUITargets NAMESPACE AardvarkUI:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AardvarkUI)
configure_package_config_file(cmake/AardvarkUIConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/AardvarkUIConfig.cmake INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AardvarkUI)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/AardvarkUIConfigVersion.cmake COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/AardvarkUIConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/AardvarkUIConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AardvarkUI)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/AardvarkUI)
