struct VertexOut {
    half4 sh;
}

[shader("fragment")]
half4 main(VertexOut vertex) {
    return vertex.sh;
}
