# Download external dependencies

include(FetchContent)

set(VULKAN_DIR "$ENV{VULKAN_SDK}")

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

set(SHADER_OUTPUT_PATH "${SHADER_DIR}" CACHE STRING "")

# Freetype
# link freetype (disable freetype dependencies since we do not need them)
set(CMAKE_DISABLE_FIND_PACKAGE_PNG ON CACHE INTERNAL "")
set(CMAKE_DISABLE_FIND_PACKAGE_BrotliDec ON CACHE INTERNAL "")
set(CMAKE_DISABLE_FIND_PACKAGE_ZLIB ON CACHE INTERNAL "")
set(CMAKE_DISABLE_FIND_PACKAGE_BZip2 ON CACHE INTERNAL "")
set(CMAKE_DISABLE_FIND_PACKAGE_HarfBuzz ON CACHE INTERNAL "")

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/freetype")
add_library(Freetype::Freetype ALIAS freetype)

set(BUILD_DOC OFF CACHE BOOL "" FORCE)
set(BUILD_SANDBOX OFF CACHE BOOL "" FORCE)
set(SKIP_PERFORMANCE_COMPARISON ON CACHE BOOL "" FORCE)
FetchContent_Declare(
        cereal
        GIT_REPOSITORY  https://github.com/USCiLab/cereal.git
        GIT_TAG         a56bad8bbb770ee266e930c95d37fff2a5be7fea
)

FetchContent_Declare(
        eastl
        GIT_REPOSITORY  https://github.com/electronicarts/EASTL.git
        GIT_SHALLOW     ON
        GIT_TAG         3.21.23 
)

set(ENTT_INCLUDE_NATVIS ON CACHE BOOL "" FORCE)
FetchContent_Declare(
        entt
        GIT_REPOSITORY  https://github.com/skypjack/entt.git
        GIT_SHALLOW     ON
        GIT_TAG         v3.15.0 
)

set(FASTGLTF_ENABLE_KHR_PHYSICS_RIGID_BODIES ON CACHE BOOL "" FORCE)
FetchContent_Declare(
        fetch_fastgltf
        GIT_REPOSITORY  https://github.com/spnda/fastgltf.git
        GIT_TAG         v0.9.0
        # GIT_REPOSITORY  https://github.com/DethRaid/fastgltf.git
        # GIT_TAG         308b996e13d765b7b29aff2791612a2c2a6f8b0b
)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE) 
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
if(LINUX)
    set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(
        fetch_glfw
        GIT_REPOSITORY  https://github.com/glfw/glfw.git
        GIT_TAG         e7ea71be039836da3a98cea55ae5569cb5eb885c
)

FetchContent_Declare(
        glm
        GIT_REPOSITORY  https://github.com/g-truc/glm.git
        GIT_SHALLOW     ON
        GIT_TAG         1.0.0
)

FetchContent_Declare(
        fetch_magic_enum
        GIT_REPOSITORY  https://github.com/Neargye/magic_enum.git
        GIT_SHALLOW     ON
        GIT_TAG         v0.9.5
)

set(NRD_EMBEDS_DXIL_SHADERS OFF CACHE BOOL "" FORCE)
set(NRD_EMBEDS_DXBC_SHADERS OFF CACHE BOOL "" FORCE)
set(NRD_NORMAL_ENCODING "4" CACHE STRING "" FORCE)
FetchContent_Declare(
        NRD 
        GIT_REPOSITORY  https://github.com/NVIDIA-RTX/NRD.git
        GIT_TAG         v4.15.0 
)

set(NRI_ENABLE_D3D12_SUPPORT OFF CACHE BOOL "" FORCE)
set(NRI_ENABLE_D3D11_SUPPORT OFF CACHE BOOL "" FORCE)
set(NRI_ENABLE_NGX_SDK ON CACHE BOOL "" FORCE)
set(NRI_ENABLE_FFX_SDK ON CACHE BOOL "" FORCE)
set(NRI_ENABLE_XESS_SDK ON CACHE BOOL "" FORCE)
FetchContent_Declare(
        NRI
        GIT_REPOSITORY  https://github.com/NVIDIA-RTX/NRI.git
        GIT_TAG         v171
)

