set(TRAVERSO_ENGINE_SOURCES
    AudioBus.cpp
    AudioChannel.cpp
    AudioDevice.cpp
    AudioDeviceThread.cpp
    TAudioDeviceClient.cpp
    TAudioDriver.cpp
    memops.cpp
)

if(HAVE_ALSA)
    list(APPEND TRAVERSO_ENGINE_SOURCES AlsaDriver.cpp)
endif()

if(HAVE_JACK)
    list(APPEND TRAVERSO_ENGINE_SOURCES JackDriver.cpp)
endif()

if(HAVE_PORTAUDIO)
    list(APPEND TRAVERSO_ENGINE_SOURCES PADriver.cpp)
endif()

if(HAVE_PULSEAUDIO)
    list(APPEND TRAVERSO_ENGINE_SOURCES PulseAudioDriver.cpp)
endif()

if(HAVE_COREAUDIO)
    list(APPEND TRAVERSO_ENGINE_SOURCES CoreAudioDriver.cpp)
endif()

add_library(traversoaudiobackend STATIC ${TRAVERSO_ENGINE_SOURCES})

target_include_directories(traversoaudiobackend
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/common
    PRIVATE
        ${PROJECT_BINARY_DIR}
        ${PROJECT_BINARY_DIR}/src
)

target_link_libraries(traversoaudiobackend
    PUBLIC
        Qt5::Core
        Qt5::Widgets
)

if(HAVE_ALSA)
    target_link_libraries(traversoaudiobackend PUBLIC PkgConfig::ALSA)
endif()

if(HAVE_JACK)
    target_link_libraries(traversoaudiobackend PUBLIC PkgConfig::JACK)
endif()

if(HAVE_PORTAUDIO)
    target_link_libraries(traversoaudiobackend PUBLIC PkgConfig::PORTAUDIO)
endif()

if(HAVE_PULSEAUDIO)
    target_link_libraries(traversoaudiobackend PUBLIC PkgConfig::PULSEAUDIO)
endif()

if(HAVE_COREAUDIO)
    target_link_libraries(traversoaudiobackend PUBLIC "-framework CoreAudio" "-framework AudioToolbox" "-framework AudioUnit")
endif()

if(WANT_PCH)
    target_precompile_headers(traversoaudiobackend PRIVATE "${CMAKE_SOURCE_DIR}/src/precompile.h")
endif()
