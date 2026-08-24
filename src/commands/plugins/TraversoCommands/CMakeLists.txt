set(TRAVERSO_COMMAND_PLUGIN_SOURCES
    TraversoCommands.cpp
)

add_library(tcp_traversocommands STATIC ${TRAVERSO_COMMAND_PLUGIN_SOURCES})

target_compile_definitions(tcp_traversocommands
    PRIVATE
        QT_PLUGIN
        QT_STATICPLUGIN
)

target_include_directories(tcp_traversocommands
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

target_link_libraries(tcp_traversocommands
    PUBLIC
        traversocommands
)

if(WANT_PCH)
    target_precompile_headers(tcp_traversocommands PRIVATE "${CMAKE_SOURCE_DIR}/src/precompile.h")
endif()
