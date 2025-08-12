#include "gltf_model.hpp"

#include <numbers>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/TaperedCapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/TaperedCylinderShape.h>
#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <spdlog/logger.h>
#include <spdlog/sinks/android_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <tracy/Tracy.hpp>

#include "core/box.hpp"
#include "core/engine.hpp"
#include "core/generated_entity_component.hpp"
#include "core/visitor.hpp"
#include "physics/collider_component.hpp"
#include "player/player_parent_component.hpp"
#include "render/basic_pbr_material.hpp"
#include "render/sarah_renderer.hpp"
#include "render/texture_loader.hpp"
#include "render/components/light_component.hpp"
#include "render/components/skeletal_mesh_component.hpp"
#include "render/components/static_mesh_component.hpp"
#include "resources/gltf_animations.hpp"
#include "resources/model_components.hpp"
#include "scene/entity_info_component.hpp"
#include "scene/world.hpp"
#include "scene/transform_component.hpp"

template<>
struct fastgltf::ElementTraits<glm::quat> : fastgltf::ElementTraits<fastgltf::math::fquat> {
};

static std::shared_ptr<spdlog::logger> logger;

static bool front_face_ccw = false;

static eastl::vector<StandardVertex> read_vertex_data(const fastgltf::Primitive& primitive, const fastgltf::Asset& model
    );

static eastl::vector<uint32_t> read_index_data(const fastgltf::Primitive& primitive, const fastgltf::Asset& model);

static eastl::tuple<eastl::vector<u16vec4>, eastl::vector<float4> > read_skinning_data(
    const fastgltf::Primitive& primitive, const fastgltf::Asset& asset
    );

static Box read_mesh_bounds(const fastgltf::Primitive& primitive, const fastgltf::Asset& model);

static void copy_vertex_data_to_vector(
    const fastgltf::Primitive& primitive,
    const fastgltf::Asset& model,
    StandardVertex* vertices
    );

template<typename DataType>
static AnimationTimeline<DataType> read_animation_sampler(
    size_t sampler_index, const fastgltf::Animation& animation, const fastgltf::Asset& asset
    );

glm::mat4 get_node_to_parent_matrix(const fastgltf::Node& node) {
    auto matrix = glm::mat4{1.f};

    std::visit(
        Visitor{
            [&](const fastgltf::math::fmat4x4& node_matrix) {
                matrix = glm::make_mat4(node_matrix.data());
            },
            [&](const fastgltf::TRS& trs) {
                const auto translation = glm::make_vec3(trs.translation.data());
                const auto rotation = glm::quat{trs.rotation[3], trs.rotation[0], trs.rotation[1], trs.rotation[2]};
                const auto scale_factors = glm::make_vec3(trs.scale.data());

                matrix = glm::translate(translation) * glm::toMat4(rotation) * glm::scale(scale_factors);
            }
        },
        node.transform
        );

    return matrix;
}

GltfModel::~GltfModel() {
    auto& animations = Engine::get().get_animation_system();
    for(const auto& gltf_animation : asset.animations) {
        animations.remove_animation(skeleton_handle, gltf_animation.name.c_str());
    }

    animations.destroy_skeleton(skeleton_handle);
}

GltfModel::GltfModel(
    ResourcePath filepath_in,
    fastgltf::Asset&& model,
    render::SarahRenderer& renderer,
    ExtrasData extras_in
    ) :
    filepath{std::move(filepath_in)},
    cached_data_path{SystemInterface::get().get_cache_folder() / filepath.get_path()},
    asset{std::move(model)},
    extras{eastl::move(extras_in)} {
    if(logger == nullptr) {
        logger = SystemInterface::get().get_logger("GltfModel");
    }

    ZoneScoped;

    cached_data_path.replace_extension();
    if(!exists(cached_data_path)) {
        std::filesystem::create_directories(cached_data_path);
    }

    logger->info("Beginning load of model {}", filepath);

    validate_model();

    import_resources_for_model(renderer);

    calculate_bounding_sphere_and_footprint();

    logger->info("Loaded model {}", filepath);
}

glm::vec4 GltfModel::get_bounding_sphere() const {
    return bounding_sphere;
}

const fastgltf::Asset& GltfModel::get_gltf_data() const {
    return asset;
}

entt::handle GltfModel::add_nodes_to_world(World& world, const eastl::optional<entt::handle>& parent_node) const {
    ZoneScoped;

    auto& registry = world.get_registry();

    eastl::vector<entt::handle> world_entities;
    world_entities.reserve(asset.nodes.size());
    // Spawn one entity per node, indexed by node ID
    for(const auto& node : asset.nodes) {
        const auto node_entity = world.create_entity();
        node_entity.emplace<TransformComponent>();
        node_entity.emplace<EntityInfoComponent>(node.name.c_str());
        node_entity.emplace<GeneratedEntityComponent>();
        world_entities.emplace_back(node_entity);
    }

    auto parent_matrix = float4x4{1.f};
    if(parent_node) {
        parent_matrix = registry.get<TransformComponent>(*parent_node).get_local_to_parent();
    }

    // Traverse the nodes and create components.

    traverse_nodes(
        [&](const size_t node_index, const fastgltf::Node& node, const float4x4& parent_to_world) {
            const auto entity = world_entities.at(node_index);

            for(const auto child_index : node.children) {
                world.parent_entity_to_entity(world_entities.at(child_index), entity);
            }

            const auto node_to_parent = get_node_to_parent_matrix(node);
            registry.patch<TransformComponent>(
                entity,
                [&](TransformComponent& transform) {
                    transform.set_local_transform(node_to_parent);
                });

            if(node.meshIndex) {
                if(node.skinIndex) {
                    logger->debug("Adding skeletal mesh component to entity {}",
                                  static_cast<uint32_t>(entity.entity()));
                    add_skeletal_mesh_component(entity, node, node_index);
                } else {
                    add_static_mesh_component(entity, node, node_index);
                }
            }

            if(node.physicsRigidBody) {
                add_collider_component(entity, node, node_index, parent_to_world * node_to_parent);
            }

            if(node.lightIndex) {
                add_light_component(entity, asset.lights.at(*node.lightIndex));
            }
        },
        parent_matrix
        );

    // Add imported information to our root node
    assert(asset.scenes[*asset.defaultScene].nodeIndices.size() == 1);
    const auto root_node_index = asset.scenes[*asset.defaultScene].nodeIndices[0];
    const auto root_entity = world_entities[root_node_index];
    root_entity.emplace<ImportedModelComponent>(filepath.to_string(), world_entities);

    // Add our top-level entities to the scene
    if(!parent_node) {
        auto top_levels = eastl::fixed_vector<entt::handle, 16>{};
        for(const auto top_level_node : asset.scenes[*asset.defaultScene].nodeIndices) {
            top_levels.emplace_back(world_entities.at(top_level_node));
        }
        world.add_top_level_entities(top_levels);
    }

    // Link to parent node, if present
    if(parent_node) {
        const auto& gltf_scene = asset.scenes[*asset.defaultScene];

        for(const auto& node_index : gltf_scene.nodeIndices) {
            const auto node_entity = world_entities[node_index];
            world.parent_entity_to_entity(node_entity, *parent_node);
        }
    }

    world_entities.clear();
    return root_entity;
}

