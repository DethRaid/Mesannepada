cmake_minimum_required(VERSION 3.22.1)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SAH_USE_FFX_CACAO ON CACHE BOOL "" FORCE)

project(mesannepada)

# Include the shared renderer

set(SAH_USE_FFX 1 CACHE BOOL "" FORCE)
set(SAH_USE_STREAMLINE 1 CACHE BOOL "" FORCE)
set(SAH_USE_XESS 1 CACHE BOOL "" FORCE)
include(${CMAKE_CURRENT_LIST_DIR}/../engine/SahCore.cmake)

# Win32 dependencies

include(${CMAKE_CURRENT_LIST_DIR}/extern/extern.cmake)

# This thing

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../cmake;")

set(SAH_SHADER_DIR ${CMAKE_CURRENT_LIST_DIR}/../engine/shaders)

file(GLOB_RECURSE SHADERS CONFIGURE_DEPENDS
        ${SAH_SHADER_DIR}/*.vert
        ${SAH_SHADER_DIR}/*.geom
        ${SAH_SHADER_DIR}/*.frag
        ${SAH_SHADER_DIR}/*.comp
        )

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS 
    ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp 
    ${CMAKE_CURRENT_LIST_DIR}/src/*.hpp
    )

add_executable(mesannepada ${SOURCES})
target_include_directories(mesannepada PUBLIC 
    "${CMAKE_CURRENT_LIST_DIR}/src"
    )

set(SHADER_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/../engine)

set(EXTERN_DIR ${SHADER_INCLUDE_DIR}/extern)

add_custom_target(compile_shaders
    python ${SAH_TOOLS_DIR}/compile_shaders.py ${SAH_SHADER_DIR} ${SAH_OUTPUT_DIR}/shaders ${EXTERN_DIR}
)
add_dependencies(mesannepada compile_shaders)

target_link_libraries(mesannepada PUBLIC
        SahCore
)

# Copy game data
# TODO: Advanced build step using gltfpack or something to compress glTF files
add_custom_command(TARGET mesannepada POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
    ${SAH_DATA_DIR}
    ${SAH_OUTPUT_DIR}/data
    )
  
#######################
# Generate VS filters #
#######################
foreach(source IN LISTS SOURCES)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "${CMAKE_CURRENT_LIST_DIR}/" "" source_path_relative "${source_path}")
    string(REPLACE "/" "\\" source_path_msvc "${source_path_relative}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()

set_property(DIRECTORY ${CMAKE_PROJECT_DIR} PROPERTY VS_STARTUP_PROJECT mesannepada)
set_property(TARGET mesannepada PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${SAH_OUTPUT_DIR}")
