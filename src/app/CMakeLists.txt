find_package(Qt6 6.7 REQUIRED COMPONENTS Widgets Concurrent)
qt_standard_project_setup()

add_library(esl_files STATIC preset.cpp audio_exporter.cpp)
target_link_libraries(esl_files PUBLIC esl_dsp Qt6::Core)

qt_add_executable(EngineSoundLab WIN32 main.cpp main_window.cpp dashboard.cpp export_dialog.cpp audio_output.cpp)
file(GLOB preset_files CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/presets/*.json")
qt_add_resources(EngineSoundLab presets PREFIX "/presets" BASE "${PROJECT_SOURCE_DIR}/presets" FILES ${preset_files})
qt_add_resources(EngineSoundLab icons PREFIX "/" FILES icons/chevron-down.png icons/chevron-up.png)
target_link_libraries(EngineSoundLab PRIVATE esl_files Qt6::Widgets Qt6::Concurrent winmm avrt dwmapi)
target_compile_definitions(EngineSoundLab PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
set_target_properties(EngineSoundLab PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")

if(BUILD_TESTING)
    add_executable(esl_export_tests "${PROJECT_SOURCE_DIR}/tests/export_tests.cpp")
    target_link_libraries(esl_export_tests PRIVATE esl_files)
    add_test(NAME export COMMAND esl_export_tests "${PROJECT_SOURCE_DIR}/presets")
endif()
