#include "render_backend.hpp"

#include <stdexcept>

#include <glm/common.hpp>
#include <spdlog/spdlog.h>
#include <tracy/Tracy.hpp>
#include <vulkan/vk_enum_string_helper.h>

#include "console/cvars.hpp"
#include "core/issue_breakpoint.hpp"
#include "core/system_interface.hpp"
#include "render/backend/blas_build_queue.hpp"
#include "render/backend/p_next_chain.hpp"
#include "render/backend/pipeline_cache.hpp"
#include "render/backend/resource_upload_queue.hpp"
#include "render/backend/rhi_globals.hpp"
#include "render/upscaling/xess.hpp"
#include "render_graph.hpp"
#include "shared/prelude.h"

namespace render {
    [[maybe_unused]] static auto cvar_use_dgc = AutoCVar_Int{
        "r.RHI.DGC.Enable",
        "Whether to use Device-Generated Commands when available. Reduces CPU load, but is not supported on all "
        "hardware. We currently use VK_NV_device_generated_commands, will switch to EXT when it reaches my GPU",
        0 // Keep this off until we have material functions working
    };

    static std::shared_ptr<spdlog::logger> logger;

    void RenderBackend::construct_singleton() {
        if(g_render_backend == nullptr) {
            g_render_backend = eastl::make_unique<RenderBackend>();
        }
    }

    RenderBackend& RenderBackend::get() {
        return *g_render_backend;
    }

    /**
     * Android driver replacement - possibly useful?
     */
    [[maybe_unused]]
    static void* replace_driver(const std::string& path, const char* hooksDir, const char* driverName) {
        void* lib_vulkan = nullptr;

        return lib_vulkan;
    }

    RenderBackend::RenderBackend() : resource_access_synchronizer{*this} {
        ZoneScoped;

        logger = SystemInterface::get().get_logger("RenderBackend");

        VkResult volk_result = VK_SUCCESS;
#if SAH_USE_STREAMLINE
        const PFN_vkGetInstanceProcAddr vk_get_instance_proc = streamline.try_load_interposer();
        if(vk_get_instance_proc != nullptr) {
            volkInitializeCustom(vk_get_instance_proc);
        } else {
            logger->warn("Could not load Streamline Vulkan loader");
            volk_result = volkInitialize();
        }

#else
        volk_result = volkInitialize();
#endif

        if(volk_result != VK_SUCCESS) {
            throw std::runtime_error{
                fmt::format("Could not initialize Volk, Vulkan is not available ({})", string_VkResult(volk_result))};
        }

        supports_raytracing = *CVarSystem::Get()->GetIntCVar("r.Raytracing.Enable") != 0;

        create_instance();
    }

    void RenderBackend::add_transfer_barrier(const VkImageMemoryBarrier2& barrier) {
        transfer_barriers.push_back(barrier);
    }

    void RenderBackend::create_instance() {
        ZoneScoped;

        // vkb enables the surface extensions for us
        auto instance_builder = vkb::InstanceBuilder{vkGetInstanceProcAddr}
                                    .set_app_name("𒈩𒀭𒉌𒅆𒊒𒁕")
                                    .set_engine_name("𒊓𒊏")
                                    .set_app_version(0, 13, 0)
                                    .require_api_version(1, 4, 0)
                                    .enable_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

#if SAH_USE_XESS
        const auto xess_extensions = XeSSAdapter::get_instance_extensions();
        for(const auto& extension : xess_extensions) {
            instance_builder.enable_extension(extension.c_str());
        }
#endif

        {
            ZoneScopedN("vkCreateInstance");
            auto instance_ret = instance_builder.build();
            if(!instance_ret) {
                const auto error_message = fmt::format("Could not initialize Vulkan: {} (VK_RESULT {})",
                                                       instance_ret.error().message(),
                                                       string_VkResult(instance_ret.vk_result()));
                throw std::runtime_error{error_message};
            }
            instance = instance_ret.value();
        }
        volkLoadInstance(instance.instance);
    }