set(RECASTNAVIGATION_DEMO OFF CACHE BOOL "" FORCE)
set(RECASTNAVIGATION_TESTS OFF CACHE BOOL "" FORCE)
set(RECASTNAVIGATION_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
        recast
        GIT_REPOSITORY https://github.com/recastnavigation/recastnavigation
        GIT_TAG        bd98d84c274ee06842bf51a4088ca82ac71f8c2d
)

set(RMLUI_BACKEND GLFW_VK CACHE STRING "" FORCE)
set(RMLUI_TRACY_PROFILING OFF CACHE BOOL "" FORCE)
set(RMLUI_CUSTOM_RTTI ON CACHE BOOL "" FORCE)
FetchContent_Declare(
        rmlui
        GIT_REPOSITORY  https://github.com/mikke89/RmlUi.git
        GIT_SHALLOW     ON
        GIT_TAG         6.0
)

FetchContent_Declare(
        spdlog
        GIT_REPOSITORY  https://github.com/gabime/spdlog.git
        GIT_TAG         v1.15.3
)

set(SPIRV_REFLECT_EXECUTABLE OFF CACHE BOOL "" FORCE)
set(SPIRV_REFLECT_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SPIRV_REFLECT_STATIC_LIB ON CACHE BOOL "" FORCE)
FetchContent_Declare(
        fetch_spirv_reflect
        GIT_REPOSITORY  https://github.com/KhronosGroup/SPIRV-Reflect.git
        GIT_SHALLOW     ON
        GIT_TAG         vulkan-sdk-1.4.304.0
)

FetchContent_Declare(
        toml11
        GIT_REPOSITORY https://github.com/ToruNiina/toml11.git
        GIT_SHALLOW     ON
        GIT_TAG        v4.4.0
)

FetchContent_Declare(
        fetch_tracy
        GIT_REPOSITORY  https://github.com/wolfpld/tracy.git
        GIT_SHALLOW     ON
        GIT_TAG         v0.11.1
)

FetchContent_Declare(
        utf8cpp
        GIT_REPOSITORY https://github.com/nemtrif/utfcpp.git
        GIT_SHALLOW    ON
        GIT_TAG        v4.0.6
)

FetchContent_Declare(
        fetch_volk
        GIT_REPOSITORY  https://github.com/zeux/volk.git
        GIT_TAG         02ffc36f3c7f8a744e741127eda7ccbdb5c622e6
)

FetchContent_Declare(
        vk-bootstrap
        GIT_REPOSITORY  https://github.com/charles-lunarg/vk-bootstrap.git
        GIT_TAG         c73b793387e739b9abdf952c52cde40d1b0e7db0
        #GIT_REPOSITORY  https://github.com/DethRaid/vk-bootstrap.git
        #GIT_TAG         vk_14_features
)

set(VMA_STATIC_VULKAN_FUNCTIONS OFF CACHE BOOL "" FORCE)
set(VMA_DYNAMIC_VULKAN_FUNCTIONS ON CACHE BOOL "" FORCE)
FetchContent_Declare(
        fetch_vma
        GIT_REPOSITORY  https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_SHALLOW     ON
        GIT_TAG         v3.3.0
)

# Jolt options
set(DOUBLE_PRECISION OFF)
set(GENERATE_DEBUG_SYMBOLS ON)
set(OVERRIDE_CXX_FLAGS OFF)
set(CROSS_PLATFORM_DETERMINISTIC OFF)
set(INTERPROCEDURAL_OPTIMIZATION ON)
set(CPP_EXCEPTIONS_ENABLED OFF)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
set(OBJECT_LAYER_BITS 16)

FetchContent_Declare(
        JoltPhysics
        GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics
        GIT_TAG v5.3.0
	SOURCE_SUBDIR Build
)

FetchContent_MakeAvailable(
        cereal
        eastl
        entt
        fetch_fastgltf
        fetch_glfw
        glm
        JoltPhysics
        fetch_magic_enum
        NRD
        NRI
        recast
        rmlui
        spdlog
        fetch_spirv_reflect
        toml11
        fetch_tracy
        fetch_vma
        vk-bootstrap
        utf8cpp
        fetch_volk)