void GltfModel::add_static_mesh_component(const entt::handle& entity, const fastgltf::Node& node, size_t node_index
    ) const {
    ZoneScoped;
    const auto mesh_index = *node.meshIndex;
    const auto& mesh = asset.meshes[mesh_index];

    eastl::fixed_vector<render::StaticMeshPrimitive, 8> primitives;
    primitives.reserve(mesh.primitives.size());

    auto cast_shadows = true;
    if(const auto itr = extras.visible_to_ray_tracing.find(node_index); itr != extras.visible_to_ray_tracing.end()) {
        cast_shadows = itr->second;
    }

    for(auto i = 0u; i < mesh.primitives.size(); i++) {
        const auto& gltf_primitive = mesh.primitives.at(i);
        const auto& imported_mesh = gltf_primitive_to_mesh.at(mesh_index).at(i);
        const auto& imported_material = gltf_material_to_material_handle.at(
            gltf_primitive.materialIndex.value_or(0)
            );

        primitives.emplace_back(render::StaticMeshPrimitive{.mesh = imported_mesh, .material = imported_material,
                                                            .visible_to_ray_tracing = cast_shadows});
    }

    entity.emplace<render::StaticMeshComponent>(primitives);
}

void GltfModel::add_skeletal_mesh_component(const entt::handle& entity, const fastgltf::Node& node,
                                            const size_t node_index
    ) const {
    ZoneScoped;
    const auto mesh_index = *node.meshIndex;
    const auto& mesh = asset.meshes[mesh_index];

    eastl::fixed_vector<render::SkeletalMeshPrimitive, 8> primitives;
    primitives.reserve(mesh.primitives.size());

    auto cast_shadows = true;
    if(const auto itr = extras.visible_to_ray_tracing.find(node_index); itr != extras.visible_to_ray_tracing.end()) {
        cast_shadows = itr->second;
    }

    for(auto i = 0u; i < mesh.primitives.size(); i++) {
        const auto& gltf_primitive = mesh.primitives.at(i);
        const auto& imported_mesh = gltf_primitive_to_mesh.at(mesh_index).at(i);
        const auto& imported_material = gltf_material_to_material_handle.at(
            gltf_primitive.materialIndex.value_or(0)
            );

        primitives.emplace_back(
            render::SkeletalMeshPrimitive{
                .mesh = imported_mesh,
                .material = imported_material,
                .visible_to_ray_tracing = cast_shadows
            });
    }

    // Skeletal mesh component should have a copy of the skin. The proxy will have buffers for the inverse bind matrices
    // and bone transforms. We'll add animator components to this entity with pointers to the animations from this model
    // The animation system can evaluate those, then update the proxy. THe render proxy will upload the bone matrices, then
    // the renderer will evaluate skinning and write out the new vertex buffers. We can update the RTAS

    entity.emplace<render::SkeletalMeshComponent>(
        primitives,
        skeleton_handle,
        skeleton_handle->bones,
        eastl::vector<float4x4>(skeleton_handle->bones.size()));
}

void GltfModel::add_collider_component(
    const entt::handle& entity, const fastgltf::Node& node, const size_t node_index, const float4x4& transform
    ) const {
    ZoneScopedN("create physics body");
    if(const auto& rigid_body = *node.physicsRigidBody; rigid_body.collider) {
        JPH::Ref<JPH::Shape> shape = get_collider_for_node(node_index, *rigid_body.collider);

        // Read collider material, if any
        auto friction = 0.6f;
        auto restitution = 0.f;
        if(rigid_body.collider->physicsMaterial) {
            const auto& material = asset.physicsMaterials.at(*rigid_body.collider->physicsMaterial);
            friction = material.staticFriction;
            restitution = material.restitution;
        }

        auto mobility = JPH::EMotionType::Static;
        auto layer = physics::layers::NON_MOVING;
        if(rigid_body.motion) {
            if(rigid_body.motion->isKinematic) {
                mobility = JPH::EMotionType::Kinematic;
            } else {
                mobility = JPH::EMotionType::Dynamic;
            }

            layer = physics::layers::MOVING;
        }

        glm::vec3 scale;
        glm::quat orientation;
        glm::vec3 translation;
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::decompose(transform, scale, orientation, translation, skew, perspective);

        auto body_settings = JPH::BodyCreationSettings{
            shape,
            JPH::Vec3{translation.x, translation.y, translation.z},
            JPH::Quat{orientation.x, orientation.y, orientation.z, orientation.w},
            mobility,
            layer
        };

        body_settings.mIsSensor = node.physicsRigidBody->trigger.has_value();
        body_settings.mFriction = friction;
        body_settings.mRestitution = restitution;

        if(rigid_body.motion) {
            if(rigid_body.motion->mass) {
                body_settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                body_settings.mMassPropertiesOverride.mMass = *rigid_body.motion->mass;
            }

            if(rigid_body.motion->inertialDiagonal) {
                // body_settings.mMassPropertiesOverride.mInertia =
            }

            body_settings.mGravityFactor = rigid_body.motion->gravityFactor;
        } {
            ZoneScopedN("create_body");
            auto& physics_scene = Engine::get().get_physics_world();
            auto& body_interface = physics_scene.get_body_interface();

            const auto body_id = body_interface.CreateAndAddBody(body_settings, JPH::EActivation::Activate);

            entity.emplace<physics::CollisionComponent>(body_id);
        }
    }
}

