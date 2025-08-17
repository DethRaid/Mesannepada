# Various game objects in the game

## Static meshes

Meshes that exist in the level and cannot be interacted with. This'll be the majority of our environments. Some of these will ahve collision, some won't need it

## Enemies

Enemies patrol the levels and can attack the player. They'll need a perception system, and some kind of combat mechanics

## Carryable objects

These are things that you can pick up, carry around, throw, and place down. They can never be added to your inventory

## Objects you can add to your inventory

These object lie around the world. You can pick them up, at which point they're removed from the world and are added to your inventory

## Objects you can interact with but not move

Doors and similar. You can interact with them - they might play an animation, change collision geometry, add items to your inventory, etc. 