if(ANDROID)
        FetchContent_Declare(
                libadrenotools
                GIT_REPOSITORY  https://github.com/bylaws/libadrenotools.git
                GIT_TAG         8fae8ce254dfc1344527e05301e43f37dea2df80
        )
        FetchContent_MakeAvailable(libadrenotools)
endif()

if(SAH_USE_STREAMLINE)
    set(STREAMLINE_FEATURE_DLSS_SR ON CACHE BOOL "" FORCE)
    set(STREAMLINE_FEATURE_DLSS_RR ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
            streamline
            DOWNLOAD_EXTRACT_TIMESTAMP OFF
            URL     https://github.com/NVIDIA-RTX/Streamline/releases/download/v2.8.0/streamline-sdk-v2.8.0.zip
    )
    FetchContent_MakeAvailable(streamline)
endif()

if(SAH_USE_FFX)
    set(CAULDRON_VK ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
            fidelityfx
            DOWNLOAD_EXTRACT_TIMESTAMP OFF
            URL             https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases/download/v1.1.4/FidelityFX-SDK-v1.1.4.zip
    )
    FetchContent_GetProperties(fidelityfx)
    if(fidelityfx_POPULATED)
        message("fidelityfx automatically populated")
    else()
        FetchContent_Populate(fidelityfx)

        add_library(ffx_api INTERFACE)
        target_include_directories(ffx_api INTERFACE
                "${fidelityfx_SOURCE_DIR}/ffx-api/include"
                )
        target_link_directories(ffx_api INTERFACE
            ${fidelityfx_SOURCE_DIR}/PrebuiltSignedDLL
        )
        target_link_libraries(ffx_api INTERFACE
            amd_fidelityfx_vk)

        add_library(fidelityfx INTERFACE)
        target_include_directories(fidelityfx INTERFACE
                "${fidelityfx_SOURCE_DIR}/sdk/include"
                )
        target_link_directories(fidelityfx INTERFACE
                "${fidelityfx_SOURCE_DIR}/sdk/bin/ffx_sdk"
                )
        target_link_libraries(fidelityfx INTERFACE
            ffx_backend_vk_x64d
            ffx_cacao_x64d
            ffx_fsr3_x64d
        )
    endif()
endif()

if(SAH_USE_XESS)
        FetchContent_Declare(
                xess
                GIT_REPOSITORY  https://github.com/intel/xess.git     
                GIT_TAG         v2.1.0
        )
        FetchContent_GetProperties(xess)
        if(xess_POPULATED)
                message("XeSS automatically populated")
        else()
            FetchContent_MakeAvailable(xess)

                add_library(xess INTERFACE)
                target_include_directories(xess INTERFACE 
                        ${xess_SOURCE_DIR}/inc
                )
                target_link_directories(xess INTERFACE
                        ${xess_SOURCE_DIR}/lib
                )
                target_link_libraries(xess INTERFACE 
                        libxess
                )
        endif()
endif()

FetchContent_Declare(
        fetch_imgui
        GIT_REPOSITORY  https://github.com/ocornut/imgui.git
        GIT_TAG         v1.91.9b
)
FetchContent_GetProperties(fetch_imgui)
if(fetch_imgui_POPULATED)
    message("Dear ImGUI automatically populated")