static float calculate_light_range(const fastgltf::num intensity) {
    // Calculate the light range based on where the light's intensity is this value
    constexpr auto min_intensity = 1.0f;

    // Attenuation factor needed to reach that intensity
    const auto attenuation = min_intensity / intensity;

    // Use the simple point light attenuation. Won't be exactly correct, should be plausible
    return sqrt(1.f / attenuation);
}

void GltfModel::add_light_component(const entt::handle& entity, const fastgltf::Light& light) {
    const auto full_color = light.color * light.intensity;
    const auto glm_color = float3{full_color.x(), full_color.y(), full_color.z()};
    switch(light.type) {
    case fastgltf::LightType::Directional:
        entity.emplace<render::DirectionalLightComponent>(glm_color);
        break;

    case fastgltf::LightType::Spot: {
        // Calculate where the light's intensity would almost reach zero. Set the light's range to that number. This formula will likely need tweaking
        const auto light_range = light.range.value_or(calculate_light_range(light.intensity));
        entity.emplace<render::SpotLightComponent>(
            glm_color,
            light_range,
            0.1f,
            light.innerConeAngle.value_or(0),
            light.outerConeAngle.value_or(std::numbers::pi / 4.f));
    }
    break;

    case fastgltf::LightType::Point: {
        const auto light_range = light.range.value_or(calculate_light_range(light.intensity));
        entity.emplace<render::PointLightComponent>(
            glm_color,
            light_range,
            0.1f
            );
    }
    break;
    }
}

JPH::Ref<JPH::Shape> GltfModel::get_collider_for_node(
    const size_t node_index, const fastgltf::Collider& collider
    ) const {
    JPH::Ref<JPH::Shape> shape = nullptr;

    // See if we've cached a collider for this node
    const auto cached_body_path = cached_data_path
                                  / "bodies"
                                  / std::to_string(JPH_VERSION_ID)
                                  / std::to_string(node_index);
    if(exists(cached_body_path) && last_write_time(cached_body_path) > last_write_time(filepath.to_filepath())) {
        shape = physics::load_shape(cached_body_path);
    }

    // If there's no data on the filesystem, or the load failed for another reason, create the shape and save it
    if(shape == nullptr) {
        shape = create_jolt_shape(collider);
        physics::save_shape(*shape, cached_body_path);
    }

    return shape;
}

JPH::Ref<JPH::Shape> GltfModel::create_jolt_shape(const fastgltf::Collider& collider) const {
    JPH::ShapeSettings* shape_settings = nullptr;

    const auto& collision_geometry = collider.geometry;
    if(collision_geometry.node) {
        // If we have a node collider, use the mesh attached to that node to create the collider
        auto collision_mesh = read_collision_mesh_from_node(*collision_geometry.node);
        shape_settings = new JPH::MeshShapeSettings{
            eastl::move(collision_mesh.first), eastl::move(collision_mesh.second)
        };

    } else if(collision_geometry.shape) {
        // If we have a shape collider, read the shape and party
        const auto& shape = asset.shapes[*collision_geometry.shape];
        fastgltf::visit_exhaustive(
            fastgltf::visitor{
                [&](const fastgltf::SphereShape& sphere) {
                    shape_settings = new JPH::SphereShapeSettings{sphere.radius};
                },
                [&](const fastgltf::BoxShape& box) {
                    shape_settings = new JPH::BoxShapeSettings{
                        JPH::Vec3{box.size.x(), box.size.y(), box.size.z()}
                    };
                },
                [&](const fastgltf::CapsuleShape& capsule) {
                    if(abs(capsule.radiusBottom - capsule.radiusTop) < eastl::numeric_limits<
                           fastgltf::num>::epsilon()) {
                        shape_settings = new JPH::CapsuleShapeSettings{
                            capsule.height / 2.f, capsule.radiusTop
                        };
                    } else {
                        shape_settings = new JPH::TaperedCapsuleShapeSettings{
                            capsule.height / 2.f, capsule.radiusTop, capsule.radiusBottom
                        };
                    }
                },
                [&](const fastgltf::CylinderShape& cylinder) {
                    const auto radius_top = eastl::max(cylinder.radiusTop, JPH::cDefaultConvexRadius);
                    const auto radius_bottom = eastl::max(cylinder.radiusBottom, JPH::cDefaultConvexRadius);
                    if(abs(radius_bottom - radius_top) < eastl::numeric_limits<
                           fastgltf::num>::epsilon()) {
                        shape_settings = new JPH::CylinderShapeSettings{
                            cylinder.height / 2.f, radius_top
                        };
                    } else {
                        shape_settings = new JPH::TaperedCylinderShapeSettings{
                            cylinder.height / 2.f, radius_top, radius_bottom
                        };
                    }
                },
            },
            shape);
    } else {
        logger->error("Invalid collider!");
    }

    if(shape_settings != nullptr) {
        auto shape_result = shape_settings->Create();
        if(!shape_result.HasError()) {
            return shape_result.Get();
        } else {
            logger->error("Could not create shape: {}", shape_result.GetError());
        }
    }

    return nullptr;
}

