# Scenes

Scenes represent a group of objects in the world. The engine expects the main scene, called `environment.sscene`, to exist and be loaded at startup. This contains objects that should alwyas be loaded - terrain, city walls, distant models, sun and sky, etc. We can also have different scenes for different scenarios. Current intent (as of 2025-07-12) is that we load in other scenes for the gameplay elements of different levels. The temple, harbor, and mansion levels will all have their own scenes, which will contain the high-detail models and gamepaly objects needed for that level

There's currently no consideration given to streaming. If truly needed we can implement automatic texture streaming or make objects stop ticking after some distance from the player, but I suspect that won't be necessary

One scene for the environment, one scene for each level

Scenes have a list of SceneObjects, which specify a file to load, a transform to place the root of that file, and a handle to the entity that represents the object in the world. Scene objects may be a gltf model, a Godot scene, or a prefab - hopefully we'll use Godot scenes less and less as dev continues

We don't automatically add SceneObjects to the world - you have to either specify a flag when loading a SceneObject, or tell the scene to add all its things to the world. This is useful....

All scenes are stored in data/game/scenes

# World 

The world is all the objects that have been loaded by all the scenes, plus all entities that have been created at runtime. It's everything. 
