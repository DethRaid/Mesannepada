#include "reflection_subsystem.hpp"

#include "scene/transform_component.hpp"

using namespace entt;

// Based on https://gist.github.com/JuanDiegoMontoya/f6002350a9f5e64c962ee52d7e879922
void ReflectionSubsystem::register_types() {
    // Reset the system (just in case)
    entt::meta_reset();

    // Register our basic types
    entt::meta_factory<float>().func<&draw_editor_float>("draw_editor"_hs);

    // And our component types
    entt::meta_factory<TransformComponent>()
        .data<&TransformComponent::local_to_parent, entt::as_ref_t>("local_to_parent"_hs)
        .custom<PropertiesMap>(PropertiesMap{{"name"_hs, "local_to_parent"}})
        .traits(Traits::Editor)
        .data<&TransformComponent::children, entt::as_ref_t>("children"_hs)
        .custom<PropertiesMap>(PropertiesMap{{"name"_hs, "children"}})
        .traits(Traits::Editor);
}
