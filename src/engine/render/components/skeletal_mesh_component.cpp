#include "skeletal_mesh_component.hpp"

namespace render {
    void SkeletalMeshComponent::propagate_bone_transforms() {
        for(const auto bone_idx : skeleton->root_bones) {
            propagate_bone_transform(bone_idx, float4x4{1.f});
        }
    }

    void SkeletalMeshComponent::propagate_bone_transform(const size_t bone_idx, const float4x4& parent_matrix) {
        const auto& bone = bones.at(bone_idx);
        const auto& worldspace_bone_matrix = parent_matrix * bones.at(bone_idx).local_transform;

        for(const auto child_idx : bone.children) {
            propagate_bone_transform(child_idx, worldspace_bone_matrix);
        }

        worldspace_bone_matrices[bone_idx] = worldspace_bone_matrix * skeleton->inverse_bind_matrices.at(bone_idx);
    }
} // render