cmake_minimum_required(VERSION 3.16)
project(VoscOperatorUi LANGUAGES CXX)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory("${RMLUI_SOURCE}" rmlui EXCLUDE_FROM_ALL)

add_library(vosc_rmlui_gl3 STATIC "${RMLUI_SOURCE}/Backends/RmlUi_Renderer_GL3.cpp")
target_link_libraries(vosc_rmlui_gl3 PRIVATE RmlUi::Core)
target_compile_definitions(vosc_rmlui_gl3 PRIVATE RMLUI_NUM_MSAA_SAMPLES=0)

set_target_properties(rmlui_core vosc_rmlui_gl3 PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${RMLUI_ARCHIVES}")