else()
    FetchContent_MakeAvailable(fetch_imgui)
    add_library(imgui STATIC
            ${fetch_imgui_SOURCE_DIR}/imgui.cpp
            ${fetch_imgui_SOURCE_DIR}/imgui_demo.cpp
            ${fetch_imgui_SOURCE_DIR}/imgui_draw.cpp
            ${fetch_imgui_SOURCE_DIR}/imgui_tables.cpp
            ${fetch_imgui_SOURCE_DIR}/imgui_widgets.cpp
            ${fetch_imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
            ${fetch_imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
            )
    target_include_directories(imgui PUBLIC
            ${fetch_imgui_SOURCE_DIR}
            ${fetch_imgui_SOURCE_DIR}/misc/cpp
            ${fetch_imgui_SOURCE_DIR}/backends
            )
    target_link_libraries(imgui PUBLIC
        glfw)
endif()

FetchContent_Declare(
        imguizmo
        GIT_REPOSITORY  https://github.com/CedricGuillemet/ImGuizmo.git
        GIT_TAG         2310acda820d7383d4c4884b7945ada92cd16a47
)
FetchContent_GetProperties(imguizmo)
if(imguizmo_POPULATED)
    message("ImGuizmo automatically populated")
else()
    FetchContent_MakeAvailable(imguizmo)
    add_library(imguizmo STATIC 
        ${imguizmo_SOURCE_DIR}/GraphEditor.cpp
        ${imguizmo_SOURCE_DIR}/ImCurveEdit.cpp
        ${imguizmo_SOURCE_DIR}/ImGradient.cpp
        ${imguizmo_SOURCE_DIR}/ImGuizmo.cpp
        ${imguizmo_SOURCE_DIR}/ImSequencer.cpp
        )
    target_include_directories(imguizmo PUBLIC 
        ${imguizmo_SOURCE_DIR}
        )
    target_link_libraries(imguizmo PUBLIC 
        imgui
        )
endif()

FetchContent_Declare(
        plf_colony
        GIT_REPOSITORY  https://github.com/mattreecebentley/plf_colony.git
        GIT_TAG         5aeb1e6dbc7686f9f09eef685e57dc6acfe616c7
)
FetchContent_GetProperties(plf_colony)
if(plf_colony_POPULATED)
    message("plf::colony automatically populated")
else()
    FetchContent_MakeAvailable(plf_colony)
    add_library(plf_colony INTERFACE)
    target_include_directories(plf_colony INTERFACE
            ${plf_colony_SOURCE_DIR}
            )
endif()

FetchContent_Declare(
        fetch_stb
        GIT_REPOSITORY  https://github.com/nothings/stb.git
        GIT_TAG         8b5f1f37b5b75829fc72d38e7b5d4bcbf8a26d55
)
FetchContent_GetProperties(fetch_stb)
if(fetch_stb_POPULATED)
    message("STB automatically populated")
else()
    FetchContent_MakeAvailable(fetch_stb)
    add_library(stb INTERFACE)
    target_include_directories(stb INTERFACE ${fetch_stb_SOURCE_DIR})
endif()

set(SAH_EXTERNAL_NATVIS_FILES
        "${FETCHCONTENT_BASE_DIR}/eastl-src/doc/EASTL.natvis"
        "${FETCHCONTENT_BASE_DIR}/entt-src/natvis/entt/signal.natvis"
        "${FETCHCONTENT_BASE_DIR}/entt-src/natvis/entt/resource.natvis"
        "${FETCHCONTENT_BASE_DIR}/entt-src/natvis/entt/process.natvis"
        "${FETCHCONTENT_BASE_DIR}/entt-src/natvis/entt/poly.natvis"
        "${FETCHCONTENT_BASE_DIR}/entt-src/natvis/entt/meta.natvis"
        "${FETCHCONTENT_BASE_DIR}/entt-src/natvis/entt/locator.natvis"
        "${FETCHCONTENT_BASE_DIR}/entt-src/natvis/entt/graph.natvis"
        "${FETCHCONTENT_BASE_DIR}/entt-src/natvis/entt/entity.natvis"
        "${FETCHCONTENT_BASE_DIR}/entt-src/natvis/entt/core.natvis"
        "${FETCHCONTENT_BASE_DIR}/entt-src/natvis/entt/container.natvis"
        "${FETCHCONTENT_BASE_DIR}/entt-src/natvis/entt/config.natvis"
        "${FETCHCONTENT_BASE_DIR}/glm-src/util/glm.natvis"
        "${FETCHCONTENT_BASE_DIR}/joltphysics-src/Jolt/Jolt.natvis"
        "${FETCHCONTENT_BASE_DIR}/fetch_vma-src/src/vk_mem_alloc.natvis"
        "${FETCHCONTENT_BASE_DIR}/fetch_imgui-src/mics/debuggers/imgui.natvis"
)