entt::handle GltfModel::add_to_world(World& world_in, const eastl::optional<entt::handle>& parent_node) const {
    const auto root_entity = add_nodes_to_world(world_in, parent_node);

    if(extras.player_parent_node != std::numeric_limits<size_t>::max()) {
        const auto& gltf_model_component = world_in.get_registry().get<ImportedModelComponent>(root_entity);
        const auto player_parent_entity = gltf_model_component.node_to_entity.at(extras.player_parent_node);

        player_parent_entity.emplace<PlayerParentComponent>();
    }

    return root_entity;
}

const ExtrasData& GltfModel::get_extras() const {
    return extras;
}

size_t GltfModel::find_node(const eastl::string_view name) const {
    if(const auto itr = eastl::find_if(
        asset.nodes.begin(),
        asset.nodes.end(),
        [&](const fastgltf::Node& node) {
            return eastl::identical(name.begin(), name.end(), node.name.begin(), node.name.end());
        }); itr != asset.nodes.end()) {

        return itr - asset.nodes.begin();
    } else {
        return eastl::numeric_limits<size_t>::max();
    }
}

void GltfModel::validate_model() {
    // This ain't no summer camp. We got rules!
    assert(asset.skins.size() < 2);

    auto num_skinned_nodes = 0;
    for(const auto& node : asset.nodes) {
        if(node.skinIndex) {
            num_skinned_nodes++;
        }
    }

    assert(num_skinned_nodes < 2);
}

void GltfModel::import_resources_for_model(render::SarahRenderer& renderer) {
    ZoneScoped;

    // Upload all buffers and textures to the GPU, maintaining a mapping from glTF resource identifier to resource

    import_skins(Engine::get().get_animation_system());

    import_meshes(renderer);

    import_materials(
        renderer.get_material_storage(),
        renderer.get_texture_loader(),
        render::RenderBackend::get()
        );

    import_animations();

    for(const auto& animation : asset.animations) {
        for(const auto& channel : animation.channels) {
            if(channel.nodeIndex) {
                dynamic_nodes.emplace(*channel.nodeIndex);
            }
        }
    }

    logger->info("Imported resources");
}

static const fastgltf::Sampler default_sampler{};

void GltfModel::import_materials(
    render::MaterialStorage& material_storage, render::TextureLoader& texture_loader, render::RenderBackend& backend
    ) {
    ZoneScoped;

    gltf_material_to_material_handle.clear();
    gltf_material_to_material_handle.reserve(asset.materials.size());

    for(const auto& gltf_material : asset.materials) {
        const auto material_name = !gltf_material.name.empty()
                                       ? gltf_material.name
                                       : "Unnamed material";
        logger->debug("Importing material {}", material_name);

        // Naive implementation creates a separate material for each glTF material
        // A better implementation would have a few pipeline objects that can be shared - e.g. we'd save the
        // pipeline create info and descriptor set layout info, and copy it down as needed

        auto material = render::BasicPbrMaterial{};
        material.name = material_name;

        if(gltf_material.alphaMode == fastgltf::AlphaMode::Opaque) {
            material.transparency_mode = render::TransparencyMode::Solid;
        } else if(gltf_material.alphaMode == fastgltf::AlphaMode::Mask) {
            material.transparency_mode = render::TransparencyMode::Cutout;
        } else if(gltf_material.alphaMode == fastgltf::AlphaMode::Blend) {
            material.transparency_mode = render::TransparencyMode::Translucent;
        }

        material.double_sided = gltf_material.doubleSided;
        material.front_face_ccw = front_face_ccw;
        logger->info("Added material with front_face_ccw={}", material.front_face_ccw);

        material.gpu_data.base_color_tint = glm::vec4(
            glm::make_vec4(gltf_material.pbrData.baseColorFactor.data())
            );
        material.gpu_data.metalness_factor = static_cast<float>(gltf_material.pbrData.metallicFactor);
        material.gpu_data.roughness_factor = static_cast<float>(gltf_material.pbrData.roughnessFactor);
        material.gpu_data.opacity_threshold = gltf_material.alphaCutoff;

        const auto emissive_factor = glm::make_vec3(gltf_material.emissiveFactor.data());
        material.gpu_data.emission_factor = glm::vec4(emissive_factor, 1.f);
        if(length(emissive_factor) > 0) {
            material.emissive = true;
        }

        if(gltf_material.pbrData.baseColorTexture) {
            material.base_color_texture = get_texture(
                gltf_material.pbrData.baseColorTexture->textureIndex,
                render::TextureType::Color,
                texture_loader
                );

            const auto& texture = asset.textures[gltf_material.pbrData.baseColorTexture->textureIndex];
            const auto& sampler = texture.samplerIndex ? asset.samplers[*texture.samplerIndex] : default_sampler;

            material.base_color_sampler = to_vk_sampler(sampler, backend);
        } else {
            material.base_color_texture = backend.get_white_texture_handle();
            material.base_color_sampler = backend.get_default_sampler();
        }

        if(gltf_material.normalTexture) {
            material.normal_texture = get_texture(
                gltf_material.normalTexture->textureIndex,
                render::TextureType::Data,
                texture_loader
                );

            const auto& texture = asset.textures[gltf_material.normalTexture->textureIndex];
            const auto& sampler = texture.samplerIndex ? asset.samplers[*texture.samplerIndex] : default_sampler;

            material.normal_sampler = to_vk_sampler(sampler, backend);
        } else {
            material.normal_texture = backend.get_default_normalmap_handle();
            material.normal_sampler = backend.get_default_sampler();
        }

        if(gltf_material.pbrData.metallicRoughnessTexture) {
            material.metallic_roughness_texture = get_texture(
                gltf_material.pbrData.metallicRoughnessTexture->textureIndex,
                render::TextureType::Data,
                texture_loader
                );

            const auto& texture = asset.textures[gltf_material.pbrData.metallicRoughnessTexture->textureIndex];
            const auto& sampler = texture.samplerIndex ? asset.samplers[*texture.samplerIndex] : default_sampler;

            material.metallic_roughness_sampler = to_vk_sampler(sampler, backend);
        } else {
            material.metallic_roughness_texture = backend.get_white_texture_handle();
            material.metallic_roughness_sampler = backend.get_default_sampler();
        }

        if(gltf_material.emissiveTexture) {
            material.emission_texture = get_texture(
                gltf_material.emissiveTexture->textureIndex,
                render::TextureType::Data,
                texture_loader
                );

            const auto& texture = asset.textures[gltf_material.emissiveTexture->textureIndex];
            const auto& sampler = texture.samplerIndex ? asset.samplers[*texture.samplerIndex] : default_sampler;

            material.emission_sampler = to_vk_sampler(sampler, backend);

            material.emissive = true;
        } else {
            material.emission_texture = backend.get_white_texture_handle();
            material.emission_sampler = backend.get_default_sampler();
        }

        const auto material_handle = material_storage.add_material_instance(std::move(material));

        logger->trace(
            "glTF material {} (index {}) has backend material index {}",
            material_handle->first.name,
            gltf_material_to_material_handle.size(),
            material_handle.index);

        gltf_material_to_material_handle.emplace_back(material_handle);
    }

    logger->info("Imported all materials");
}

