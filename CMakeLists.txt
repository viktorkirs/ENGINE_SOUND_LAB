cmake_minimum_required(VERSION 3.24)
project(EngineSoundLab VERSION 1.2.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(esl_dsp STATIC
    src/core/engine_model.cpp
    src/core/simulation.cpp
    src/core/dsp.cpp
    src/core/recording.cpp
    src/core/listener.cpp
    src/core/render.cpp
)
target_include_directories(esl_dsp PUBLIC src)
if(MSVC)
    add_compile_options(/utf-8)
    target_compile_options(esl_dsp PRIVATE /utf-8 /W4)
else()
    target_compile_options(esl_dsp PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(esl_dsp_tests tests/dsp_tests.cpp)
    target_link_libraries(esl_dsp_tests PRIVATE esl_dsp)
    add_test(NAME dsp COMMAND esl_dsp_tests)
endif()

option(ESL_BUILD_APP "Build the Qt desktop application" ON)
if(ESL_BUILD_APP)
    add_subdirectory(src/app)
endif()
