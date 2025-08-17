# Drawing Meshes

SAH uses indirect rendering. It populates the indirect draw commands buffer based on the results of frustum and visibility culling. Each draw command uses the base instance to index into a buffer of primitive IDs, and uses the primitive ID to index into the primitive data buffer. The primitive data constains pointers to the vertex positions and data, and material information