void GltfModel::import_meshes(render::SarahRenderer& renderer) {
    ZoneScoped;

    auto& mesh_storage = renderer.get_mesh_storage();

    gltf_primitive_to_mesh.reserve(asset.meshes.size());

    for(const auto& mesh : asset.meshes) {
        // Copy the vertex and index data into the appropriate buffers
        // Interleave the vertex data, because it's easier for me to handle conceptually
        // Maybe eventually profile splitting out positions for separate use

        auto imported_primitives = eastl::vector<render::MeshHandle>{};
        imported_primitives.reserve(mesh.primitives.size());

        auto primitive_idx = 0u;
        for(const auto& primitive : mesh.primitives) {
            const auto vertices = read_vertex_data(primitive, asset);
            const auto indices = read_index_data(primitive, asset);

            const auto mesh_bounds = read_mesh_bounds(primitive, asset);

            auto mesh_maybe = eastl::optional<render::MeshHandle>{};

            if(primitive.findAttribute("WEIGHTS_0") != primitive.attributes.end()) {
                const auto& [bone_ids, weights] = read_skinning_data(primitive, asset);
                mesh_maybe = mesh_storage.add_skeletal_mesh(vertices, indices, mesh_bounds, bone_ids, weights);
            } else {
                mesh_maybe = mesh_storage.add_mesh(vertices, indices, mesh_bounds);
            }

            if(mesh_maybe) {
                imported_primitives.emplace_back(*mesh_maybe);
            } else {
                logger->error(
                    "Could not import mesh primitive {} in mesh {}",
                    primitive_idx,
                    mesh.name.empty() ? "Unnamed mesh" : mesh.name
                    );
            }

            primitive_idx++;
        }

        gltf_primitive_to_mesh.emplace_back(imported_primitives);
    }
}

void GltfModel::import_skins(AnimationSystem& animation_system) {
    ZoneScoped;

    /*
     * We want to import the skins simply. Let's make a list of transforms, with simple parent/child relationships.
     * Blender exports skeletons as the first nodes in a glTF file, let's rely on that and crash horribly in five years
     */

    // I don't want to deal with multiple skins per file. Do not
    assert(asset.skins.size() < 2);

    for(const auto& original_skin : asset.skins) {
        auto skeleton = Skeleton{};
        auto inverse_bind_matrices = eastl::vector<float4x4>{};
        original_skin.inverseBindMatrices.and_then([&](const auto accessor_id) {
            const auto& accessor = asset.accessors.at(accessor_id);
            skeleton.inverse_bind_matrices.resize(accessor.count);

            fastgltf::copyFromAccessor<float4x4>(asset, accessor, skeleton.inverse_bind_matrices.data());
        });

        skeleton.bones.reserve(original_skin.joints.size());
        node_id_to_bone_id.resize(original_skin.joints.size());
        for(const auto& node_id : original_skin.joints) {
            node_id_to_bone_id[node_id] = skeleton.bones.size();

            const auto& node = asset.nodes.at(node_id);
            const auto& node_transform = fastgltf::getTransformMatrix(node);
            auto& bone = skeleton.bones.emplace_back();
            bone.local_transform = glm::make_mat4(node_transform.data());
            bone.children.reserve(node.children.size());
            for(const auto child_idx : node.children) {
                bone.children.emplace_back(child_idx);
            }
        }

        // Fix up children to refer to bones, not nodes
        for(auto& bone : skeleton.bones) {
            for(auto& child_idx : bone.children) {
                child_idx = node_id_to_bone_id.at(child_idx);
            }
        }

        skeleton_handle = animation_system.add_skeleton(eastl::move(skeleton));
    }
}

void GltfModel::import_animations() const {
    auto& animations = Engine::get().get_animation_system();
    for(const auto& gltf_animation : asset.animations) {
        auto animation = Animation{};
        animation.channels.reserve(gltf_animation.channels.size());
        for(const auto& gltf_channel : gltf_animation.channels) {
            // If we're importing a skinned mesh, remap from node ID to bone ID
            auto channel_id = *gltf_channel.nodeIndex;
            if(!node_id_to_bone_id.empty()) {
                channel_id = node_id_to_bone_id.at(*gltf_channel.nodeIndex);
            }

            auto& transform_animation = animation.channels[channel_id];

            // Read in the data for this channel's sampler. In theory this can lead to a bunch of data duplication - in practice I currently do not care
            switch(gltf_channel.path) {
            case fastgltf::AnimationPath::Translation:
                transform_animation.position = read_animation_sampler<float3>(
                    gltf_channel.samplerIndex,
                    gltf_animation,
                    asset);
                break;

            case fastgltf::AnimationPath::Rotation:
                transform_animation.rotation = read_animation_sampler<glm::quat>(
                    gltf_channel.samplerIndex,
                    gltf_animation,
                    asset);
                break;

            case fastgltf::AnimationPath::Scale:
                transform_animation.scale = read_animation_sampler<float3>(
                    gltf_channel.samplerIndex,
                    gltf_animation,
                    asset);
                break;

            case fastgltf::AnimationPath::Weights:
                [[fallthrough]];
            default:
                throw std::runtime_error{"Unsupported animation path!"};
            }
        }

        animations.add_animation(skeleton_handle, eastl::string{gltf_animation.name.c_str()}, std::move(animation));
    }
}