    void RenderBackend::create_device(VkSurfaceKHR surface) {
        ZoneScoped;

        constexpr auto required_features = VkPhysicalDeviceFeatures{
            .imageCubeArray = VK_TRUE,
            .geometryShader = VK_TRUE,
            .sampleRateShading = VK_TRUE,
            .multiDrawIndirect = VK_TRUE,
            .drawIndirectFirstInstance = VK_TRUE,
            .depthClamp = VK_TRUE,
            .samplerAnisotropy = VK_TRUE,
            .textureCompressionBC = VK_TRUE,
            .vertexPipelineStoresAndAtomics = VK_TRUE,
            .fragmentStoresAndAtomics = VK_TRUE,
            .shaderImageGatherExtended = VK_TRUE,
            .shaderSampledImageArrayDynamicIndexing = VK_TRUE,
            .shaderStorageBufferArrayDynamicIndexing = VK_TRUE,
            .shaderInt64 = VK_TRUE,
            .shaderInt16 = VK_TRUE,
        };

        auto required_1_1_features = VkPhysicalDeviceVulkan11Features{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
            .storageBuffer16BitAccess = VK_TRUE,
            .uniformAndStorageBuffer16BitAccess = VK_TRUE,
            .storagePushConstant16 = VK_TRUE,
            .multiview = VK_TRUE,
            .variablePointersStorageBuffer = VK_TRUE,
            .variablePointers = VK_TRUE,
            .shaderDrawParameters = VK_TRUE,
        };

        auto required_1_2_features = VkPhysicalDeviceVulkan12Features{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
            .drawIndirectCount = VK_TRUE,
            .shaderFloat16 = VK_TRUE,
            .shaderInt8 = VK_TRUE,
            .descriptorIndexing = VK_TRUE,
            .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
            .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
            .descriptorBindingPartiallyBound = VK_TRUE,
            .descriptorBindingVariableDescriptorCount = VK_TRUE,
            .runtimeDescriptorArray = VK_TRUE,
            .samplerFilterMinmax = VK_TRUE,
            .scalarBlockLayout = VK_TRUE,
            .imagelessFramebuffer = VK_TRUE,
            .shaderSubgroupExtendedTypes = VK_TRUE,
            .bufferDeviceAddress = VK_TRUE,
            .shaderOutputLayer = VK_TRUE,
        };

        auto required_1_3_features = VkPhysicalDeviceVulkan13Features{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
            .synchronization2 = VK_TRUE,
            .dynamicRendering = VK_TRUE,
            .shaderIntegerDotProduct = VK_TRUE,
            .maintenance4 = VK_TRUE,
        };

        auto required_1_4_features = VkPhysicalDeviceVulkan14Features{
            .maintenance5 = VK_TRUE,
        };

        auto phys_device_builder =
            vkb::PhysicalDeviceSelector{instance}
                .set_surface(surface)
                .add_required_extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)
                .add_required_extension(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME) // FFX needs this
                .set_required_features(required_features)
                .set_required_features_11(required_1_1_features)
                .set_required_features_12(required_1_2_features)
                .set_required_features_13(required_1_3_features)
                //.set_required_features_14(required_1_4_features)
                .set_minimum_version(1, 3);

        auto phys_device_ret = phys_device_builder.select();
        if(!phys_device_ret) {
            const auto error_message = fmt::format("Could not select device: {}", phys_device_ret.error().message());
            throw std::runtime_error{error_message};
        }
        physical_device = phys_device_ret.value();

        logger->info("Selected device {}", physical_device.name);

        if(cvar_use_dgc.get()) {
            supports_dgc = physical_device.enable_extension_if_present(VK_EXT_DEVICE_GENERATED_COMMANDS_EXTENSION_NAME);
            if(supports_dgc) {
                logger->info("Device Generated Commands is supported!");
            }
        }

