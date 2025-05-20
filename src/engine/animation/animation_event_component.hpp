#pragma once

#include <EASTL/string.h>

/**
 * Event component that tells a GameObject to plat an event
 *
 * This should be added to the GameObject's root entity, then a GameObject's tick function can check for this component
 * and process it as needed
 */
struct AnimationEventComponent {
    eastl::string animation_to_play;
};
