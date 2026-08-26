set(TRAVERSO_CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/common/Utils.cpp
    ${CMAKE_SOURCE_DIR}/src/common/Tsar.cpp
    ${CMAKE_SOURCE_DIR}/src/common/Debugger.cpp
    ${CMAKE_SOURCE_DIR}/src/common/Mixer.cpp
    ${CMAKE_SOURCE_DIR}/src/common/RingBuffer.cpp
    ${CMAKE_SOURCE_DIR}/src/common/Resampler.cpp
    AudioClip.cpp
    AudioClipGroup.cpp
    AudioClipManager.cpp
    AudioFileCopyConvert.cpp
    AudioFileMerger.cpp
    AudioTrack.cpp
    AudioSource.cpp
    AbstractViewPort.cpp
    TCommand.cpp
    TConfig.cpp
    TShortcutManager.cpp
    ContextPointer.cpp
    ContextItem.cpp
    Curve.cpp
    CurveNode.cpp
    CommandPlugin.cpp
    DiskIO.cpp
    Export.cpp
    FadeCurve.cpp
    FileHelpers.cpp
    Information.cpp
    TInputEventDispatcher.cpp
    Peak.cpp
    Project.cpp
    ProjectManager.cpp
    TAudioProcessingNode.cpp
    ReadSource.cpp
    ResourcesManager.cpp
    TBusTrack.cpp
    TSend.cpp
    TSession.cpp
    Sheet.cpp
    Track.cpp
    WriteSource.cpp
    gdither.cpp
    SnapList.cpp
    Snappable.cpp
    TimeLine.cpp
    Marker.cpp
    Themer.cpp
    ProjectConverter.cpp
    TGlobalContext.cpp
)

if(ENABLE_SSE_OPT)
    if(IS_ARCH_X86)
        list(APPEND TRAVERSO_CORE_SOURCES ${CMAKE_SOURCE_DIR}/src/common/sse_functions.S)
    elseif(IS_ARCH_X86_64)
        if(APPLE)
            list(APPEND TRAVERSO_CORE_SOURCES ${CMAKE_SOURCE_DIR}/src/common/sse_functions_64bit_mac.S)
        else()
            list(APPEND TRAVERSO_CORE_SOURCES ${CMAKE_SOURCE_DIR}/src/common/sse_functions_64bit.S)
        endif()
    endif()
endif()

add_library(traversocore STATIC ${TRAVERSO_CORE_SOURCES})

target_include_directories(traversocore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/common
        ${PROJECT_BINARY_DIR}
        ${PROJECT_BINARY_DIR}/src
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/engine
        ${CMAKE_SOURCE_DIR}/src/audiofileio
        ${CMAKE_SOURCE_DIR}/src/audiofileio/decode
        ${CMAKE_SOURCE_DIR}/src/audiofileio/encode
        ${CMAKE_SOURCE_DIR}/src/commands
        ${CMAKE_SOURCE_DIR}/src/plugins
        ${CMAKE_SOURCE_DIR}/src/plugins/native
)

target_link_libraries(traversocore
    PUBLIC
        traversoaudiobackend
        traversoaudiofileio
        Qt6::Core
        Qt6::Widgets
        Qt6::Xml
        PkgConfig::FFTW3
        PkgConfig::SAMPLERATE
)

if(APPLE AND WANT_VECLIB_OPTIMIZATIONS)
    target_link_libraries(traversocore PRIVATE "-framework Accelerate")
endif()

if(WANT_PCH)
    target_precompile_headers(traversocore PRIVATE "${CMAKE_SOURCE_DIR}/src/precompile.h")
endif()