        supports_rt = physical_device.enable_extension_if_present(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        supports_rt &= physical_device.enable_extension_if_present(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
        supports_rt &= physical_device.enable_extension_if_present(VK_KHR_RAY_TRACING_MAINTENANCE_1_EXTENSION_NAME);
        supports_rt &= physical_device.enable_extension_if_present(VK_KHR_RAY_QUERY_EXTENSION_NAME);
        supports_rt &= physical_device.enable_extension_if_present(VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME);
        supports_rt &= physical_device.enable_extension_if_present(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);

        auto supports_rt_validation = false;
        if(supports_rt) {
            supports_rt_validation =
                physical_device.enable_extension_if_present(VK_NV_RAY_TRACING_VALIDATION_EXTENSION_NAME);
            logger->info("Device {} supports ray tracing", physical_device.name);
        } else {
            logger->warn("Device {} does not support ray tracing!", physical_device.name);
        }

        physical_device.enable_extension_if_present(VK_NV_DEVICE_GENERATED_COMMANDS_EXTENSION_NAME);
        physical_device.enable_extension_if_present(VK_NV_DEVICE_GENERATED_COMMANDS_COMPUTE_EXTENSION_NAME);

        supports_nv_shader_reorder =
            physical_device.enable_extension_if_present(VK_NV_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME);

        physical_device.enable_extension_if_present(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);
        supports_nv_diagnostics_config =
            physical_device.enable_extension_if_present(VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME);

        supports_shading_rate_image =
            physical_device.enable_extension_if_present(VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME);
        if(supports_shading_rate_image) {
            logger->debug("{} is supported", VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME);
        }

        physical_device.enable_extension_if_present(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);

#if SAH_USE_XESS
        const auto xess_device_extensions = XeSSAdapter::get_device_extensions(instance, physical_device);
        for(const auto& extension : xess_device_extensions) {
            physical_device.enable_extension_if_present(extension.c_str());
        }
#endif

        query_physical_device_features();

        query_physical_device_properties();

        auto device_builder = vkb::DeviceBuilder{physical_device};

#if SAH_USE_XESS
        auto xess_features = VkPhysicalDeviceFeatures2{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        XeSSAdapter::add_required_features(instance, physical_device, xess_features);
        // TODO: Merge their requests properly
        auto mutable_descriptor = VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT,
            .mutableDescriptorType = VK_TRUE,
        };
        device_builder.add_pNext(&mutable_descriptor);
#endif

        auto dynamic_state_3_features = VkPhysicalDeviceExtendedDynamicState3FeaturesEXT{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT,
            .extendedDynamicState3PolygonMode = VK_TRUE,
        };
        device_builder.add_pNext(&dynamic_state_3_features);

        auto rt_validation_features = VkPhysicalDeviceRayTracingValidationFeaturesNV{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_VALIDATION_FEATURES_NV,
            .rayTracingValidation = VK_TRUE};

        if(supports_ray_tracing()) {
            device_builder.add_pNext(&acceleration_structure_features);
            device_builder.add_pNext(&ray_pipeline_features);
            device_builder.add_pNext(&ray_query_features);
            if(supports_rt_validation) {
                device_builder.add_pNext(&rt_validation_features);
            }
        }

        if(supports_device_generated_commands()) {
            device_builder.add_pNext(&device_generated_commands_features);
        }

        if(supports_shading_rate_image) {
            device_builder.add_pNext(&shading_rate_image_features);
        }

        // Set up device creation info for Aftermath feature flag configuration.
        auto aftermath_flags = static_cast<VkDeviceDiagnosticsConfigFlagsNV>(
            VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_RESOURCE_TRACKING_BIT_NV |
            VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_AUTOMATIC_CHECKPOINTS_BIT_NV |
            VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_DEBUG_INFO_BIT_NV |
            VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_ERROR_REPORTING_BIT_NV);
        auto device_diagnostics_info = VkDeviceDiagnosticsConfigCreateInfoNV{
            .sType = VK_STRUCTURE_TYPE_DEVICE_DIAGNOSTICS_CONFIG_CREATE_INFO_NV, .flags = aftermath_flags};

        if(supports_nv_diagnostics_config) {
            device_builder.add_pNext(&device_diagnostics_info);
        }

        {
            ZoneScopedN("vkb::DeviceBuilder::build");
            auto device_ret = device_builder.build();
            if(!device_ret) {
                const auto message = fmt::format("Could not create logical device: {}", device_ret.error().message());
                throw std::runtime_error{"Could not build logical device"};
            }
            device = *device_ret;
        }
        {
            ZoneScopedN("volkLoadDevice");
            volkLoadDevice(device.device);
        }
    }

    void RenderBackend::query_physical_device_features() {
        auto physical_device_features = ExtensibleStruct<VkPhysicalDeviceFeatures2>{};
        physical_device_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

        if(supports_ray_tracing()) {
            ray_pipeline_features = VkPhysicalDeviceRayTracingPipelineFeaturesKHR{
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
            physical_device_features.add_extension(&ray_pipeline_features);

            acceleration_structure_features = VkPhysicalDeviceAccelerationStructureFeaturesKHR{
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
            };
            physical_device_features.add_extension(&acceleration_structure_features);

            ray_query_features = VkPhysicalDeviceRayQueryFeaturesKHR{
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
            };
            physical_device_features.add_extension(&ray_query_features);
        }

        if(supports_device_generated_commands()) {
            device_generated_commands_features = VkPhysicalDeviceDeviceGeneratedCommandsFeaturesNV{
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_FEATURES_NV,
            };
            physical_device_features.add_extension(&device_generated_commands_features);
        }

        if(supports_shading_rate_image) {
            shading_rate_image_features = VkPhysicalDeviceFragmentShadingRateFeaturesKHR{
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR,
                .attachmentFragmentShadingRate = VK_TRUE,
            };
            physical_device_features.add_extension(&shading_rate_image_features);
        }

        vkGetPhysicalDeviceFeatures2(physical_device, *physical_device_features);

        device_features = **physical_device_features;

        if(acceleration_structure_features.accelerationStructure) {
            logger->info("Ray tracing supported");
        }

        supports_rt &= acceleration_structure_features.accelerationStructure == VK_TRUE;

        supports_dgc = device_generated_commands_features.deviceGeneratedCommands == VK_TRUE;

        supports_shading_rate_image = shading_rate_image_features.attachmentFragmentShadingRate == VK_TRUE;
        if(supports_shading_rate_image) {
            logger->debug("Shading rate attachment is supported!");
        } else {
            logger->debug("Shading rate attachment is NOT supported");
        }

        if(supports_shading_rate_image) {
            auto count = uint32_t{};
            vkGetPhysicalDeviceFragmentShadingRatesKHR(physical_device, &count, nullptr);

            auto shading_rates = eastl::vector<VkPhysicalDeviceFragmentShadingRateKHR>(
                count,
                VkPhysicalDeviceFragmentShadingRateKHR{
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_KHR});
            vkGetPhysicalDeviceFragmentShadingRatesKHR(physical_device, &count, shading_rates.data());

            supported_shading_rates = eastl::vector<glm::uvec2>{};
            supported_shading_rates.reserve(count);
            for(const auto& rate : shading_rates) {
                supported_shading_rates.emplace_back(rate.fragmentSize.width, rate.fragmentSize.height);
            }

            logger->debug("We support {} shading rates", count);
        }
    }

    void RenderBackend::query_physical_device_properties() {
        auto physical_device_properties = ExtensibleStruct<VkPhysicalDeviceProperties2>{};
        physical_device_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;

        if(supports_shading_rate_image) {
            physical_device_properties.add_extension(&shading_rate_properties);
        }

        if(supports_raytracing) {
            physical_device_properties.add_extension(&ray_tracing_pipeline_properties);
        }

        vkGetPhysicalDeviceProperties2(physical_device, *physical_device_properties);

        logger->debug("Max supported texel size: {}x{}",
                      shading_rate_properties.maxFragmentShadingRateAttachmentTexelSize.width,
                      shading_rate_properties.maxFragmentShadingRateAttachmentTexelSize.height);
    }

    void RenderBackend::create_swapchain(const uint2 resolution) {
        ZoneScoped;

        auto swapchain_ret =
            vkb::SwapchainBuilder{device}
                .set_desired_format({.format = VK_FORMAT_R8G8B8A8_SRGB, .colorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR})
                .add_fallback_format(
                    {.format = VK_FORMAT_B8G8R8A8_SRGB, .colorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR})
                .set_desired_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
                .set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                .set_desired_extent(resolution.x, resolution.y)
                .build();
        if(!swapchain_ret) {
            throw std::runtime_error{"Could not create swapchain"};
        }

        swapchain = *swapchain_ret;
    }

    RenderBackend::~RenderBackend() {
        wait_for_idle();
    }

    void RenderBackend::init(VkSurfaceKHR surface, const uint2 resolution) {
        create_device(surface);

        graphics_queue = *device.get_queue(vkb::QueueType::graphics);
        graphics_queue_family_index = *device.get_queue_index(vkb::QueueType::graphics);

        set_object_name(graphics_queue, "Graphics Queue");

        // Don't use a dedicated transfer queue, because my attempts at a queue ownership transfer have failed

        // const auto transfer_queue_maybe = device.get_queue(vkb::QueueType::transfer);
        // if (transfer_queue_maybe) {
        //     transfer_queue = *transfer_queue_maybe;
        // } else {
        transfer_queue = graphics_queue;
        // }

        // const auto transfer_queue_family_index_maybe = device.get_queue_index(vkb::QueueType::transfer);
        // if (transfer_queue_family_index_maybe) {
        //     transfer_queue_family_index = *transfer_queue_family_index_maybe;
        // } else {
        transfer_queue_family_index = graphics_queue_family_index;
        // }

        create_tracy_context();

        global_descriptor_allocator = eastl::make_unique<DescriptorSetAllocator>(*this);
        global_descriptor_allocator->init(device.device);

        frame_descriptor_allocators = {DescriptorSetAllocator{*this}, DescriptorSetAllocator{*this}};
        for(auto& frame_allocator : frame_descriptor_allocators) {
            frame_allocator.init(device.device);
        }
        descriptor_layout_cache.init(device.device);

        allocator = eastl::make_unique<ResourceAllocator>(*this);
        g_global_allocator = allocator.get();

        upload_queue = eastl::make_unique<ResourceUploadQueue>(*this);

        blas_build_queue = eastl::make_unique<BlasBuildQueue>();

        pipeline_cache = eastl::make_unique<PipelineCache>(*this);

        texture_descriptor_pool = eastl::make_unique<TextureDescriptorPool>(*this);

        create_swapchain(resolution);

        create_command_pools();

        constexpr auto fence_create_info = VkFenceCreateInfo{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };
        for(auto& fence : frame_fences) {
            vkCreateFence(device.device, &fence_create_info, nullptr, &fence);
        }

        create_default_resources();

        logger->info("Initialized backend");
    }

    bool RenderBackend::supports_ray_tracing() const {
        return supports_rt; 
    }

    bool RenderBackend::supports_device_generated_commands() const {
        return supports_dgc;
    }

    const eastl::vector<glm::uvec2>& RenderBackend::get_shading_rates() const {
        return supported_shading_rates;
    }

    glm::vec2 RenderBackend::get_max_shading_rate_texel_size() const {
        const auto properties_max = glm::vec2{shading_rate_properties.maxFragmentShadingRateAttachmentTexelSize.width,
                                              shading_rate_properties.maxFragmentShadingRateAttachmentTexelSize.height};

        return properties_max;
    }

    uint32_t RenderBackend::get_shader_group_handle_size() const {
        return ray_tracing_pipeline_properties.shaderGroupHandleSize;
    }

    uint32_t RenderBackend::get_shader_group_alignment() const {
        return ray_tracing_pipeline_properties.shaderGroupBaseAlignment;
    }

    void RenderBackend::execute_graph(RenderGraph& render_graph) {
        submit_command_buffer(render_graph.extract_command_buffer());

        render_graph.execute_post_submit_tasks();
    }

    VkInstance RenderBackend::get_instance() const {
        return instance.instance;
    }

    const vkb::PhysicalDevice& RenderBackend::get_physical_device() const {
        return physical_device;
    }

    bool RenderBackend::supports_astc() const {
        return physical_device.features.textureCompressionASTC_LDR;
    }

    bool RenderBackend::supports_etc2() const {
        return physical_device.features.textureCompressionETC2;
    }

    bool RenderBackend::supports_bc() const {
        return physical_device.features.textureCompressionBC;
    }

    const vkb::Device& RenderBackend::get_device() const {
        return device;
    }

    bool RenderBackend::has_separate_transfer_queue() const {
        return graphics_queue_family_index != transfer_queue_family_index;
    }

    uint32_t RenderBackend::get_graphics_queue_family_index() const {
        return graphics_queue_family_index;
    }

    VkQueue RenderBackend::get_transfer_queue() const {
        return transfer_queue;
    }

    uint32_t RenderBackend::get_transfer_queue_family_index() const {
        return transfer_queue_family_index;
    }

    void RenderBackend::advance_frame() {
        ZoneScoped;

        if(!is_first_frame) {
            total_num_frames++;

            cur_frame_idx++;
            cur_frame_idx %= num_in_flight_frames;
        }

        logger->trace("Beginning frame {} (frame idx {})", total_num_frames, cur_frame_idx);

        if(total_num_frames % 100 == 0) {
            // allocator->report_memory_usage();
        }

        {
            ZoneScopedN("Wait for previous frame");
            const auto result =
                vkWaitForFences(device, 1, &frame_fences[cur_frame_idx], VK_TRUE, std::numeric_limits<uint64_t>::max());
            if(result != VK_SUCCESS) {
                logger->error("Could not wait for frame fence: {}", string_VkResult(result));
            }
        }

        swapchain_semaphore = create_transient_semaphore("Acquire swapchain semaphore");
        {
            ZoneScopedN("Acquire swapchain image");
            vkAcquireNextImageKHR(device,
                                  swapchain.swapchain,
                                  std::numeric_limits<uint64_t>::max(),
                                  swapchain_semaphore,
                                  VK_NULL_HANDLE,
                                  &cur_swapchain_image_idx);
        }

        if(!is_first_frame) {
            graphics_command_allocators[cur_frame_idx].reset();
            transfer_command_allocators[cur_frame_idx].reset();

            auto& semaphores = zombie_semaphores[cur_frame_idx];
            available_semaphores.insert(available_semaphores.end(), semaphores.begin(), semaphores.end());
            // for(const auto& semaphore : semaphores) {
            //     vkDestroySemaphore(device, semaphore, nullptr);
            // }
            semaphores.clear();

            allocator->free_resources_for_frame(cur_frame_idx);

            frame_descriptor_allocators[cur_frame_idx].reset_pools();
        }

        vkResetFences(device, 1, &frame_fences[cur_frame_idx]);

        is_first_frame = false;
    }

    void RenderBackend::flush_batched_command_buffers() {
        ZoneScoped;

        // Flushes pending uploads to our queued_transfer_command_buffers
        upload_queue->flush_pending_uploads();

        if(!queued_transfer_command_buffers.empty()) {
            const auto submission_semaphore = create_transient_semaphore("Transfer commands submission");
            constexpr auto mask = static_cast<VkPipelineStageFlags>(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
            const auto transfer_submit = VkSubmitInfo{
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .waitSemaphoreCount = static_cast<uint32_t>(last_submission_semaphores.size()),
                .pWaitSemaphores = last_submission_semaphores.data(),
                .pWaitDstStageMask = &mask,
                .commandBufferCount = static_cast<uint32_t>(queued_transfer_command_buffers.size()),
                .pCommandBuffers = queued_transfer_command_buffers.data(),
                .signalSemaphoreCount = 1,
                .pSignalSemaphores = &submission_semaphore,
            };

            {
                ZoneScopedN("vkQueueSubmit transfer");
                vkQueueSubmit(transfer_queue, 1, &transfer_submit, VK_NULL_HANDLE);
            }

            for(auto commands : queued_transfer_command_buffers) {
                transfer_command_allocators[cur_frame_idx].return_command_buffer(commands);
            }

            queued_transfer_command_buffers.clear();

            last_submission_semaphores.clear();
            last_submission_semaphores.emplace_back(submission_semaphore);

            destroy_semaphore(submission_semaphore);
        }

        // Submit any transfer barriers
        // Currently, the high-level code decides if we need a transfer barrier and the backend just does what it's told
        // This is fine I guess
        if(!transfer_barriers.empty()) {
            const auto transfer_semaphore = create_transient_semaphore("Queue transfer operation");

            // Submit release barriers to transfer queue
            {
                const auto commands = create_transfer_command_buffer("Transfer queue release command buffer");

                constexpr auto begin_info =
                    VkCommandBufferBeginInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                             .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
                vkBeginCommandBuffer(commands, &begin_info);

                const auto dependency =
                    VkDependencyInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                     .imageMemoryBarrierCount = static_cast<uint32_t>(transfer_barriers.size()),
                                     .pImageMemoryBarriers = transfer_barriers.data()};
                vkCmdPipelineBarrier2(commands, &dependency);

                vkEndCommandBuffer(commands);

                const auto command_submit = VkCommandBufferSubmitInfo{
                    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                    .commandBuffer = commands,
                };
                const auto semaphore_signal =
                    VkSemaphoreSubmitInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                                          .semaphore = transfer_semaphore,
                                          .stageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT};
                const auto submit = VkSubmitInfo2{
                    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                    .commandBufferInfoCount = 1,
                    .pCommandBufferInfos = &command_submit,
                    .signalSemaphoreInfoCount = 1,
                    .pSignalSemaphoreInfos = &semaphore_signal,
                };
                vkQueueSubmit2(transfer_queue, 1, &submit, VK_NULL_HANDLE);
            }

            // Submit acquire barriers to graphics queue
            {
                auto commands = create_graphics_command_buffer("Graphics queue acquire command buffer");

                commands.begin();

                commands.barrier({}, {}, transfer_barriers);

                commands.end();

                const auto semaphore_wait = VkSemaphoreSubmitInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                                                                  .semaphore = transfer_semaphore,
                                                                  .stageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT};
                const auto command_submit = VkCommandBufferSubmitInfo{
                    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                    .commandBuffer = commands.get_vk_commands(),
                };
                const auto submit = VkSubmitInfo2{
                    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                    .waitSemaphoreInfoCount = 1,
                    .pWaitSemaphoreInfos = &semaphore_wait,
                    .commandBufferInfoCount = 1,
                    .pCommandBufferInfos = &command_submit
                    // No need for semaphores - the command buffer only contains barriers, those provide synchronization
                };
                vkQueueSubmit2(graphics_queue, 1, &submit, VK_NULL_HANDLE);

                destroy_semaphore(transfer_semaphore);
            }

            transfer_barriers.clear();
        }

        if(!queued_command_buffers.empty()) {
            auto command_buffers = eastl::vector<VkCommandBuffer>{};
            command_buffers.reserve(queued_command_buffers.size() * 2);

            for(const auto& queued_commands : queued_command_buffers) {
                command_buffers.emplace_back(queued_commands.get_vk_commands());
            }

            auto wait_stages = eastl::fixed_vector<VkPipelineStageFlags, 8>{VK_PIPELINE_STAGE_ALL_COMMANDS_BIT};
            auto wait_semaphores = eastl::fixed_vector<VkSemaphore, 8>{swapchain_semaphore};
            if(!last_submission_semaphores.empty()) {
                wait_semaphores.insert(
                    wait_semaphores.end(), last_submission_semaphores.begin(), last_submission_semaphores.end());
                wait_stages.emplace_back(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

                last_submission_semaphores.clear();
            }

            auto signal_semaphore =
                create_transient_semaphore(fmt::format("Graphics submit semaphore {}", cur_frame_idx));

            const auto submit = VkSubmitInfo{
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .waitSemaphoreCount = static_cast<uint32_t>(wait_semaphores.size()),
                .pWaitSemaphores = wait_semaphores.data(),
                .pWaitDstStageMask = wait_stages.data(),
                .commandBufferCount = static_cast<uint32_t>(command_buffers.size()),
                .pCommandBuffers = command_buffers.data(),
                .signalSemaphoreCount = 1,
                .pSignalSemaphores = &signal_semaphore,
            };

            {
                ZoneScopedN("vkQueueSubmit graphics");

                logger->trace("Submitting graphics commands");
                logger->flush();
                const auto result = vkQueueSubmit(graphics_queue, 1, &submit, frame_fences[cur_frame_idx]);
                logger->trace("Submitted submission fence for frame {}", cur_frame_idx);

                if(result == VK_ERROR_DEVICE_LOST) {
                    logger->error("Device lost detected!");
                    logger->flush();
                    SAH_BREAKPOINT;
                }
            }

            for(const auto& queued_commands : queued_command_buffers) {
                graphics_command_allocators[cur_frame_idx].return_command_buffer(queued_commands.get_vk_commands());
            }

            destroy_semaphore(swapchain_semaphore);

            queued_command_buffers.clear();

            last_submission_semaphores.emplace_back(signal_semaphore);
        } else {
            logger->warn("No queued command buffers this frame? Things might get wonky");
        }
    }

    void RenderBackend::collect_tracy_data(const CommandBuffer& commands) const {
        TracyVkCollect(tracy_context, commands.get_vk_commands())}

    TracyVkCtx RenderBackend::get_tracy_context() const {
        return tracy_context;
    }

    void RenderBackend::create_tracy_context() {
        ZoneScoped;

        const auto pool_create_info = VkCommandPoolCreateInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = graphics_queue_family_index,
        };
        vkCreateCommandPool(device.device, &pool_create_info, nullptr, &tracy_command_pool);

        const auto command_buffer_allocate = VkCommandBufferAllocateInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = tracy_command_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        vkAllocateCommandBuffers(device.device, &command_buffer_allocate, &tracy_command_buffer);

        tracy_context =
            TracyVkContext(physical_device.physical_device, device.device, graphics_queue, tracy_command_buffer)
    }