void GltfModel::calculate_bounding_sphere_and_footprint() {
    auto min_extents = glm::vec3{0.f};
    auto max_extents = glm::vec3{0.f};

    traverse_nodes(
        [&](const size_t node_index, const fastgltf::Node& node, const glm::mat4& local_to_world) {
            if(node.meshIndex) {
                const auto& mesh = asset.meshes[*node.meshIndex];
                for(const auto& primitive : mesh.primitives) {
                    const auto mesh_bounds = read_mesh_bounds(primitive, asset);

                    const auto primitive_min_modelspace = local_to_world * glm::vec4{mesh_bounds.min, 1.f};
                    const auto primitive_max_modelspace = local_to_world * glm::vec4{mesh_bounds.max, 1.f};

                    min_extents = glm::min(min_extents, glm::vec3{primitive_min_modelspace});
                    max_extents = glm::max(max_extents, glm::vec3{primitive_max_modelspace});
                }
            }
        },
        float4x4{1.f}
        );

    // Bounding sphere center and radius
    const auto bounding_sphere_center = (min_extents + max_extents) / 2.f;
    const auto bounding_sphere_radius = glm::max(
        length(min_extents - bounding_sphere_center),
        length(max_extents - bounding_sphere_center)
        );

    // Footprint radius (radius along xz plane)
    const auto footprint_radius = glm::max(
        length(
            glm::vec2{min_extents.x, min_extents.z} -
            glm::vec2{
                bounding_sphere_center.x,
                bounding_sphere_center.z
            }
            ),
        length(
            glm::vec2{max_extents.x, max_extents.z} -
            glm::vec2{
                bounding_sphere_center.x,
                bounding_sphere_center.z
            }
            )
        );
    bounding_sphere = glm::vec4{bounding_sphere_center, bounding_sphere_radius};

    logger->info(
        "Bounding sphere: Center=({}, {}, {}) radius={}",
        bounding_sphere.x,
        bounding_sphere.y,
        bounding_sphere.z,
        bounding_sphere.w
        );
    logger->info("Footprint radius: {}", footprint_radius);
}

render::TextureHandle GltfModel::get_texture(
    const size_t gltf_texture_index, const render::TextureType type,
    render::TextureLoader& texture_storage
    ) {
    if(gltf_texture_to_texture_handle.find(gltf_texture_index) == gltf_texture_to_texture_handle.end()) {
        import_single_texture(gltf_texture_index, type, texture_storage);
    }

    return gltf_texture_to_texture_handle[gltf_texture_index];
}

void GltfModel::import_single_texture(
    const size_t gltf_texture_index, const render::TextureType type,
    render::TextureLoader& texture_storage
    ) {
    ZoneScoped;

    const auto& gltf_texture = asset.textures[gltf_texture_index];
    auto image_index = std::size_t{};
    if(gltf_texture.basisuImageIndex) {
        image_index = *gltf_texture.basisuImageIndex;
    } else {
        image_index = *gltf_texture.imageIndex;
    }

    const auto& image = asset.images[image_index];

    auto image_data = eastl::vector<std::byte>{};
    auto image_name = ResourcePath{ResourcePath::Scope::File, image.name};
    auto mime_type = fastgltf::MimeType::None;

    std::visit(
        Visitor{
            [&](const auto&) {
                /* I'm just here so I don't get a compiler error */
            },
            [&](const fastgltf::sources::BufferView& buffer_view) {
                const auto& real_buffer_view = asset.bufferViews[buffer_view.bufferViewIndex];
                const auto& buffer = asset.buffers[real_buffer_view.bufferIndex];
                const auto* buffer_vector = std::get_if<fastgltf::sources::Array>(&buffer.data);
                auto* data_pointer = buffer_vector->bytes.data();
                data_pointer += real_buffer_view.byteOffset;
                image_data = {data_pointer, data_pointer + real_buffer_view.byteLength};
                mime_type = buffer_view.mimeType;
            },
            [&](const fastgltf::sources::URI& file_path) {
                const auto uri = file_path.uri.string();

                logger->info("Loading texture {}", uri);

                // Try to load a KTX version of the texture
                const auto texture_filepath = filepath.to_filepath().parent_path() / std::filesystem::path{uri};
                auto ktx_texture_filepath = texture_filepath;
                ktx_texture_filepath.replace_extension("ktx2");
                const auto ktx_resource_path = ResourcePath{ResourcePath::Scope::File, ktx_texture_filepath.string()};
                auto data_maybe = SystemInterface::get().load_file(ktx_resource_path);
                if(data_maybe) {
                    image_data = std::move(*data_maybe);
                    image_name = ktx_resource_path;
                    mime_type = fastgltf::MimeType::KTX2;
                    return;
                }

                const auto texture_resource_path = ResourcePath{ResourcePath::Scope::File, texture_filepath.string()};
                data_maybe = SystemInterface::get().load_file(texture_resource_path);
                if(data_maybe) {
                    image_data = std::move(*data_maybe);
                    image_name = texture_resource_path;
                    const auto& extension = texture_filepath.extension();
                    if(extension == ".png") {
                        mime_type = fastgltf::MimeType::PNG;
                    } else if(extension == ".jpg" || extension == ".jpeg") {
                        mime_type = fastgltf::MimeType::JPEG;
                    }
                    return;
                }

                throw std::runtime_error{fmt::format("Could not load image {}", texture_filepath.string())};
            },
            [&](const fastgltf::sources::Array& vector_data) {
                image_data.resize(vector_data.bytes.size());
                std::memcpy(image_data.data(), vector_data.bytes.data(), vector_data.bytes.size() * sizeof(std::byte));
                mime_type = vector_data.mimeType;
            }
        },
        image.data
        );

    auto handle = eastl::optional<render::TextureHandle>{};
    if(mime_type == fastgltf::MimeType::PNG || mime_type == fastgltf::MimeType::JPEG) {
        handle = texture_storage.upload_texture_stbi(image_name, image_data, type);
    }

    if(handle) {
        gltf_texture_to_texture_handle.emplace(gltf_texture_index, *handle);
    } else {
        throw std::runtime_error{fmt::format("Could not load image {}", image_name)};
    }
}

