#pragma once

#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/unordered_map.h>

#include "reflection/debug_drawer.hpp"


/**
 * Various things that help with reflection. We got serializers, deserializers, debug UI editors, all you could want!
 */
class ReflectionSubsystem {
public:
    void register_types();

};