    ResourceAllocator& RenderBackend::get_global_allocator() const {
        return *allocator;
    }

    GraphicsPipelineBuilder RenderBackend::begin_building_pipeline(const std::string_view name) const {
        return GraphicsPipelineBuilder{*pipeline_cache}.set_name(name);
    }

    HitGroupBuilder RenderBackend::create_hit_group(const std::string_view name) const {
        return HitGroupBuilder{*pipeline_cache}.set_name(name);
    }

    uint32_t RenderBackend::get_current_gpu_frame() const {
        return cur_frame_idx;
    }

    ResourceUploadQueue& RenderBackend::get_upload_queue() const {
        return *upload_queue;
    }

    BlasBuildQueue& RenderBackend::get_blas_build_queue() const {
        return *blas_build_queue;
    }

    ResourceAccessTracker& RenderBackend::get_resource_access_tracker() {
        return resource_access_synchronizer;
    }

    PipelineCache& RenderBackend::get_pipeline_cache() const {
        return *pipeline_cache;
    }

    TextureDescriptorPool& RenderBackend::get_texture_descriptor_pool() const {
        return *texture_descriptor_pool;
    }

    CommandBuffer RenderBackend::create_graphics_command_buffer(const std::string& name) {
        static uint32_t num_command_buffers = 0;
        return CommandBuffer{graphics_command_allocators[cur_frame_idx].allocate_command_buffer(
                                 fmt::format("{} for frame {} {}", name, cur_frame_idx, num_command_buffers++)),
                             *this};
    }

