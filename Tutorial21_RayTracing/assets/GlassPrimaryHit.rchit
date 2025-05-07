// CubePrimaryHit.rchit
// Material id per instance:
//   0 = glass
//   1 = diffuse
//   2 = metal

#include "structures.fxh"
#include "RayUtils.fxh"

ConstantBuffer<CubeAttribs> g_CubeAttribsCB;

#define MAT_GLASS   0
#define MAT_DIFFUSE 1
#define MAT_METAL   2

// -----------------------------------------------------------------------------
// helpers
// -----------------------------------------------------------------------------
float3 FresnelSchlick(float3 F0, float cosTheta)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float3 Lambert(float3 albedo, float3 N)
{
    float3 L     = normalize(g_ConstantsCB.LightPos[0].xyz - WorldRayOrigin());
    float  NdotL = max(dot(N, L), 0.0);
    return albedo * NdotL * g_ConstantsCB.LightColor[0].rgb;
}

float3 Blend(float3 baseC, float3 reflC, float k)
{
    return lerp(baseC, reflC, k);
}

// -----------------------------------------------------------------------------
// materials
// -----------------------------------------------------------------------------
float3 ShadeGlass(float3 N, inout PrimaryRayPayload payload)
{
    const float AirIOR   = 1.0;
    const float GlassIOR = 1.5;

    float3 V      = WorldRayDirection();
    bool   front  = (HitKind() == HIT_KIND_TRIANGLE_FRONT_FACE);
    float3 norm   = front ? N : -N;
    float  relIOR = front ? (AirIOR / GlassIOR) : (GlassIOR / AirIOR);

    float3 T = refract(V, norm, relIOR);

    float  cosNI = dot(V, -norm);

    /* replicate the scalar into a float3 so the compiler
       gets three components and does not complain */
    float  F0s = pow((GlassIOR - AirIOR) / (GlassIOR + AirIOR), 2.0);
    float3 F0  = float3(F0s, F0s, F0s);

    float  F   = FresnelSchlick(F0, cosNI).r;   // any channel

    RayDesc ray;
    ray.TMin = SMALL_OFFSET;
    ray.TMax = 100.0;

    // reflection
    ray.Origin    = WorldRayOrigin() + V * RayTCurrent() + norm * SMALL_OFFSET;
    ray.Direction = reflect(V, norm);
    float3 refl   = CastPrimaryRay(ray, payload.Recursion + 1).Color;

    // refraction
    float3 refr = 0.0;
    if (F < 1.0)
    {
        ray.Origin    = WorldRayOrigin() + V * RayTCurrent();
        ray.Direction = T;
        refr          = CastPrimaryRay(ray, payload.Recursion + 1).Color;
    }

    return Blend(refr, refl, F);
}

static float Rand01(uint seed)
{
    float x = sin(float(seed) * 12.9898 + 78.233) * 43758.5453;
    return frac(x);
}



float3 ShadeDiffuse(float3 N)
{   
    uint instId = InstanceIndex();
    float r = Rand01(instId+0);
    float g = Rand01(instId+1);
    float b = Rand01(instId+2);
    
    
    const float3 Albedo = float3(r, g, b);
    float3 ambient = g_ConstantsCB.AmbientColor.rgb * Albedo;



    return Lambert(Albedo, N) + ambient;
}

float3 ShadeMetal(float3 N, inout PrimaryRayPayload payload)
{
    /* F0 for something gold like */
    const float3 F0 = float3(0.95, 0.93, 0.88);

    float3 V   = WorldRayDirection();
    float  cos = saturate(dot(-V, N));
    float3 F   = FresnelSchlick(F0, cos);

    RayDesc ray;
    ray.TMin      = SMALL_OFFSET;
    ray.TMax      = 100.0;
    ray.Origin    = WorldRayOrigin() + V * RayTCurrent() + N * SMALL_OFFSET;
    ray.Direction = reflect(V, N);

    float3 refl = CastPrimaryRay(ray, payload.Recursion + 1).Color;
    return refl * F;
}

// -----------------------------------------------------------------------------
// closest-hit
// -----------------------------------------------------------------------------
[shader("closesthit")]
void main(inout PrimaryRayPayload payload,
          in BuiltInTriangleIntersectionAttributes attr)
{
    /* interpolate normal in world space */
    float3 bary = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y,
                         attr.barycentrics.x,
                         attr.barycentrics.y);

    uint3 tri = g_CubeAttribsCB.Primitives[PrimitiveIndex()].xyz;

    float3 N = g_CubeAttribsCB.Normals[tri.x] * bary.x +
               g_CubeAttribsCB.Normals[tri.y] * bary.y +
               g_CubeAttribsCB.Normals[tri.z] * bary.z;

    N = normalize(mul((float3x3)ObjectToWorld3x4(), N));

    uint   id  = InstanceID();
    float3 col = 0.0;

    if (id == MAT_GLASS)
        col = ShadeGlass(N, payload);
    else if (id == MAT_DIFFUSE)
        col = ShadeDiffuse(N);
    else /* MAT_METAL */
        col = ShadeMetal(N, payload);

    payload.Color = col;
    payload.Depth = RayTCurrent();


}
