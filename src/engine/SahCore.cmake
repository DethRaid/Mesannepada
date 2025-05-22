cmake_minimum_required(VERSION 3.26.1)

project(mesannepada)

# These must be 1 or 0 because of CMake skill issue
option(SAH_USE_FFX "Whether to use AMD's FidelityFX library" 1)
option(SAH_USE_XESS "Whether to use Intel's XeSS library" 1)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(WIN32)
    set(USE_STREAMLINE_DEFAULT 1)
elseif(Linux)
    set(USE_STREAMLINE_DEFAULT 0)
endif()

option(SAH_USE_STREAMLINE "Whether to use Nvidia's Streamline library" ${USE_STREAMLINE_DEFAULT})

# Shaders

set(SHADER_DIR ${CMAKE_CURRENT_LIST_DIR}/shaders)

# SAH Core

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/*.cpp ${CMAKE_CURRENT_LIST_DIR}/*.hpp)

add_library(SahCore STATIC ${SOURCES})

# External code
# Must be after we set CMAKE_RUNTIME_OUTPUT_DIRECTORY, because Streamline needs that variable
set(EXTERN_DIR ${CMAKE_CURRENT_LIST_DIR}/extern)
include(${EXTERN_DIR}/extern.cmake)

target_compile_definitions(SahCore PUBLIC
        VK_NO_PROTOTYPES
        GLFW_INCLUDE_NONE
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_ENABLE_EXPERIMENTAL
        _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING
        TRACY_ENABLE
        SAH_USE_FFX=${SAH_USE_FFX}
        SAH_USE_STREAMLINE=${SAH_USE_STREAMLINE}
        SAH_USE_XESS=${SAH_USE_XESS}
        UTF_CPP_CPLUSPLUS=202002
        EASTL_EASTDC_VSNPRINTF=0
        SAH_USE_IRRADIANCE_CACHE=0
        )

if(WIN32)
        target_compile_definitions(SahCore PUBLIC
                VK_USE_PLATFORM_WIN32_KHR
                NOMINMAX
                )
elseif(Linux)
    target_compile_definitions(SahCore PUBLIC
        VK_USE_PLATFORM_WAYLAND
    )
endif()

message(STATUS "Vulkan SDK: $ENV{VULKAN_SDK}")

target_include_directories(SahCore PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
        )
target_include_directories(SahCore SYSTEM PUBLIC
        "$ENV{VULKAN_SDK}/include"
        ${JoltPhysics_SOURCE_DIR}/..
        )

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-format-security")
target_link_libraries(SahCore PUBLIC
        EASTL
        EnTT::EnTT
        fastgltf::fastgltf
        freetype
        glm::glm-header-only
        glfw
        GPUOpen::VulkanMemoryAllocator
        imgui
        Jolt
        KTX::ktx
        magic_enum::magic_enum
        plf_colony
        renderdoc
        slang
        spdlog::spdlog
        spirv-reflect-static
        stb
        Tracy::TracyClient
        RmlUi::RmlUi
        toml11::toml11
        utf8cpp
        vk-bootstrap
        volk::volk_headers
        )

if(WIN32)
    target_compile_options(SahCore PUBLIC 
        "/MP"
    )
elseif(LINUX)
    target_compile_options(SahCore PUBLIC
        "-fms-extensions"
        "-Wno-nullability-completeness")
endif()

if(SAH_USE_FFX)
    target_link_libraries(SahCore PUBLIC
            ffx_api
            fidelityfx
        )

    add_custom_command(TARGET SahCore POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${fidelityfx_SOURCE_DIR}/PrebuiltSignedDLL/amd_fidelityfx_vk.dll"
        ${SAH_OUTPUT_DIR})
    add_custom_command(TARGET SahCore POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${fidelityfx_SOURCE_DIR}/sdk/bin/ffx_sdk/ffx_cacao_x64d.dll"
        ${SAH_OUTPUT_DIR})
    add_custom_command(TARGET SahCore POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${fidelityfx_SOURCE_DIR}/sdk/bin/ffx_sdk/ffx_fsr3_x64d.dll"
        ${SAH_OUTPUT_DIR})
endif()
if(WIN32 AND SAH_USE_STREAMLINE)
    message(STATUS "Including Streamline")
    target_link_libraries(SahCore PUBLIC
            streamline
    )
endif()
if(SAH_USE_XESS)
    target_link_libraries(SahCore PUBLIC
        xess
    )
    add_custom_command(TARGET SahCore POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${xess_SOURCE_DIR}/bin/libxess.dll"
            ${SAH_OUTPUT_DIR})
endif()

#######################
# Generate VS filters #
#######################
foreach(source IN LISTS SOURCES)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "${CMAKE_CURRENT_LIST_DIR}/" "" source_path_relative "${source_path}")
    string(REPLACE "/" "\\" source_path_msvc "${source_path_relative}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()