    VkCommandBuffer RenderBackend::create_transfer_command_buffer(const std::string& name) {
        return transfer_command_allocators[cur_frame_idx].allocate_command_buffer(
            fmt::format("{} for frame {}", name, cur_frame_idx));
    }

    void RenderBackend::create_command_pools() {
        ZoneScoped;

        for(auto& command_pool : graphics_command_allocators) {
            command_pool = CommandAllocator{*this, graphics_queue_family_index};
        }

        for(auto& command_pool : transfer_command_allocators) {
            command_pool = CommandAllocator{*this, transfer_queue_family_index};
        }
    }

    void RenderBackend::submit_transfer_command_buffer(VkCommandBuffer commands) {
        // Batch the command buffer so we can submit it at end-of-frame
        queued_transfer_command_buffers.push_back(commands);
    }

    void RenderBackend::submit_command_buffer(CommandBuffer&& commands) {
        ZoneScoped;

        queued_command_buffers.emplace_back(std::move(commands));
    }

    void RenderBackend::present() {
        const auto present_info =
            VkPresentInfoKHR{.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                             .waitSemaphoreCount = static_cast<uint32_t>(last_submission_semaphores.size()),
                             .pWaitSemaphores = last_submission_semaphores.data(),
                             .swapchainCount = 1,
                             .pSwapchains = &swapchain.swapchain,
                             .pImageIndices = &cur_swapchain_image_idx};
        {
            ZoneScopedN("vkQueuePresentKHR");
            vkQueuePresentKHR(graphics_queue, &present_info);
        }

        last_submission_semaphores.clear();
    }

