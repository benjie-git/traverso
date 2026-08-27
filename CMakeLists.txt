cmake_minimum_required(VERSION 3.16...3.30)

project(traverso
    VERSION 0.50.0
    LANGUAGES C CXX
)

set(VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(VERSION_SUFFIX "-git-01-11-2010")
set(VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}${VERSION_SUFFIX}")

# C/C++ standard requirements
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Standard install paths and output directories
include(GNUInstallDirs)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")

# Qt6 setup
if(APPLE)
    if(EXISTS "/opt/homebrew/opt/qt")
        list(PREPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/qt")
    elseif(EXISTS "/opt/homebrew/opt/qt@6")
        list(PREPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/qt@6")
    elseif(EXISTS "/usr/local/opt/qt")
        list(PREPEND CMAKE_PREFIX_PATH "/usr/local/opt/qt")
    elseif(EXISTS "/usr/local/opt/qt@6")
        list(PREPEND CMAKE_PREFIX_PATH "/usr/local/opt/qt@6")
    elseif(EXISTS "/opt/local/libexec/qt6")
        list(PREPEND CMAKE_PREFIX_PATH "/opt/local/libexec/qt6")
    endif()
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 6.0 REQUIRED COMPONENTS Core Widgets Xml)

# Options with platform defaults
if(APPLE)
    set(DEFAULT_WANT_ALSA OFF)
    set(DEFAULT_WANT_JACK OFF)
    set(DEFAULT_WANT_PORTAUDIO ON)
    set(DEFAULT_WANT_COREAUDIO OFF)
    set(DEFAULT_WANT_LV2 ON)
elseif(WIN32)
    set(DEFAULT_WANT_ALSA OFF)
    set(DEFAULT_WANT_JACK OFF)
    set(DEFAULT_WANT_PORTAUDIO ON)
    set(DEFAULT_WANT_COREAUDIO OFF)
    set(DEFAULT_WANT_LV2 OFF)
else()
    set(DEFAULT_WANT_ALSA ON)
    set(DEFAULT_WANT_JACK ON)
    set(DEFAULT_WANT_PORTAUDIO OFF)
    set(DEFAULT_WANT_COREAUDIO OFF)
    set(DEFAULT_WANT_LV2 ON)
endif()

option(WANT_JACK "Include JACK Audio Connection Kit support" ${DEFAULT_WANT_JACK})
option(WANT_ALSA "Include ALSA support" ${DEFAULT_WANT_ALSA})
option(WANT_PORTAUDIO "Include PortAudio support" ${DEFAULT_WANT_PORTAUDIO})
option(WANT_PULSEAUDIO "Include PulseAudio support" OFF)
option(WANT_COREAUDIO "Include CoreAudio support (macOS)" ${DEFAULT_WANT_COREAUDIO})
option(WANT_LV2 "Include LV2 Plugin support" ${DEFAULT_WANT_LV2})
option(WANT_MP3_DECODE "Include MP3 decoding support (libmad)" ON)
option(WANT_MP3_ENCODE "Include MP3 encoding support (lame)" ON)
option(WANT_M4A_ENCODE "Include M4A/AAC encoding support (faac)" ON)
option(WANT_PCH "Use precompiled headers" OFF)
option(WANT_TRAVERSO_DEBUG "Enable Traverso debug output on command line" OFF)
option(WANT_THREAD_CHECK "Enable runtime thread checks for debugging" OFF)
option(WANT_VECLIB_OPTIMIZATIONS "Build with VecLib optimizations (Mac OS X PowerPC/Tiger)" OFF)
option(WANT_SSE "Use SSE instruction set optimizations (x86/x86_64 only)" ON)

# Version tagging option
if(TAG_VERSION)
    message(STATUS "Selected version tag: ${TAG_VERSION}")
elseif(CUSTOM_VERSIONTAG)
    set(TAG_VERSION "${CUSTOM_VERSIONTAG}" CACHE STRING "Custom version tag")
endif()

# Global build definitions
set(TRAVERSO_DEFINES_LIST "STATIC_BUILD")

macro(traverso_add_definition def)
    list(APPEND TRAVERSO_DEFINES_LIST "${def}")
    add_compile_definitions(${def})
endmacro()

add_compile_definitions(STATIC_BUILD)

if(WANT_TRAVERSO_DEBUG OR CMAKE_BUILD_TYPE STREQUAL "Debug")
    traverso_add_definition(USE_DEBUGGER)
endif()

if(WANT_THREAD_CHECK)
    traverso_add_definition(THREAD_CHECK)
endif()

if(APPLE AND WANT_VECLIB_OPTIMIZATIONS)
    traverso_add_definition(BUILD_VECLIB_OPTIMIZATIONS)
endif()

if(WIN32)
    traverso_add_definition(QT_DLL)
    traverso_add_definition(WIN_BUILD)
endif()

# Architecture and SSE optimizations detection
set(ENABLE_SSE_OPT FALSE)
set(HOST_SUPPORTS_SSE FALSE)
set(IS_ARCH_X86 FALSE)
set(IS_ARCH_X86_64 FALSE)

if(WANT_SSE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$" OR (CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86)$" AND CMAKE_SIZEOF_VOID_P EQUAL 8))
        enable_language(ASM)
        set(ENABLE_SSE_OPT TRUE)
        set(HOST_SUPPORTS_SSE TRUE)
        set(IS_ARCH_X86_64 TRUE)
        traverso_add_definition(ARCH_X86)
        traverso_add_definition(ARCH_X86_64)
        traverso_add_definition(SSE_OPTIMIZATIONS)
        traverso_add_definition(USE_XMMINTRIN)
        traverso_add_definition(USE_X86_64_ASM)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i[3-6]86|x86)$" OR (CMAKE_SIZEOF_VOID_P EQUAL 4 AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm.*|aarch64)$"))
        enable_language(ASM)
        set(ENABLE_SSE_OPT TRUE)
        set(HOST_SUPPORTS_SSE TRUE)
        set(IS_ARCH_X86 TRUE)
        traverso_add_definition(ARCH_X86)
        traverso_add_definition(SSE_OPTIMIZATIONS)
        traverso_add_definition(USE_XMMINTRIN)
    endif()
