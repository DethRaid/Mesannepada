cmake_minimum_required(VERSION 3.22.1)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(mesannepada
    VERSION 0.13.0
    DESCRIPTION "A game set in the First Dynasty of Ur"
    LANGUAGES CXX
)

# Oh whoops the internet died
execute_process(
        COMMAND ping www.google.com -n 2
        ERROR_QUIET
        RESULT_VARIABLE NO_CONNECTION
)

if(NOT NO_CONNECTION EQUAL 0)
        set(FETCHCONTENT_FULLY_DISCONNECTED ON)
        message(WARNING "FetchContent offline mode: requires an already populated _deps")
else()
        set(FETCHCONTENT_FULLY_DISCONNECTED OFF)
endif()

# Address sanitizer please save me
# set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /fsanitize=address")
# set(CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} /fsanitize=address")

# Set up known folders
set(SAH_TOOLS_DIR "${CMAKE_CURRENT_LIST_DIR}/tools")
set(SAH_DATA_DIR "${CMAKE_CURRENT_LIST_DIR}/data")
set(SAH_OUTPUT_DIR "${CMAKE_BINARY_DIR}/out")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${SAH_OUTPUT_DIR}")

# set output directories for all builds (Debug, Release, etc.)
foreach( OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES} )
    string( TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG )
    set( CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${SAH_OUTPUT_DIR} )
endforeach( OUTPUTCONFIG CMAKE_CONFIGURATION_TYPES )

# Build the application

include(${CMAKE_CURRENT_LIST_DIR}/src/game/mesannepada.cmake)
