#include "transform_component.hpp"

#include <glm/gtx/matrix_decompose.hpp>

float4x4 TransformComponent::get_local_to_world() const {
    return cached_parent_to_world * get_local_to_parent();
}

float4x4 TransformComponent::get_local_to_parent() const {
    return glm::translate(float4x4{1.f}, location) * glm::mat4{rotation} * glm::scale(scale);
}

void TransformComponent::set_local_transform(const float4x4& transform) {
    glm::vec3 skew;
    glm::vec4 perspective;
    glm::decompose(transform, scale, rotation, location, skew, perspective);
}

TransformComponent TransformComponent::from_json(
        const simdjson::simdjson_result<simdjson::ondemand::value>& component_definition) {
    return TransformComponent{};
}