endif()

# System checks and definitions
include(ConfigureChecks.cmake)

# Dependencies detection via PkgConfig
find_package(PkgConfig REQUIRED)

pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)
pkg_check_modules(SAMPLERATE REQUIRED IMPORTED_TARGET samplerate)
pkg_check_modules(FFTW3 REQUIRED IMPORTED_TARGET fftw3)
pkg_check_modules(WAVPACK REQUIRED IMPORTED_TARGET wavpack>=4.40.0)
pkg_check_modules(OGG REQUIRED IMPORTED_TARGET ogg>=1.1.2)
pkg_check_modules(VORBIS REQUIRED IMPORTED_TARGET vorbis>=1.1.2)
pkg_check_modules(VORBISFILE REQUIRED IMPORTED_TARGET vorbisfile)
pkg_check_modules(VORBISENC REQUIRED IMPORTED_TARGET vorbisenc)
pkg_check_modules(FLAC REQUIRED IMPORTED_TARGET flac>=1.1.2)

set(HAVE_PORTAUDIO FALSE)
if(WANT_PORTAUDIO)
    pkg_check_modules(PORTAUDIO IMPORTED_TARGET portaudio-2.0>=19)
    if(PORTAUDIO_FOUND)
        set(HAVE_PORTAUDIO TRUE)
        traverso_add_definition(PORTAUDIO_SUPPORT)
    else()
        message(FATAL_ERROR "PortAudio requested but not found via pkg-config (portaudio-2.0)")
    endif()
endif()

set(HAVE_ALSA FALSE)
if(WANT_ALSA)
    pkg_check_modules(ALSA IMPORTED_TARGET alsa>=1.0.0)
    if(ALSA_FOUND)
        set(HAVE_ALSA TRUE)
        traverso_add_definition(ALSA_SUPPORT)
    else()
        message(FATAL_ERROR "ALSA requested but not found via pkg-config (alsa)")
    endif()
endif()

set(HAVE_JACK FALSE)
if(WANT_JACK)
    pkg_check_modules(JACK IMPORTED_TARGET jack>=0.100)
    if(JACK_FOUND)
        set(HAVE_JACK TRUE)
        traverso_add_definition(JACK_SUPPORT)
    else()
        message(FATAL_ERROR "JACK requested but not found via pkg-config (jack)")
    endif()
endif()

set(HAVE_PULSEAUDIO FALSE)
if(WANT_PULSEAUDIO)
    pkg_check_modules(PULSEAUDIO IMPORTED_TARGET libpulse>=0.9)
    if(PULSEAUDIO_FOUND)
        set(HAVE_PULSEAUDIO TRUE)
        traverso_add_definition(PULSEAUDIO_SUPPORT)
    else()
        message(FATAL_ERROR "PulseAudio requested but not found via pkg-config (libpulse)")
    endif()
endif()

set(HAVE_COREAUDIO FALSE)
if(APPLE AND WANT_COREAUDIO)
    set(HAVE_COREAUDIO TRUE)
    traverso_add_definition(COREAUDIO_SUPPORT)
endif()

set(HAVE_LILV FALSE)
set(LV2_SUPPORT FALSE)
if(WANT_LV2)
    pkg_check_modules(LIBLILV IMPORTED_TARGET lilv-0>=0.4.4)
    if(LIBLILV_FOUND)
        set(HAVE_LILV TRUE)
        set(LV2_SUPPORT TRUE)
        traverso_add_definition(LV2_SUPPORT)
    else()
        message(STATUS "LILV not found, LV2 support disabled")
    endif()
endif()

set(HAVE_MP3_DECODING FALSE)
if(WANT_MP3_DECODE)
    pkg_check_modules(MAD IMPORTED_TARGET mad>=0.15.0)
    if(MAD_FOUND)
        set(HAVE_MP3_DECODING TRUE)
        traverso_add_definition(MP3_DECODE_SUPPORT)
    else()
        message(FATAL_ERROR "MP3 decoding requested but libmad not found via pkg-config")
    endif()
