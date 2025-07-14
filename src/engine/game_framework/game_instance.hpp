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
    virtual void register_reflection_types() {}

    /**
     * Ticks the game. This should call game-specific systems, such as updating the player's class or other game types
     */
    virtual void tick(float delta_time) {}
};

