// tus includes
#include "structures.fxh"
#include "RayUtils.fxh"


static float Rand01(uint seed)
{
    float x = sin(float(seed) * 12.9898 + 78.233) * 43758.5453;
    return frac(x);
}


//‑‑ helpers sacados de SpherePrimaryHit.rchit -------------------------------
struct SurfaceInfo
{
    float3 WorldPos;
    float3 Normal;
    float4 Albedo;
};

static SurfaceInfo MakeSphereSurface(in ProceduralGeomIntersectionAttribs attribs,
                                     in Constants                     cb)
{
    SurfaceInfo s;
    float3 worldPos = WorldRayOrigin() +      // origen del rayo en espacio mundo
                  WorldRayDirection() *   // dirección normalizada
                  RayTCurrent();          // t de la intersección actual




    float3 worldNrm = attribs.Normal;     
    s.WorldPos = worldPos;
    s.Normal   = worldNrm;
    return s;
}

static bool LaunchShadowRay(float3 P, float3 L)
{
    RayDesc ray;
    ray.Origin    = P + L * SMALL_OFFSET;
    ray.Direction = L;
    ray.TMin      = 0.0;
    ray.TMax      = 1e38;

    ShadowRayPayload sh = CastShadow(ray, /*Recursion*/0);
    return sh.Shading > 0.0;
}
//---------------------------------------------------------------------------






[shader("closesthit")]
void main(inout PrimaryRayPayload            payload,
          in    ProceduralGeomIntersectionAttribs attribs)
{
        SurfaceInfo s;
        s.WorldPos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
        s.Normal   = attribs.Normal;
        uint instId = InstanceIndex();

        if (instId >= 4) // las pequeñitas
        {
            // color pseudo‐aleatorio por instancia
            float r = Rand01(instId + 0);
            float g = Rand01(instId + 1);
            float b = Rand01(instId + 2);
            s.Albedo = float4(r, g, b, 1.0);
        }
        else
        {
            // bolas grandes y suelo
            s.Albedo = float4(g_ConstantsCB.SphereReflectionColorMask.rgb, 1.0);
        }

        float3 result = g_ConstantsCB.AmbientColor.rgb;

        [unroll]
        for (uint i = 0; i < NUM_LIGHTS; ++i)
        {
            float3 L     = normalize(g_ConstantsCB.LightPos[i].xyz - s.WorldPos);
            float  NdotL = saturate(dot(s.Normal, L));

            if (!LaunchShadowRay(s.WorldPos, L))
                NdotL = 0.0;

            result += NdotL * g_ConstantsCB.LightColor[i].rgb * s.Albedo.rgb;
        }

        payload.Color = float4(result, 1.0);
}
