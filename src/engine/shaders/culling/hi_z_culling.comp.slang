/**
 * Hi-Z culling shader
 * 
 * Can cull objects to a frustum and depth pyramid in one go
 */

// Culling adapted from https://vkguide.dev/docs/gpudriven/compute_culling/

#include "shared/primitive_data.hpp"
#include "shared/view_data.hpp"

Sampler2D textures[];

[[vk::push_constant]]
cbuffer Constants {
    PrimitiveDataGPU* primitive_datas;
    uint* last_frame_visibility;
    uint* newly_visible_objects;
    uint* this_frame_visibility;
    ViewDataGPU* view_data;

    uint num_primitives;
    uint hi_z_texture_index;
    float2 hi_z_size;
    float num_mips;
};

// TODO: We add the sphere's radius to its z in a few places, the math I copied says we should subtract (and logic
// says we should subtract). Not sure what's going on, something with math

// 2D Polyhedral Bounds of a Clipped, Perspective-Projected 3D Sphere. Michael Mara, Morgan McGuire. 2013
bool projectSphere(float3 center, float radius, float znear, float P00, float P11, out float4 aabb) {
    if(center.z < radius + znear) {
        return false;
    }

    float2 cx = -center.xz;
    float2 vx = float2(sqrt(dot(cx, cx) - radius * radius), radius);
    float2 minx = mul(float2x2(vx.x, vx.y, -vx.y, vx.x), cx);
    float2 maxx = mul(float2x2(vx.x, -vx.y, vx.y, vx.x), cx);

    float2 cy = -center.yz;
    float2 vy = float2(sqrt(dot(cy, cy) - radius * radius), radius);
    float2 miny = mul(float2x2(vy.x, vy.y, -vy.y, vy.x), cy);
    float2 maxy = mul(float2x2(vy.x, -vy.y, vy.y, vy.x), cy);

    aabb = float4(minx.x / minx.y * P00, miny.x / miny.y * P11, maxx.x / maxx.y * P00, maxy.x / maxy.y * P11);
    aabb = aabb.xwzy * float4(0.5f, -0.5f, 0.5f, -0.5f) + 0.5f; // clip space -> uv space

    return true;
}

bool is_in_frustum(const float3 center, const float radius) {
    bool visible = true;

    // frustum culling

    // Multiply the center of the bounding sphere by the plane equation for each frustum plane, test if the result is
    // less than the bounding sphere's radius
    // The `abs` in the equation lets us test the left/right and up/down planes at once
    // We omit some components of the plane equation for each plane, because they're always 0
    visible = visible && center.z * view_data.frustum[1] - abs(center.x) * view_data.frustum[0] > -radius;
    visible = visible && center.z * view_data.frustum[3] - abs(center.y) * view_data.frustum[2] > -radius;

    visible = visible && -center.z + radius > view_data.z_near;

    return visible;
}

bool is_occluded(const float3 center, const float radius) {
    // Flip z because stinky
    const float3 corrected_center = center * float3(1, 1, -1);

    // If the sphere overlaps with the near plane, we get NaNs. Avoid that issue
    if(abs(corrected_center.z) < radius) {
        return false;
    }

    float4 aabb;
    const bool success = projectSphere(corrected_center, radius, view_data.z_near, view_data.projection[0][0], view_data.projection[1][1], aabb);
    if(!success) {
        return false;
    }

    // Reversed AABB order because... reasons. If I use the order vkguide uses the AABB size is negative and I sample
    // HiZ MIP NaN, if I use this order then the AABB size is positive and I sample a reasonable MIP level

    float width = (aabb.x - aabb.z) * hi_z_size.x;
    float height = (aabb.y - aabb.w) * hi_z_size.y;

    // Mip level of the depth pyramid to compare against
    float level = floor(log2(max(width, height)));
    level = min(level, num_mips - 1);

    float depth = textures[hi_z_texture_index].SampleLevel((aabb.xy + aabb.zw) * 0.5f, level).r;

    float depth_of_sphere = view_data.z_near / (corrected_center.z + radius);

    return depth_of_sphere < depth;
}

void mark_invisible(uint primitive_id) {
    newly_visible_objects[primitive_id] = 0;
    this_frame_visibility[primitive_id] = 0;
}

[shader("compute")]
[numthreads(96, 1, 1)]
void main(uint primitive_id : SV_DispatchThreadID) {
    if(primitive_id >= num_primitives) {
        return;
    }    

    PrimitiveDataGPU instance_to_cull = primitive_datas[primitive_id];
    if((instance_to_cull.flags & PRIMITIVE_FLAG_ENABLED) == 0) {
        mark_invisible(primitive_id);
        return;
    }

    float3 center = (instance_to_cull.bounds_max.xyz + instance_to_cull.bounds_min_and_radius.xyz) * 0.5;
    center = mul(view_data.view, float4(center, 1)).xyz;
    float radius = instance_to_cull.bounds_min_and_radius.w;

    if(!is_in_frustum(center, radius)) {
        mark_invisible(primitive_id);
        return;
    }
        
    // Something is wrong here... I don't know what, and I have enough performance overhead to not worry about it
    // if(is_occluded(center, radius)) {
    //     mark_invisible(primitive_id);
    //     return;
    // }
 
    const uint visible_last_frame = last_frame_visibility[primitive_id];
    const uint newly_visible = visible_last_frame == 0 ? 1 : 0;
    newly_visible_objects[primitive_id] = newly_visible;
    this_frame_visibility[primitive_id] = 1;
}