VkSampler GltfModel::to_vk_sampler(const fastgltf::Sampler& sampler, const render::RenderBackend& backend) {
    auto create_info = VkSamplerCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .maxLod = VK_LOD_CLAMP_NONE,
    };

    if(sampler.minFilter) {
        switch(*sampler.minFilter) {
        case fastgltf::Filter::Nearest:
            create_info.minFilter = VK_FILTER_NEAREST;
            create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            break;

        case fastgltf::Filter::Linear:
            create_info.minFilter = VK_FILTER_LINEAR;
            break;

        case fastgltf::Filter::NearestMipMapNearest:
            create_info.minFilter = VK_FILTER_NEAREST;
            create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            break;

        case fastgltf::Filter::LinearMipMapNearest:
            create_info.minFilter = VK_FILTER_LINEAR;
            create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            break;

        case fastgltf::Filter::NearestMipMapLinear:
            create_info.minFilter = VK_FILTER_NEAREST;
            create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            break;

        case fastgltf::Filter::LinearMipMapLinear:
            create_info.minFilter = VK_FILTER_LINEAR;
            create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        }
    }

    if(sampler.magFilter) {
        switch(*sampler.magFilter) {
        case fastgltf::Filter::Nearest:
            create_info.magFilter = VK_FILTER_NEAREST;
            break;

        case fastgltf::Filter::Linear:
            create_info.magFilter = VK_FILTER_LINEAR;
            break;

        default:
            logger->error("Invalid texture mag filter");
        }
    }

    switch(sampler.wrapS) {
    case fastgltf::Wrap::Repeat:
        create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        break;

    case fastgltf::Wrap::ClampToEdge:
        create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        break;

    case fastgltf::Wrap::MirroredRepeat:
        create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        break;
    }

    switch(sampler.wrapT) {
    case fastgltf::Wrap::Repeat:
        create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        break;

    case fastgltf::Wrap::ClampToEdge:
        create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        break;

    case fastgltf::Wrap::MirroredRepeat:
        create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        break;
    }

    if(create_info.mipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR) {
        create_info.anisotropyEnable = VK_TRUE;
        create_info.maxAnisotropy = 8;
    }

    return backend.get_global_allocator().get_sampler(create_info);
}

eastl::pair<JPH::VertexList, JPH::IndexedTriangleList> GltfModel::read_collision_mesh_from_node(
    const size_t node_idx
    ) const {
    ZoneScoped;
    const auto& node = asset.nodes[node_idx];
    if(!node.meshIndex) {
        throw std::runtime_error{
            fmt::format("Node {} is used as a mesh collider, but it does not have a mesh!", node_idx)
        };
    }

    const auto& mesh = asset.meshes[*node.meshIndex];

    JPH::VertexList vertex_positions;
    JPH::IndexedTriangleList indices;

    for(const auto& primitive : mesh.primitives) {
        const auto* positions_attribute = primitive.findAttribute("POSITION");

        // Meshes MUST have positions

        const auto& positions_accessor = asset.accessors[positions_attribute->accessorIndex];

        const auto first_vertex = static_cast<uint32_t>(vertex_positions.size());
        vertex_positions.resize(vertex_positions.size() + positions_accessor.count);
        auto* write_ptr = vertex_positions.begin() + first_vertex;

        fastgltf::copyFromAccessor<fastgltf::math::fvec3>(asset, positions_accessor, write_ptr);

        assert(vertex_positions.size() < eastl::numeric_limits<uint32_t>::max());

        // Also read indices. Offset them to refer to our merged vertex buffer
        if(primitive.indicesAccessor) {
            const auto& indices_accessor = asset.accessors[*primitive.indicesAccessor];
            indices.reserve(indices.size() + (indices_accessor.count / 3));
            JPH::IndexedTriangle cur_triangle;

            fastgltf::iterateAccessorWithIndex<uint32_t>(
                asset,
                indices_accessor,
                [&](const uint32_t index, const size_t i) {
                    if(i > 0 && i % 3 == 0) {
                        // Add the current triangle to the list, reset it
                        indices.emplace_back(cur_triangle);
                        cur_triangle = {};
                    }

                    cur_triangle.mIdx[i % 3] = index + first_vertex;
                });
        } else {
            throw std::runtime_error{"Mesh has no indices! I literally can not even"};
        }
    }

    return {vertex_positions, indices};
}

eastl::vector<StandardVertex> read_vertex_data(const fastgltf::Primitive& primitive, const fastgltf::Asset& model) {
    // Get the first attribute's index_count. All the attributes must have the same number of elements, no need to get all their counts
    const auto positions_index = primitive.findAttribute("POSITION")->accessorIndex;
    const auto& positions_accessor = model.accessors[positions_index];
    const auto num_vertices = positions_accessor.count;

    auto vertices = eastl::vector<StandardVertex>();
    vertices.resize(
        num_vertices,
        StandardVertex{
            .position = glm::vec3{},
            .normal = glm::vec3{0, 0, 1},
            .tangent = glm::vec4{1, 0, 0, 1},
            .texcoord = {},
            .color = glm::packUnorm4x8(glm::vec4{1, 1, 1, 1}),
        }
        );

    copy_vertex_data_to_vector(primitive, model, vertices.data());

    return vertices;
}

