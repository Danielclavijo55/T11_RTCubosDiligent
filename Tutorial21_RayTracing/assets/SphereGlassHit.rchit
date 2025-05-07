// -------------------------------------------------------------------------
//  Closest‑hit para la esfera de vidrio
// -------------------------------------------------------------------------
#include "structures.fxh"
#include "RayUtils.fxh"

struct SurfaceInfo
{
    float3 WorldPos;
    float3 Normal;
};

static SurfaceInfo MakeSphereSurface(in ProceduralGeomIntersectionAttribs a)
{
    SurfaceInfo s;
    s.WorldPos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    s.Normal   = a.Normal;
    return s;
}

// ---- Fresnel simplificado ------------------------------------------------
float Fresnel(float eta, float cosThetaI)
{
    cosThetaI = clamp(cosThetaI, -1.0, 1.0);
    if (cosThetaI < 0.0) { eta = 1.0 / eta; cosThetaI = -cosThetaI; }

    float sin2t = eta * eta * (1.0 - cosThetaI * cosThetaI);
    if (sin2t > 1.0) return 1.0;

    float cosThetaT = sqrt(1.0 - sin2t);
    float Rs = (eta * cosThetaI - cosThetaT) / (eta * cosThetaI + cosThetaT);
    float Rp = (eta * cosThetaT - cosThetaI) / (eta * cosThetaT + cosThetaI);
    return 0.5 * (Rs * Rs + Rp * Rp);
}

// -------------------------------------------------------------------------
[shader("closesthit")]
void main(inout PrimaryRayPayload payload,
          in    ProceduralGeomIntersectionAttribs attribs)
{
    SurfaceInfo surf = MakeSphereSurface(attribs);

    float3 V    = -WorldRayDirection();
    float  cosI = saturate(dot(surf.Normal, V));
    float  eta  = g_ConstantsCB.GlassIndexOfRefraction.x;

    float3 reflDir = reflect(V, surf.Normal);
    float3 refrDir = refract(V, surf.Normal, 1.0 / eta);
    float  kr      = Fresnel(eta, cosI);

    // --------- declarar e inicializar payloads secundarios ----------------
    PrimaryRayPayload reflPl = (PrimaryRayPayload)0;
    PrimaryRayPayload refrPl = (PrimaryRayPayload)0;

    // ------------------ REFLEXIÓN -----------------------------------------
    {
        RayDesc ray;
        ray.Origin    = surf.WorldPos + reflDir * SMALL_OFFSET;
        ray.Direction = reflDir;
        ray.TMin      = 0.0;
        ray.TMax      = 1e38;
        reflPl = CastPrimaryRay(ray, payload.Recursion + 1);
    }

    // ------------------ REFRACCIÓN ----------------------------------------
    {
        RayDesc ray;
        ray.Origin    = surf.WorldPos + refrDir * SMALL_OFFSET;
        ray.Direction = refrDir;
        ray.TMin      = 0.0;
        ray.TMax      = 1e38;
        refrPl = CastPrimaryRay(ray, payload.Recursion + 1);
    }

    // ------------------ COMBINAR RESULTADOS -------------------------------
    float3 col = kr * reflPl.Color +
                (1.0 - kr) * refrPl.Color * g_ConstantsCB.GlassMaterialColor.rgb;

    payload.Color = float4(col, 1.0);
}
