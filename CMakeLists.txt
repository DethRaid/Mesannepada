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

# Whether we're making a packaged build for distribution, or a development build for... developing. This affects  afew things:
# In a development build, we load data from the data/ folder in the root of the repo. This folder gets committed to git
# In a packaged build, we load data from the data/ folder next to the executable
# In both builds, we put compiled shaders in the shaders/ folder next to the executable
# Packaged build also (will eventually) run a texture compression and mesh optimization step, maybe, if I get around to it
option(SAH_PACKAGED_BUILD "Whether to make a packaged build for distribution" OFF)

# Set up known folders

# Directory for build tooling like the shaders compile script
set(SAH_TOOLS_DIR "${CMAKE_CURRENT_LIST_DIR}/tools")

# WHere to find data to build
set(SAH_INPUT_DIR "${CMAKE_CURRENT_LIST_DIR}/data")

# Where to output built data, e.g. shaders and compressed textures
set(SAH_OUTPUT_DIR "${CMAKE_BINARY_DIR}/out")

if(SAH_PACKAGED_BUILD)
    # Directory to load data from
    set(SAH_DATA_DIR "${SAH_OUTPUT_DIR}/data")

else()
    # Directory to load data from
    set(SAH_DATA_DIR "${CMAKE_CURRENT_LIST_DIR}/data")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${SAH_OUTPUT_DIR}")

add_compile_definitions(EASTL_EASTDC_VSNPRINTF=0)

# set output directories for all builds (Debug, Release, etc.)
foreach( OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES} )
    string( TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG )
    set( CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${SAH_OUTPUT_DIR} )
endforeach( OUTPUTCONFIG CMAKE_CONFIGURATION_TYPES )

# Build the application

include(${CMAKE_CURRENT_LIST_DIR}/src/game/mesannepada.cmake)
