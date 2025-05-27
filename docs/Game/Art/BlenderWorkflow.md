# Blender Modelling Worlflow

Here's some notes on the modelling workflow I use

I split things up into different files based on... vibes, really. I have one file for Ur's environment, one for the ziggurat comples, one for the Nannar courtyard, one for some other areas. I guess we generally want to make different files for each reusable asset, and for different zones we want to load individually

In each file, put the meshes we care about reusing in a separate collecction. Name it SM_Something. Then, in the file we use to put everything together, link that collection into the file. This allows us to move the collection around and party

I use Godot to assemble models into larger assets. I save assembled assets as Godot scenes, and load them as-is directly. This lets us avoid using the incomplete glTF Reference format

Animated meshes should be modeled with their animatable parts at their maximum extents. This means the T-pose for animated characters
