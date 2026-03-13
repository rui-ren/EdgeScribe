cmake_minimum_required(VERSION 3.18)
project(edgescribe VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Options ──────────────────────────────────────────────────────────────────
option(EDGESCRIBE_DOWNLOAD_GENAI "Download onnxruntime-genai prebuilt libs" ON)

# Allow user to provide their own GenAI installation
set(ORT_GENAI_PATH "" CACHE PATH "Path to onnxruntime-genai installation (include/ and lib/)")

# ── Platform detection ───────────────────────────────────────────────────────
if(WIN32)
    set(PLATFORM_RID "win-x64")
    set(GENAI_LIB_NAMES "onnxruntime-genai.lib")
    set(GENAI_DLL_PATTERN "*.dll")
elseif(APPLE)
    if(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(PLATFORM_RID "osx-arm64")
    else()
        set(PLATFORM_RID "osx-x64")
    endif()
    set(GENAI_LIB_NAMES "libonnxruntime-genai.dylib")
    set(GENAI_DLL_PATTERN "*.dylib")
else()
    set(PLATFORM_RID "linux-x64")
    set(GENAI_LIB_NAMES "libonnxruntime-genai.so")
    set(GENAI_DLL_PATTERN "*.so*")
endif()

message(STATUS "EdgeScribe platform: ${PLATFORM_RID}")

# ── Find or download onnxruntime-genai ───────────────────────────────────────
if(ORT_GENAI_PATH)
    message(STATUS "Using user-provided GenAI: ${ORT_GENAI_PATH}")
    set(GENAI_INCLUDE_DIR "${ORT_GENAI_PATH}/include")
    set(GENAI_LIB_DIR "${ORT_GENAI_PATH}/lib")
else()
    # For local development, point to the onnxruntime-genai build output
    # Users can set this via -DORT_GENAI_PATH=...
    message(STATUS "ORT_GENAI_PATH not set. Please provide path to onnxruntime-genai.")
    message(STATUS "  cmake -DORT_GENAI_PATH=/path/to/onnxruntime-genai/build/output ..")
    message(STATUS "  The path should contain include/ (with ort_genai.h) and lib/ directories.")
    message(FATAL_ERROR "ORT_GENAI_PATH is required. Set it to point to your onnxruntime-genai installation.")
endif()

# Verify the include directory exists — try multiple layouts
if(NOT EXISTS "${GENAI_INCLUDE_DIR}/ort_genai.h" AND NOT EXISTS "${GENAI_INCLUDE_DIR}/ort_genai_c.h")
    # Try: headers at root
    if(EXISTS "${ORT_GENAI_PATH}/ort_genai.h")
        set(GENAI_INCLUDE_DIR "${ORT_GENAI_PATH}")
    # Try: headers in src/ (source repo layout)
    elseif(EXISTS "${ORT_GENAI_PATH}/src/ort_genai.h")
        set(GENAI_INCLUDE_DIR "${ORT_GENAI_PATH}/src")
    else()
        message(FATAL_ERROR "Cannot find ort_genai.h in ${GENAI_INCLUDE_DIR}, ${ORT_GENAI_PATH}, or ${ORT_GENAI_PATH}/src")
    endif()
endif()

# Also try to auto-detect lib directory if not explicitly set
if(NOT EXISTS "${GENAI_LIB_DIR}")
    # Try: build output in build/Windows/Release/Release (source repo build layout)
    if(EXISTS "${ORT_GENAI_PATH}/build/Windows/Release/Release")
        set(GENAI_LIB_DIR "${ORT_GENAI_PATH}/build/Windows/Release/Release")
    elseif(EXISTS "${ORT_GENAI_PATH}/build/Release")
        set(GENAI_LIB_DIR "${ORT_GENAI_PATH}/build/Release")
    endif()
endif()

message(STATUS "GenAI include: ${GENAI_INCLUDE_DIR}")
message(STATUS "GenAI lib: ${GENAI_LIB_DIR}")

# ── Source files ─────────────────────────────────────────────────────────────
set(EDGESCRIBE_SOURCES
    src/cli/main.cpp
    src/asr/transcriber.cpp
    src/asr/audio_capture.cpp
    src/asr/audio_file.cpp
    src/asr/diarizer.cpp
    src/core/model_manager.cpp
    src/llm/llm_engine.cpp
    src/vision/vision_engine.cpp
    src/tts/tts_engine.cpp
    src/tts/phonemizer.cpp
    src/server/api_server.cpp
)

add_executable(edgescribe ${EDGESCRIBE_SOURCES})

target_include_directories(edgescribe PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${GENAI_INCLUDE_DIR}
)

# ── Find ONNX Runtime headers (needed for TTS engine) ───────────────────────
# ORT headers may be alongside GenAI headers or in a deps directory
set(ORT_INCLUDE_DIR "")
if(EXISTS "${GENAI_INCLUDE_DIR}/onnxruntime_cxx_api.h")
    set(ORT_INCLUDE_DIR "${GENAI_INCLUDE_DIR}")
elseif(EXISTS "${ORT_GENAI_PATH}/build/Windows/Release/_deps/ortlib-src/build/native/include/onnxruntime_cxx_api.h")
    set(ORT_INCLUDE_DIR "${ORT_GENAI_PATH}/build/Windows/Release/_deps/ortlib-src/build/native/include")
elseif(EXISTS "${GENAI_LIB_DIR}/../include/onnxruntime_cxx_api.h")
    set(ORT_INCLUDE_DIR "${GENAI_LIB_DIR}/../include")
elseif(EXISTS "${ORT_GENAI_PATH}/ort/include/onnxruntime_cxx_api.h")
    set(ORT_INCLUDE_DIR "${ORT_GENAI_PATH}/ort/include")
endif()

if(ORT_INCLUDE_DIR)
    message(STATUS "ORT include: ${ORT_INCLUDE_DIR}")
    target_include_directories(edgescribe PRIVATE ${ORT_INCLUDE_DIR})
else()
    message(WARNING "Could not find onnxruntime_cxx_api.h — TTS engine may not compile")
endif()

# ── Link onnxruntime-genai ───────────────────────────────────────────────────
if(WIN32)
    # On Windows, look for .lib import library
    find_library(GENAI_LIBRARY
        NAMES onnxruntime-genai
        PATHS "${GENAI_LIB_DIR}"
        NO_DEFAULT_PATH
    )
    if(NOT GENAI_LIBRARY)
        # Try finding the DLL directly and link against it
        file(GLOB GENAI_LIBS "${GENAI_LIB_DIR}/*.lib")
        if(GENAI_LIBS)
            list(GET GENAI_LIBS 0 GENAI_LIBRARY)
        else()
            message(FATAL_ERROR "Cannot find onnxruntime-genai library in ${GENAI_LIB_DIR}")
        endif()
    endif()
else()
    find_library(GENAI_LIBRARY
        NAMES onnxruntime-genai
        PATHS "${GENAI_LIB_DIR}"
        NO_DEFAULT_PATH
    )
    if(NOT GENAI_LIBRARY)
        message(FATAL_ERROR "Cannot find onnxruntime-genai library in ${GENAI_LIB_DIR}")
    endif()
endif()

target_link_libraries(edgescribe PRIVATE ${GENAI_LIBRARY})

# ── Link onnxruntime (needed for TTS ONNX Runtime C++ API) ──────────────────
find_library(ORT_LIBRARY
    NAMES onnxruntime
    PATHS "${GENAI_LIB_DIR}"
    NO_DEFAULT_PATH
)
if(ORT_LIBRARY)
    target_link_libraries(edgescribe PRIVATE ${ORT_LIBRARY})
    message(STATUS "ORT lib: ${ORT_LIBRARY}")
endif()

# ── Platform-specific audio backend for miniaudio ────────────────────────────
if(APPLE)
    find_library(COREAUDIO_FRAMEWORK CoreAudio)
    find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox)
    find_library(AUDIOUNIT_FRAMEWORK AudioUnit)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
    target_link_libraries(edgescribe PRIVATE
        ${COREAUDIO_FRAMEWORK}
        ${AUDIOTOOLBOX_FRAMEWORK}
        ${AUDIOUNIT_FRAMEWORK}
        ${COREFOUNDATION_FRAMEWORK}
    )
elseif(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(edgescribe PRIVATE
        Threads::Threads
        dl
        m
    )
    # Try to find PulseAudio or ALSA
    find_library(PULSE_LIBRARY pulse)
    if(PULSE_LIBRARY)
        target_link_libraries(edgescribe PRIVATE ${PULSE_LIBRARY})
    endif()
endif()

# ── Networking for model download ────────────────────────────────────────────
if(WIN32)
    target_link_libraries(edgescribe PRIVATE ws2_32 crypt32 winmm)
    target_compile_definitions(edgescribe PRIVATE
        _WIN32_WINNT=0x0A00
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

# ── Compile definitions ─────────────────────────────────────────────────────
target_compile_definitions(edgescribe PRIVATE
    EDGESCRIBE_VERSION="${PROJECT_VERSION}"
    EDGESCRIBE_PLATFORM="${PLATFORM_RID}"
)

# ── Copy DLLs to output (Windows) ───────────────────────────────────────────
if(WIN32)
    file(GLOB GENAI_DLLS "${GENAI_LIB_DIR}/*.dll")
    foreach(DLL ${GENAI_DLLS})
        add_custom_command(TARGET edgescribe POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLL}" $<TARGET_FILE_DIR:edgescribe>
        )
    endforeach()
elseif(APPLE)
    file(GLOB GENAI_DYLIBS "${GENAI_LIB_DIR}/*.dylib")
    foreach(DYLIB ${GENAI_DYLIBS})
        add_custom_command(TARGET edgescribe POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DYLIB}" $<TARGET_FILE_DIR:edgescribe>
        )
    endforeach()
else()
    file(GLOB GENAI_SOS "${GENAI_LIB_DIR}/*.so*")
    foreach(SO ${GENAI_SOS})
        add_custom_command(TARGET edgescribe POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SO}" $<TARGET_FILE_DIR:edgescribe>
        )
    endforeach()
endif()

# ── Install ──────────────────────────────────────────────────────────────────
install(TARGETS edgescribe RUNTIME DESTINATION bin)

# ── Package ──────────────────────────────────────────────────────────────────
set(CPACK_PACKAGE_NAME "edgescribe")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "On-device speech-to-text. Private. Open source.")
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
elseif(APPLE)
    set(CPACK_GENERATOR "TGZ")
else()
    set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)
