add_subdirectory(plugins)

set(TRAVERSO_COMMANDS_SOURCES
    ArrowKeyBrowser.cpp
    AudioClipExternalProcessing.cpp
    AddRemove.cpp
    ClipSelection.cpp
    CommandGroup.cpp
    Crop.cpp
    ExternalProcessingDialog.cpp
    Fade.cpp
    Gain.cpp
    TGainGroupCommand.cpp
    Import.cpp
    MoveClip.cpp
    MoveCurveNode.cpp
    MoveEdge.cpp
    MoveMarker.cpp
    MovePlugin.cpp
    MoveTrack.cpp
    PCommand.cpp
    SplitClip.cpp
    TrackPan.cpp
    Zoom.cpp
    ArmTracks.cpp
    PlayHeadMove.cpp
    WorkCursorMove.cpp
    RemoveClip.cpp
    TMoveCommand.cpp
    ExternalProcessingDialog.ui
)

add_library(traversocommands STATIC ${TRAVERSO_COMMANDS_SOURCES})

target_include_directories(traversocommands
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/traverso
        ${CMAKE_SOURCE_DIR}/src/sheetcanvas
        ${CMAKE_SOURCE_DIR}/src/plugins
        ${CMAKE_SOURCE_DIR}/src/plugins/native
        ${PROJECT_BINARY_DIR}
        ${PROJECT_BINARY_DIR}/src
)

target_link_libraries(traversocommands
    PUBLIC
        traversocore
)

if(WANT_PCH)
    target_precompile_headers(traversocommands PRIVATE "${CMAKE_SOURCE_DIR}/src/precompile.h")
endif()
