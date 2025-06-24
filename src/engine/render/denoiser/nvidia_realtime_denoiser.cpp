#include "nvidia_realtime_denoiser.hpp"

#include <EASTL/array.h>

#include "core/system_interface.hpp"

namespace render {
    static std::shared_ptr<spdlog::logger> logger;

    NvidiaRealtimeDenoiser::NvidiaRealtimeDenoiser() {
        if(logger == nullptr) {
            logger = SystemInterface::get().get_logger("NvidiaRealtimeDenoiser");
        }
        const auto denoiser_descs = eastl::array{
            // ReLAX
            nrd::DenoiserDesc{
                .identifier = static_cast<nrd::Identifier>(nrd::Denoiser::RELAX_DIFFUSE_SPECULAR),
                .denoiser = nrd::Denoiser::RELAX_DIFFUSE_SPECULAR
            },
            // SIGMA
            nrd::DenoiserDesc{
                .identifier = static_cast<nrd::Identifier>(nrd::Denoiser::SIGMA_SHADOW),
                .denoiser = nrd::Denoiser::SIGMA_SHADOW
            },
        };

        const auto instance_create_descs = nrd::InstanceCreationDesc{
            .denoisers = denoiser_descs.data(),
            .denoisersNum = static_cast<uint32_t>(denoiser_descs.size())
        };

        const auto result = nrd::CreateInstance(instance_create_descs, instance);
        if(result != nrd::Result::SUCCESS) {
            logger->error("Could not create NRD: {}", result);
            instance = nullptr;
        }

        const auto settings = nrd::CommonSettings{
.viewToClipMatrix = ,
.viewToClipMatrixPrev = ,
.worldToViewMatrix = ,
.worldToViewMatrixPrev = ,
.worldPrevToWorldMatrix = ,
.motionVectorScale = ,
.cameraJitter = ,
.cameraJitterPrev = ,
.resourceSize = ,
.resourceSizePrev = ,
.rectSize = ,
.rectSizePrev = ,
.viewZScale = ,
.timeDeltaBetweenFrames = ,
.denoisingRange = ,
.disocclusionThreshold = ,
.disocclusionThresholdAlternate = ,
.cameraAttachedReflectionMaterialID = ,
.strandMaterialID = ,
.strandThickness = ,
.splitScreen = ,
.printfAt = ,
.debug = ,
.rectOrigin = ,
.frameIndex = ,
.accumulationMode = ,
.isMotionVectorInWorldSpace = ,
.isHistoryConfidenceAvailable = ,
.isDisocclusionThresholdMixAvailable = ,
.isBaseColorMetalnessAvailable = ,
.enableValidation =
        };
        nrd::SetCommonSettings(*instance, settings);
    }

    void NvidiaRealtimeDenoiser::set_resolution(const uint2& resolution) {
        if(resolution == cached_resolution) {
            return;
        }

        cached_resolution = resolution;
    }
} // render
