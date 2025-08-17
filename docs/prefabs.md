We have prefabs, and we have a way to instantiate them in the scene

Prefabs have components. Some components are defined in a glTF file. StaticMeshComponent, SkeletalMeshComponent, CameraComponent, CollisionComponent, TriggerComponent, etc. This data can be represented directly in a glTF file

Other components are not implicit. PlayerControllerComponet is one such example. That one is added in code, when we load The Player File

And then we have components that aren't defined in code, nor are they implicit. 

We have a way to spawn prefabs in a scene. We place a MarkerNode in Godot, and give it a metadata called `spawn_gameobject`. This will appear in the `.tscn` as `metadata/spawn_gameobject`. The value of this metadata should be the path of the prefab to spawn, relative to the working directory
