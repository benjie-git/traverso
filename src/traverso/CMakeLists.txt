set(TRAVERSO_GUI_SOURCES
    ${CMAKE_SOURCE_DIR}/src/common/fpu.cc
    Main.cpp
    TMainWindow.cpp
    Traverso.cpp
    TTransport.cpp
    dialogs/settings/Pages.cpp
    dialogs/settings/SettingsDialog.cpp
    dialogs/project/ProjectManagerDialog.cpp
    dialogs/InsertSilenceDialog.cpp
    dialogs/MarkerDialog.cpp
    dialogs/project/NewSheetDialog.cpp
    dialogs/project/NewProjectDialog.cpp
    dialogs/project/OpenProjectDialog.cpp
    dialogs/project/NewTrackDialog.cpp
    dialogs/project/ImportClipsDialog.cpp
    dialogs/RestoreProjectBackupDialog.cpp
    dialogs/PluginSelectorDialog.cpp
    dialogs/ProjectConverterDialog.cpp
    dialogs/ExportDialog.cpp
    dialogs/CDWritingDialog.cpp
    dialogs/ThemeModifierDialog.cpp
    dialogs/TTrackSelector.cpp
    dialogs/TShortcutEditorDialog.cpp
    widgets/BusMonitor.cpp
    widgets/CorrelationMeterWidget.cpp
    widgets/ExportFormatOptionsWidget.cpp
    widgets/InfoWidgets.cpp
    widgets/MessageWidget.cpp
    widgets/MeterWidget.cpp
    widgets/ResourcesWidget.cpp
    widgets/SpectralMeterWidget.cpp
    widgets/TransportConsoleWidget.cpp
    widgets/VUMeter.cpp
    widgets/WelcomeWidget.cpp
    widgets/TSessionTabWidget.cpp
    widgets/TContextHelpWidget.cpp
    ${CMAKE_SOURCE_DIR}/resources/traverso.qrc
)

# Collect all UI files in src/traverso for AUTOUIC
file(GLOB_RECURSE TRAVERSO_UI_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.ui"
)
list(APPEND TRAVERSO_GUI_SOURCES ${TRAVERSO_UI_FILES})

add_executable(traverso ${TRAVERSO_GUI_SOURCES})

if(TAG_VERSION)
    set_target_properties(traverso PROPERTIES OUTPUT_NAME "traverso${TAG_VERSION}")
endif()

target_include_directories(traverso
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/dialogs
        ${CMAKE_CURRENT_SOURCE_DIR}/dialogs/settings
        ${CMAKE_CURRENT_SOURCE_DIR}/dialogs/project
        ${CMAKE_CURRENT_SOURCE_DIR}/widgets
        ${CMAKE_CURRENT_SOURCE_DIR}/ui
        ${PROJECT_BINARY_DIR}
        ${PROJECT_BINARY_DIR}/src
)

target_link_libraries(traverso
    PRIVATE
        traversosheetcanvas
        traversocore
        traversoaudiofileio
        traversoaudiobackend
        traversoplugins
        tcp_traversocommands
        traversocommands
        Qt6::Core
        Qt6::Widgets
        Qt6::Xml
        ${CMAKE_DL_LIBS}
)

if(APPLE)
    target_link_libraries(traverso PRIVATE "-framework CoreFoundation")
endif()

if(WIN32)
    target_link_libraries(traverso PRIVATE winmm)
endif()

if(WANT_PCH)
    target_precompile_headers(traverso PRIVATE "${CMAKE_SOURCE_DIR}/src/precompile.h")
endif()

install(TARGETS traverso
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .
)
