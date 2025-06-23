#pragma once

class Engine;
/**
 * Represents a game. Contains code for things like loading the initial level. Will certainly expand
 */
class GameInstance
{
public:
    virtual ~GameInstance() = default;

    /**
     * Register the game's types with the reflection subsystem
     *
     * @see reflection::ReflectionSubsystem::register_types
     */
    virtual void register_reflection_types() {};
};

