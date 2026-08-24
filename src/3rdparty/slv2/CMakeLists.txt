set(TRAVERSO_SLV2_SOURCES
    src/collections.c
    src/plugin.c
    src/pluginclass.c
    src/plugininstance.c
    src/plugins.c
    src/pluginui.c
    src/pluginuiinstance.c
    src/port.c
    src/query.c
    src/scalepoint.c
    src/util.c
    src/value.c
    src/world.c
)

add_library(traversoslv2 STATIC ${TRAVERSO_SLV2_SOURCES})

target_compile_features(traversoslv2 PRIVATE c_std_99)

target_include_directories(traversoslv2
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/slv2
    PRIVATE
        ${PROJECT_BINARY_DIR}
        ${PROJECT_BINARY_DIR}/src
)
