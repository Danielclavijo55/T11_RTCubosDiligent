#include "structures.fxh"
ConstantBuffer<Constants> g_ConstantsCB;
[shader("miss")]
void main(inout PrimaryRayPayload payload)
{
    const float3 Palette[] = {
        float3(0.25, 0.25, 0.25), // Dark gray
        float3(0.35, 0.35, 0.35),
        float3(0.45, 0.45, 0.45),
        float3(0.55, 0.55, 0.55),
        float3(0.65, 0.65, 0.65),
        float3(0.75, 0.75, 0.75)  // Light gray
    };
    // Generate sky color.
    float factor  = clamp((WorldRayDirection().y + 0.5) / 1.5 * 4.0, 0.0, 4.0);
    int   idx     = floor(factor);
          factor -= float(idx);
    float3 color  = lerp(Palette[idx], Palette[idx+1], factor);
    payload.Color = color;
    //payload.Depth = RayTCurrent(); // bug in DXC for SPIRV
    payload.Depth = g_ConstantsCB.ClipPlanes.y;
}