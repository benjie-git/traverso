set(TRAVERSO_AUDIOFILEIO_SOURCES
    decode/AbstractAudioReader.cpp
    decode/SFAudioReader.cpp
    decode/FlacAudioReader.cpp
    decode/ResampleAudioReader.cpp
    decode/VorbisAudioReader.cpp
    decode/WPAudioReader.cpp
    encode/AbstractAudioWriter.cpp
    encode/SFAudioWriter.cpp
    encode/WPAudioWriter.cpp
    encode/FlacAudioWriter.cpp
    encode/VorbisAudioWriter.cpp
)

if(HAVE_MP3_DECODING)
    list(APPEND TRAVERSO_AUDIOFILEIO_SOURCES decode/MadAudioReader.cpp)
endif()

if(HAVE_M4A_DECODING)
    list(APPEND TRAVERSO_AUDIOFILEIO_SOURCES decode/FaadAudioReader.cpp)
endif()

if(HAVE_MP3_ENCODING)
    list(APPEND TRAVERSO_AUDIOFILEIO_SOURCES encode/LameAudioWriter.cpp)
endif()

if(HAVE_M4A_ENCODING)
    list(APPEND TRAVERSO_AUDIOFILEIO_SOURCES encode/FaacAudioWriter.cpp)
endif()

add_library(traversoaudiofileio STATIC ${TRAVERSO_AUDIOFILEIO_SOURCES})

target_include_directories(traversoaudiofileio
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/decode
        ${CMAKE_CURRENT_SOURCE_DIR}/encode
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_SOURCE_DIR}/src/3rdparty
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
        ${PROJECT_BINARY_DIR}
        ${PROJECT_BINARY_DIR}/src
)

target_link_libraries(traversoaudiofileio
    PUBLIC
        Qt6::Core
        Qt6::Widgets
        PkgConfig::SNDFILE
        PkgConfig::SAMPLERATE
        PkgConfig::WAVPACK
        PkgConfig::OGG
        PkgConfig::VORBIS
        PkgConfig::VORBISFILE
        PkgConfig::VORBISENC
        PkgConfig::FLAC
)

if(HAVE_MP3_DECODING)
    target_link_libraries(traversoaudiofileio PUBLIC PkgConfig::MAD)
endif()

if(HAVE_M4A_DECODING)
    target_link_libraries(traversoaudiofileio PUBLIC PkgConfig::FAAD2)
endif()

if(HAVE_MP3_ENCODING)
    target_link_libraries(traversoaudiofileio PUBLIC PkgConfig::LAME)
endif()

if(HAVE_M4A_ENCODING)
    target_link_libraries(traversoaudiofileio PUBLIC PkgConfig::FAAC)
endif()

if(WANT_PCH)
    target_precompile_headers(traversoaudiofileio PRIVATE "${CMAKE_SOURCE_DIR}/src/precompile.h")
endif()
