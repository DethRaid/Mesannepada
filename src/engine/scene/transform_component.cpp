#include "transform_component.hpp"

float4x4 TransformComponent::get_local_to_world() const {
    return cached_parent_to_world * local_to_parent;
}

TransformComponent TransformComponent::from_json(
        const simdjson::simdjson_result<simdjson::ondemand::value>& component_definition) {
    return TransformComponent{};
}
