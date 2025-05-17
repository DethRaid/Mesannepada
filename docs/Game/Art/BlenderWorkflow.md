# Blender Modelling Worlflow

Here's some notes on the modelling workflow I use

I split things up int dofferent files based on... vibes, really. I have one file for Ur's environment, one for the ziggurat comples, one for the Nannar courtyard, one for some other areas. I guess we generally want to make different files for each reusable asset, and for different zones we want to load individually

In each file, put the meshes we care about reusing in a separate collecction. Name it SM_Something. Then, in the file we use to put everything together, link that collection into the file. This allows us to move the collection around and party

The current wat the cross-file references work is based on custom properties. For every object that's linked into the file, add an empty and give it a custom property. The custom property should be called `file_reference`, and its value should be the path to the file (e.g. data/game/whatever)

Ther's probably a better way to do this with an upgrade to Blender's glTF exporter. Maybe they'll support the glTF External Reference format whenever it gets done smh
