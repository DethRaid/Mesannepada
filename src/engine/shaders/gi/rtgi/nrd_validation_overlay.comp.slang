Texture2D src_tex;

RWTexture2D dst_tex;

[vk::push_constant]
cbuffer Constants {
    uint2 resolution;
}

[numthreads(8, 8, 1)]
[shader("compute")]
void main(uint2 thread_id : SV_DispatchThreadID) {
    if(any(thread_id >= resolution)) {
        return;
    }

    const uint2 src_pixel = uint2(thread_id.x, resolution.y - thread_id.y);

    const float4 src = src_tex[src_pixel];
    const float4 dst = dst_tex[thread_id];
    dst_tex[thread_id] = lerp(dst, src * 0.5, src.a);
}