eastl::vector<uint32_t> read_index_data(const fastgltf::Primitive& primitive, const fastgltf::Asset& model) {
    const auto& index_accessor = model.accessors[*primitive.indicesAccessor];
    const auto num_indices = index_accessor.count;

    auto indices = eastl::vector<uint32_t>{};
    indices.resize(num_indices);

    auto* const index_write_ptr = indices.data();

    const auto& index_buffer_view = model.bufferViews[*index_accessor.bufferViewIndex];
    const auto& index_buffer = model.buffers[index_buffer_view.bufferIndex];
    const auto* index_buffer_data = std::get_if<fastgltf::sources::Array>(&index_buffer.data);
    const auto* index_ptr_u8 = index_buffer_data->bytes.data() + index_buffer_view.byteOffset + index_accessor.
                               byteOffset;

    if(index_accessor.componentType == fastgltf::ComponentType::UnsignedShort) {
        const auto* index_read_ptr = reinterpret_cast<const uint16_t*>(index_ptr_u8);

        for(auto i = 0u; i < index_accessor.count; i++) {
            index_write_ptr[i] = index_read_ptr[i];
        }
    } else {
        memcpy(index_write_ptr, index_ptr_u8, index_accessor.count * sizeof(uint32_t));
    }

    return indices;
}

Box read_mesh_bounds(const fastgltf::Primitive& primitive, const fastgltf::Asset& model) {
    const auto position_attribute_idx = primitive.findAttribute("POSITION")->accessorIndex;
    const auto& position_accessor = model.accessors[position_attribute_idx];
    // glTF spec says that the min and max of a position accessor must exist
    const auto min = glm::make_vec3(position_accessor.min->data<double>());
    // NOLINT(bugprone-unchecked-optional-access)
    const auto max = glm::make_vec3(position_accessor.max->data<double>());
    // NOLINT(bugprone-unchecked-optional-access)

    return {.min = min, .max = max};
}

eastl::tuple<eastl::vector<u16vec4>, eastl::vector<float4> > read_skinning_data(
    const fastgltf::Primitive& primitive, const fastgltf::Asset& asset
    ) {
    const auto weights_index = primitive.findAttribute("WEIGHTS_0")->accessorIndex;
    const auto bone_ids_index = primitive.findAttribute("JOINTS_0")->accessorIndex;

    const auto& weights_accessor = asset.accessors[weights_index];
    const auto& bone_ids_accessor = asset.accessors[bone_ids_index];

    auto bone_ids = eastl::vector<u16vec4>{weights_accessor.count};
    auto weights = eastl::vector<float4>{weights_accessor.count};

    fastgltf::copyFromAccessor<u16vec4>(asset, bone_ids_accessor, bone_ids.data());
    fastgltf::copyFromAccessor<float4>(asset, weights_accessor, weights.data());

    return {bone_ids, weights};
}

void copy_vertex_data_to_vector(
    const fastgltf::Primitive& primitive,
    const fastgltf::Asset& model,
    StandardVertex* vertices
    ) {
    ZoneScoped;

    if(const auto position_attribute = primitive.findAttribute("POSITION");
        position_attribute != primitive.attributes.end()) {
        const auto& attribute_accessor = model.accessors[position_attribute->accessorIndex];

        fastgltf::iterateAccessorWithIndex<glm::vec3>(
            model,
            attribute_accessor,
            [&](const glm::vec3& position, const size_t idx) {
                vertices[idx].position = position;
            });
    }

    if(const auto normal_attribute = primitive.findAttribute("NORMAL");
        normal_attribute != primitive.attributes.end()) {
        const auto& attribute_accessor = model.accessors[normal_attribute->accessorIndex];

        fastgltf::iterateAccessorWithIndex<glm::vec3>(
            model,
            attribute_accessor,
            [&](const glm::vec3& normal, const size_t idx) {
                vertices[idx].normal = normal;
            });
    }

    if(const auto tangent_attribute = primitive.findAttribute("TANGENT");
        tangent_attribute != primitive.attributes.end()) {
        const auto& attribute_accessor = model.accessors[tangent_attribute->accessorIndex];

        fastgltf::iterateAccessorWithIndex<glm::vec4>(
            model,
            attribute_accessor,
            [&](const glm::vec4& tangent, const size_t idx) {
                vertices[idx].tangent = tangent;
                front_face_ccw = tangent.w > 0;
            });
    }

    if(const auto texcoord_attribute = primitive.findAttribute("TEXCOORD_0");
        texcoord_attribute != primitive.attributes.end()) {
        const auto& attribute_accessor = model.accessors[texcoord_attribute->accessorIndex];

        fastgltf::iterateAccessorWithIndex<glm::vec2>(
            model,
            attribute_accessor,
            [&](const glm::vec2& texcoord, const size_t idx) {
                vertices[idx].texcoord = texcoord;
            });
    }

    const auto color_attribute = primitive.findAttribute("COLOR_0");
    if(color_attribute != primitive.attributes.end()) {
        const auto& attribute_accessor = model.accessors[color_attribute->accessorIndex];

        fastgltf::iterateAccessorWithIndex<glm::u8vec4>(
            model,
            attribute_accessor,
            [&](const glm::u8vec4& color, const size_t idx) {
                const auto color_vec = float4{color} / 255.f;
                vertices[idx].color = glm::packUnorm4x8(color_vec);
            });
    }

    // TODO: Other texcoord channels
}

template<typename DataType>
AnimationTimeline<DataType> read_animation_sampler(
    const size_t sampler_index, const fastgltf::Animation& animation, const fastgltf::Asset& asset
    ) {
    AnimationTimeline<DataType> spline;

    const auto& sampler = animation.samplers.at(sampler_index);

    // Read times
    const auto& timestamps_accessor = asset.accessors[sampler.inputAccessor];
    spline.timestamps.resize(timestamps_accessor.count);
    fastgltf::copyFromAccessor<float>(asset, timestamps_accessor, spline.timestamps.data());

    // Read data
    const auto& data_accessor = asset.accessors[sampler.outputAccessor];
    spline.values.resize(data_accessor.count);
    fastgltf::copyFromAccessor<DataType>(asset, data_accessor, spline.values.data());

    return spline;
}
