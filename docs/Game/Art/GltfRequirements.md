# glTF Requirements

We have a few requirements for glTF files

Mostly about skeletons

A glTF file may have at most one skin, and one skinned node. THe nodes in the skin MUST be the first nodes in the nodes array

See GltfModel::validate_model() for more