    void RenderBackend::wait_for_idle() const {
        vkDeviceWaitIdle(device);
    }

    DescriptorSetAllocator& RenderBackend::get_persistent_descriptor_allocator() {
        return *global_descriptor_allocator;
    }

    DescriptorSetAllocator& RenderBackend::get_transient_descriptor_allocator() {
        return frame_descriptor_allocators[cur_frame_idx];
    }

    VkSemaphore RenderBackend::create_transient_semaphore(const std::string& name) {
        auto semaphore = VkSemaphore{};

        if(!available_semaphores.empty()) {
            semaphore = available_semaphores.back();
            available_semaphores.pop_back();

        } else {
            constexpr auto create_info = VkSemaphoreCreateInfo{
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            };

            vkCreateSemaphore(device, &create_info, nullptr, &semaphore);
        }

        // if(!name.empty() && vkSetDebugUtilsObjectNameEXT != nullptr) {
        //     const auto name_info = VkDebugUtilsObjectNameInfoEXT{
        //         .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        //         .objectType = VK_OBJECT_TYPE_SEMAPHORE,
        //         .objectHandle = reinterpret_cast<uint64_t>(semaphore),
        //         .pObjectName = name.c_str(),
        //     };
        //     vkSetDebugUtilsObjectNameEXT(device, &name_info);
        // }

        return semaphore;
    }