endif()

set(HAVE_MP3_ENCODING FALSE)
if(WANT_MP3_ENCODE)
    pkg_check_modules(LAME IMPORTED_TARGET lame)
    if(NOT LAME_FOUND)
        pkg_check_modules(LAME IMPORTED_TARGET mp3lame)
    endif()
    if(NOT LAME_FOUND)
        find_path(LAME_INCLUDE_DIR lame/lame.h)
        find_library(LAME_LIBRARY NAMES mp3lame lame)
        if(LAME_INCLUDE_DIR AND LAME_LIBRARY)
            add_library(PkgConfig::LAME UNKNOWN IMPORTED)
            set_target_properties(PkgConfig::LAME PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${LAME_INCLUDE_DIR}"
                IMPORTED_LOCATION "${LAME_LIBRARY}"
            )
            set(LAME_FOUND TRUE)
        endif()
    endif()
    if(LAME_FOUND)
        set(HAVE_MP3_ENCODING TRUE)
        traverso_add_definition(MP3_ENCODE_SUPPORT)
    else()
        message(FATAL_ERROR "MP3 encoding requested but lame not found")
    endif()
endif()

set(HAVE_M4A_ENCODING FALSE)
if(WANT_M4A_ENCODE)
    pkg_check_modules(FAAC IMPORTED_TARGET faac)
    if(NOT FAAC_FOUND)
        find_path(FAAC_INCLUDE_DIR faac.h)
        find_library(FAAC_LIBRARY NAMES faac)
        if(FAAC_INCLUDE_DIR AND FAAC_LIBRARY)
            add_library(PkgConfig::FAAC UNKNOWN IMPORTED)
            set_target_properties(PkgConfig::FAAC PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${FAAC_INCLUDE_DIR}"
                IMPORTED_LOCATION "${FAAC_LIBRARY}"
            )
            set(FAAC_FOUND TRUE)
        endif()
    endif()
    if(FAAC_FOUND)
        set(HAVE_M4A_ENCODING TRUE)
        traverso_add_definition(M4A_ENCODE_SUPPORT)
    else()
        message(FATAL_ERROR "M4A encoding requested but faac not found")
    endif()
endif()

# Build formatted TRAVERSO_DEFINES string
set(TRAVERSO_DEFINES "")
foreach(def IN LISTS TRAVERSO_DEFINES_LIST)
    string(APPEND TRAVERSO_DEFINES "-D${def} ")
endforeach()
string(STRIP "${TRAVERSO_DEFINES}" TRAVERSO_DEFINES)

# Generate config.h
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake"
    "${PROJECT_BINARY_DIR}/src/config.h"
)

# Uninstall target
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
    "${PROJECT_BINARY_DIR}/cmake_uninstall.cmake"
    @ONLY
)
if(NOT TARGET uninstall)
    add_custom_target(uninstall
        COMMAND "${CMAKE_COMMAND}" -P "${PROJECT_BINARY_DIR}/cmake_uninstall.cmake"
    )
endif()

# Add source directory
add_subdirectory(src)

# Install Linux desktop integration files
if(UNIX AND NOT APPLE)
    install(FILES resources/traverso.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES resources/x-traverso.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
    install(FILES resources/images/traverso.png DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps)
    install(DIRECTORY resources/freedesktop/icons/ DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor)
endif()

# CPack configuration
set(CPACK_PACKAGE_NAME "traverso")
set(CPACK_PACKAGE_VENDOR "Traverso Team")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Traverso is an open source Digital Audio Workstation for Linux, macOS and Windows")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Traverso ${PROJECT_VERSION}")
set(CPACK_SOURCE_GENERATOR "TGZ;TXZ")
set(CPACK_SOURCE_IGNORE_FILES
    "/build/"
    "/\\\\.git/"
    "/\\\\.gitignore"
    "/\\\\.cache"
    "/.*~$"
)
include(CPack)

# Configuration summary
message(STATUS "
--------------------------------------------------
Traverso ${VERSION} Configuration Summary:
  Install prefix        : ${CMAKE_INSTALL_PREFIX}
  Build type            : ${CMAKE_BUILD_TYPE}
  ALSA support          : ${HAVE_ALSA}
  JACK support          : ${HAVE_JACK}
  PortAudio support     : ${HAVE_PORTAUDIO}
  PulseAudio support    : ${HAVE_PULSEAUDIO}
  CoreAudio support     : ${HAVE_COREAUDIO}
  LV2 (Lilv) support    : ${HAVE_LILV}
  MP3 decode support    : ${HAVE_MP3_DECODING}
  MP3 encode support    : ${HAVE_MP3_ENCODING}
  M4A encode support    : ${HAVE_M4A_ENCODING}
  SSE optimizations     : ${HOST_SUPPORTS_SSE}
  Precompiled headers   : ${WANT_PCH}
--------------------------------------------------
")
