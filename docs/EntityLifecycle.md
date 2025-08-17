# Entity Lifecycle

Entities have a few distinct lifecycle phases

First, they're constructed. Individual components may have constructors that run. These should make no assumptions about the world outside of the component - the engil will exist, but don't assume that your entity already has a certain component

Then, they're awoken. The entity gets an AwakeLifePhaseComponent (name TBD). Individual systems can process those components however they need, preferably using reactive storages to know what's awoken. At this point the entity is fully constructed, and the scene that the entity belongs to has been fully loaded

Waking up might involve something like enemies getting a reference to the player, or other cross-entity communication

After that, they're ticking. This is the state that most entities will be in for most of the time. Systems can do whatever, there's no rules

Finally, entities are destroyed. Systems are expected to listen for component destruction and handle it as needed. They should not make assumptions about the order components are destroyed in