    void RenderBackend::destroy_semaphore(VkSemaphore semaphore) {
        zombie_semaphores[cur_frame_idx].emplace_back(semaphore);
    }

    vkb::Swapchain& RenderBackend::get_swapchain() {
        return swapchain;
    }

    uint32_t RenderBackend::get_current_swapchain_index() const {
        return cur_swapchain_image_idx;
    }

    vkutil::DescriptorLayoutCache& RenderBackend::get_descriptor_cache() {
        return descriptor_layout_cache;
    }

    TextureHandle RenderBackend::get_white_texture_handle() const {
        return white_texture_handle;
    }

    TextureHandle RenderBackend::get_default_normalmap_handle() const {
        return default_normalmap_handle;
    }

    VkSampler RenderBackend::get_default_sampler() const {
        return default_sampler;
    }

    uint32_t RenderBackend::get_white_texture_srv() const {
        return white_texture_srv;
    }

    uint32_t RenderBackend::get_default_normalmap_srv() const {
        return default_normalmap_srv;
    }

    PFN_vkGetInstanceProcAddr RenderBackend::get_vk_load_proc_addr() {
        return vkGetInstanceProcAddr;
    }

    void RenderBackend::deinit() {
        pipeline_cache->destroy_all_pipelines();
    }

    void RenderBackend::create_default_resources() {
        ZoneScoped;

        white_texture_handle = allocator->create_texture(
            "White texture", {VK_FORMAT_R8G8B8A8_UNORM, glm::uvec2{8, 8}, 1, TextureUsage::StaticImage});

        default_normalmap_handle = allocator->create_texture(
            "Default normalmap", {VK_FORMAT_R8G8B8A8_UNORM, glm::uvec2{8, 8}, 1, TextureUsage::StaticImage});

        default_sampler = allocator->get_sampler(
            VkSamplerCreateInfo{.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .maxLod = VK_LOD_CLAMP_NONE});

        upload_queue->enqueue(TextureUploadJob{.destination = white_texture_handle,
                                               .mip = 0,
                                               .data = eastl::vector<uint8_t>(static_cast<size_t>(64 * 4), 0xFF)});
        upload_queue->enqueue(TextureUploadJob{
            .destination = default_normalmap_handle,
            .mip = 0,
            .data = eastl::vector<uint8_t>{
                0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00,
                0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00,
                0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00,
                0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00,
                0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00,
                0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00,
                0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00,
                0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00,
                0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00,
                0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00,
                0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00,
                0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00,
                0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00,
                0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00,
                0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00,
                0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0x00,
            }});

        white_texture_srv = texture_descriptor_pool->create_texture_srv(white_texture_handle, default_sampler);
        default_normalmap_srv = texture_descriptor_pool->create_texture_srv(default_normalmap_handle, default_sampler);
    }

    void RenderBackend::set_object_name(const uint64_t object_handle, const VkObjectType object_type,
                                        const std::string& name) const {
        if(vkSetDebugUtilsObjectNameEXT != nullptr) {
            const auto name_info = VkDebugUtilsObjectNameInfoEXT{
                .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                .objectType = object_type,
                .objectHandle = object_handle,
                .pObjectName = name.c_str(),
            };
            vkSetDebugUtilsObjectNameEXT(device, &name_info);
        }
    }
} // namespace render
