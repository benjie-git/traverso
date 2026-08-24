set(TRAVERSO_PLUGINS_SOURCES
    PluginChain.cpp
    Plugin.cpp
    PluginManager.cpp
    PluginSlider.cpp
    native/CorrelationMeter.cpp
    native/SpectralMeter.cpp
    native/GainEnvelope.cpp
    PluginPropertiesDialog.cpp
)

if(HAVE_LILV)
    list(APPEND TRAVERSO_PLUGINS_SOURCES LV2/LV2Plugin.cpp)
endif()

add_library(traversoplugins STATIC ${TRAVERSO_PLUGINS_SOURCES})

target_include_directories(traversoplugins
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/native
        ${CMAKE_CURRENT_SOURCE_DIR}/LV2
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/commands
        ${CMAKE_SOURCE_DIR}/src/sheetcanvas
        ${CMAKE_SOURCE_DIR}/src/traverso
        ${PROJECT_BINARY_DIR}
        ${PROJECT_BINARY_DIR}/src
)

target_link_libraries(traversoplugins
    PUBLIC
        traversocore
)

if(HAVE_LILV)
    target_link_libraries(traversoplugins PUBLIC PkgConfig::LIBLILV)
endif()

if(WANT_PCH)
    target_precompile_headers(traversoplugins PRIVATE "${CMAKE_SOURCE_DIR}/src/precompile.h")
endif()
