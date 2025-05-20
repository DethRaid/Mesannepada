#include "physics_shape_loader.hpp"

#include <fstream>
#include <Jolt/Core/StreamWrapper.h>
#include <Jolt/Physics/Collision/PhysicsMaterial.h>

#include "core/system_interface.hpp"

namespace physics {
    static std::shared_ptr<spdlog::logger> logger;

    static void init_logger() {
        if(logger == nullptr) {
            logger = SystemInterface::get().get_logger("PhysicsShapeLoader");
        }
    }

    JPH::Ref<JPH::Shape> load_shape(const std::filesystem::path& filepath) {
        init_logger();

        auto std_stream = std::ifstream{filepath, std::ios::binary};
        auto stream = JPH::StreamInWrapper{std_stream};
        JPH::Shape::IDToShapeMap shape_map{};
        JPH::Shape::IDToMaterialMap material_map{};
        auto result = JPH::Shape::sRestoreWithChildren(stream, shape_map, material_map);
        if(result.HasError()) {
            logger->error("Could not load shape {}: {}", filepath.string(), result.GetError());
            return nullptr;
        }

        return result.Get();
    }

    void save_shape(const JPH::Shape& shape, const std::filesystem::path& filepath) {
        if(!std::filesystem::exists(filepath.parent_path())) {
            std::filesystem::create_directories(filepath.parent_path());
        }

        auto std_stream = std::ofstream{filepath, std::ios::binary};
        auto stream = JPH::StreamOutWrapper{std_stream};
        JPH::Shape::ShapeToIDMap shape_map{};
        JPH::Shape::MaterialToIDMap material_map{};
        shape.SaveWithChildren(stream, shape_map, material_map);
    }
}
