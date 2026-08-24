set(TRAVERSO_SONGCANVAS_SOURCES
    AudioClipView.cpp
    AudioTrackView.cpp
    ClipsViewPort.cpp
    Cursors.cpp
    CurveNodeView.cpp
    CurveView.cpp
    FadeCurveView.cpp
    MarkerView.cpp
    LineView.cpp
    PluginChainView.cpp
    PluginView.cpp
    PositionIndicator.cpp
    TrackView.cpp
    TTrackLaneView.cpp
    SheetView.cpp
    SheetWidget.cpp
    TBusTrackView.cpp
    TimeLineView.cpp
    TimeLineViewPort.cpp
    TrackPanelView.cpp
    TrackPanelViewPort.cpp
    ViewItem.cpp
    ViewPort.cpp
    dialogs/AudioClipEditDialog.cpp
    dialogs/TTrackManagerDialog.cpp
    dialogs/AudioClipEditDialog.ui
    dialogs/TTrackManagerDialog.ui
    VUMeterView.cpp
    TCanvasCursor.cpp
    TKnobView.cpp
    TTextView.cpp
)

add_library(traversosheetcanvas STATIC ${TRAVERSO_SONGCANVAS_SOURCES})

target_include_directories(traversosheetcanvas
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/dialogs
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/traverso
        ${CMAKE_SOURCE_DIR}/src/traverso/dialogs
        ${CMAKE_SOURCE_DIR}/src/plugins
        ${CMAKE_SOURCE_DIR}/src/plugins/native
        ${CMAKE_SOURCE_DIR}/src/plugins/LV2
        ${PROJECT_BINARY_DIR}
        ${PROJECT_BINARY_DIR}/src
)

target_link_libraries(traversosheetcanvas
    PUBLIC
        traversocore
        traversocommands
        traversoplugins
)

if(WANT_PCH)
    target_precompile_headers(traversosheetcanvas PRIVATE "${CMAKE_SOURCE_DIR}/src/precompile.h")
endif()
