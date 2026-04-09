cmake_minimum_required(VERSION 3.24)
project(MeloDick LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MELODICK_BUILD_APP "Build MeloDick standalone bootstrap app" ON)
option(MELODICK_BUILD_TESTS "Build MeloDick tests" ON)
option(MELODICK_ENABLE_ONNX "Enable ONNX Runtime backends" ON)

set(MELODICK_OLD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../MelodickOld" CACHE PATH "Path to old MeloDick assets")
set(MELODICK_ONNXRUNTIME_ROOT "${MELODICK_OLD_ROOT}/onnxruntime-win-x64-1.24.4" CACHE PATH "Path to ONNX Runtime root")

add_library(melodick_core
    src/core/audio_resampler.cpp
    src/core/note_blob.cpp
    src/core/pitch_preprocessing.cpp
)
target_include_directories(melodick_core PUBLIC include)

add_library(melodick_io
    src/io/wav_io.cpp
)
target_include_directories(melodick_io PUBLIC include)
target_link_libraries(melodick_io PUBLIC melodick_core)

add_library(melodick_capabilities_core
    src/capabilities/segmenter.cpp
)
target_include_directories(melodick_capabilities_core PUBLIC include)
target_link_libraries(melodick_capabilities_core PUBLIC melodick_core)

add_library(melodick_capabilities_onnx
    src/capabilities/onnx_backends.cpp
)
target_include_directories(melodick_capabilities_onnx PUBLIC include)
target_link_libraries(melodick_capabilities_onnx PUBLIC melodick_core melodick_capabilities_core)
target_compile_definitions(melodick_capabilities_onnx PRIVATE MELODICK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

if(MELODICK_ENABLE_ONNX)
    if(WIN32)
        set(MELODICK_ONNXRUNTIME_INCLUDE_DIR "${MELODICK_ONNXRUNTIME_ROOT}/include")
        set(MELODICK_ONNXRUNTIME_LIB_DIR "${MELODICK_ONNXRUNTIME_ROOT}/lib")
        set(MELODICK_ONNXRUNTIME_LIB "${MELODICK_ONNXRUNTIME_LIB_DIR}/onnxruntime.lib")
        set(MELODICK_ONNXRUNTIME_DLL "${MELODICK_ONNXRUNTIME_LIB_DIR}/onnxruntime.dll")

        if(NOT EXISTS "${MELODICK_ONNXRUNTIME_LIB}")
            message(FATAL_ERROR "onnxruntime.lib not found: ${MELODICK_ONNXRUNTIME_LIB}")
        endif()
        if(NOT EXISTS "${MELODICK_ONNXRUNTIME_INCLUDE_DIR}/onnxruntime_cxx_api.h")
            message(FATAL_ERROR "onnxruntime headers not found in ${MELODICK_ONNXRUNTIME_INCLUDE_DIR}")
        endif()

        target_include_directories(melodick_capabilities_onnx PUBLIC "${MELODICK_ONNXRUNTIME_INCLUDE_DIR}")
        target_link_libraries(melodick_capabilities_onnx PUBLIC "${MELODICK_ONNXRUNTIME_LIB}")
    else()
        message(FATAL_ERROR "Current bootstrap ONNX integration supports Windows first.")
    endif()
endif()

add_library(melodick_render
    src/render/render_group_planner.cpp
    src/render/dirty_timeline.cpp
    src/render/lazy_render_scheduler.cpp
)
target_include_directories(melodick_render PUBLIC include)
target_link_libraries(melodick_render PUBLIC melodick_core)

add_library(melodick_app
    src/app/capability_chain.cpp
    src/app/session.cpp
)
target_include_directories(melodick_app PUBLIC include)
target_link_libraries(melodick_app PUBLIC melodick_core melodick_capabilities_core melodick_render melodick_io)

add_library(melodick_project
    src/project/project_state.cpp
)
target_include_directories(melodick_project PUBLIC include)
target_link_libraries(melodick_project PUBLIC melodick_core)
if(WIN32)
    target_link_libraries(melodick_project PUBLIC winsqlite3)
else()
    message(FATAL_ERROR "Current project persistence implementation requires winsqlite3 on Windows.")
endif()
target_link_libraries(melodick_app PUBLIC melodick_project)

if(MELODICK_BUILD_APP)
    add_executable(melodick_standalone_bootstrap
        src/app/standalone/main.cpp
    )
    target_link_libraries(melodick_standalone_bootstrap PRIVATE melodick_app melodick_capabilities_onnx)

    add_executable(melodick_analysis_dump
        src/tools/analysis_dump_main.cpp
    )
    target_link_libraries(melodick_analysis_dump PRIVATE melodick_capabilities_onnx melodick_io)
    if(WIN32 AND MELODICK_ENABLE_ONNX)
        add_custom_command(TARGET melodick_standalone_bootstrap POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MELODICK_ONNXRUNTIME_DLL}"
                "$<TARGET_FILE_DIR:melodick_standalone_bootstrap>/onnxruntime.dll")
        add_custom_command(TARGET melodick_analysis_dump POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MELODICK_ONNXRUNTIME_DLL}"
                "$<TARGET_FILE_DIR:melodick_analysis_dump>/onnxruntime.dll")
    endif()
endif()

if(MELODICK_BUILD_TESTS)
    include(CTest)
    enable_testing()

    add_executable(melodick_tests
        tests/test_main.cpp
        tests/test_audio_resampler.cpp
        tests/test_note_blob.cpp
        tests/test_segmentation.cpp
        tests/test_render.cpp
        tests/test_session.cpp
        tests/test_project.cpp
    )
    target_link_libraries(melodick_tests PRIVATE melodick_app)
    add_test(NAME melodick_tests COMMAND melodick_tests)
    if(WIN32 AND MELODICK_ENABLE_ONNX)
        add_custom_command(TARGET melodick_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MELODICK_ONNXRUNTIME_DLL}"
                "$<TARGET_FILE_DIR:melodick_tests>/onnxruntime.dll")
    endif()
endif()
