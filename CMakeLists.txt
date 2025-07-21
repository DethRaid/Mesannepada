cmake_minimum_required(VERSION 3.22.1)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(mesannepada
    VERSION 0.13.0
    DESCRIPTION "A game set in the First Dynasty of Ur"
    LANGUAGES CXX
)

# Address sanitizer please save me
# set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /fsanitize=address")
# set(CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} /fsanitize=address")

# Set up known folders
# Directory for build tooling like the shaders compile script
set(SAH_TOOLS_DIR "${CMAKE_CURRENT_LIST_DIR}/tools")
# Directory where the game's data is stored
set(SAH_DATA_DIR "${CMAKE_CURRENT_LIST_DIR}/data")
set(SAH_OUTPUT_DIR "${CMAKE_BINARY_DIR}/out")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${SAH_OUTPUT_DIR}")

add_compile_definitions(EASTL_EASTDC_VSNPRINTF=0)

# set output directories for all builds (Debug, Release, etc.)
foreach( OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES} )
    string( TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG )
    set( CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${SAH_OUTPUT_DIR} )
endforeach( OUTPUTCONFIG CMAKE_CONFIGURATION_TYPES )

# Build the application

include(${CMAKE_CURRENT_LIST_DIR}/src/game/mesannepada.cmake)
