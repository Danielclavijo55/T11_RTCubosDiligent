/*
 *  Copyright 2019-2024 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#include "Tutorial21_RayTracing.hpp"
#include "MapHelper.hpp"
#include "GraphicsTypesX.hpp"
#include "GraphicsUtilities.h"
#include "TextureUtilities.h"
#include "ShaderMacroHelper.hpp"
#include "imgui.h"
#include "ImGuiUtils.hpp"
#include "AdvancedMath.hpp"
#include "PlatformMisc.hpp"
#include <random>
#include <vector>

namespace Diligent
{


        namespace
    {
    // Para referenciar con claridad los distintos hit‑groups
    constexpr char HG_Cube[]           = "CubePrimaryHit";
    constexpr char HG_Ground[]         = "GroundHit";
    constexpr char HG_GlassCube[]      = "GlassPrimaryHit";
    constexpr char HG_SphereMetallic[] = "SpherePrimaryHit";
    constexpr char HG_SphereDiffuse[]  = "SpherePrimaryDiffuseHit";
    constexpr char HG_SphereGlass[]    = "SphereGlassHit"; // si lo usas
    constexpr char HG_SphereShadow[]   = "SphereShadowHit";
    } // namespace

SampleBase* CreateSample()
{
    return new Tutorial21_RayTracing();
}

void Tutorial21_RayTracing::Render()
{
    UpdateTLAS();

    // Update constants
    {
        float3 CameraWorldPos = float3::MakeVector(m_Camera.GetWorldMatrix()[3]);
        auto   CameraViewProj = m_Camera.GetViewMatrix() * m_Camera.GetProjMatrix();

        m_Constants.CameraPos   = float4{CameraWorldPos, 1.0f};
        m_Constants.InvViewProj = CameraViewProj.Inverse();

        m_pImmediateContext->UpdateBuffer(m_ConstantsCB, 0, sizeof(m_Constants), &m_Constants, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    // Trace rays
    {
        m_pRayTracingSRB->GetVariableByName(SHADER_TYPE_RAY_GEN, "g_ColorBuffer")->Set(m_pColorRT->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS));

        m_pImmediateContext->SetPipelineState(m_pRayTracingPSO);
        m_pImmediateContext->CommitShaderResources(m_pRayTracingSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        TraceRaysAttribs Attribs;
        Attribs.DimensionX = m_pColorRT->GetDesc().Width;
        Attribs.DimensionY = m_pColorRT->GetDesc().Height;
        Attribs.pSBT       = m_pSBT;

        m_pImmediateContext->TraceRays(Attribs);
    }

    // Blit to swapchain image
    {
        m_pImageBlitSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_Texture")->Set(m_pColorRT->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));

        auto* pRTV = m_pSwapChain->GetCurrentBackBufferRTV();
        m_pImmediateContext->SetRenderTargets(1, &pRTV, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_pImmediateContext->SetPipelineState(m_pImageBlitPSO);
        m_pImmediateContext->CommitShaderResources(m_pImageBlitSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_pImmediateContext->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
    }
}

void Tutorial21_RayTracing::CreateGraphicsPSO()
{
    // Create graphics pipeline to blit render target into swapchain image.

    GraphicsPipelineStateCreateInfo PSOCreateInfo;

    PSOCreateInfo.PSODesc.Name         = "Image blit PSO";
    PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

    PSOCreateInfo.GraphicsPipeline.NumRenderTargets             = 1;
    PSOCreateInfo.GraphicsPipeline.RTVFormats[0]                = m_pSwapChain->GetDesc().ColorBufferFormat;
    PSOCreateInfo.GraphicsPipeline.PrimitiveTopology            = PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode      = CULL_MODE_NONE;
    PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = False;

    ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.ShaderCompiler = SHADER_COMPILER_DXC;
    ShaderCI.CompileFlags   = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;

    // Create a shader source stream factory to load shaders from files.
    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
    m_pEngineFactory->CreateDefaultShaderSourceStreamFactory(nullptr, &pShaderSourceFactory);
    ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;

    RefCntAutoPtr<IShader> pVS;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
        ShaderCI.EntryPoint      = "main";
        ShaderCI.Desc.Name       = "Image blit VS";
        ShaderCI.FilePath        = "ImageBlit.vsh";
        m_pDevice->CreateShader(ShaderCI, &pVS);
        VERIFY_EXPR(pVS != nullptr);
    }

    RefCntAutoPtr<IShader> pPS;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
        ShaderCI.EntryPoint      = "main";
        ShaderCI.Desc.Name       = "Image blit PS";
        ShaderCI.FilePath        = "ImageBlit.psh";
        m_pDevice->CreateShader(ShaderCI, &pPS);
        VERIFY_EXPR(pPS != nullptr);
    }

    PSOCreateInfo.pVS = pVS;
    PSOCreateInfo.pPS = pPS;

    PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;

    m_pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pImageBlitPSO);
    VERIFY_EXPR(m_pImageBlitPSO != nullptr);

    m_pImageBlitPSO->CreateShaderResourceBinding(&m_pImageBlitSRB, true);
    VERIFY_EXPR(m_pImageBlitSRB != nullptr);
}

void Tutorial21_RayTracing::CreateRayTracingPSO()
{
    m_MaxRecursionDepth = std::min(m_MaxRecursionDepth, m_pDevice->GetAdapterInfo().RayTracing.MaxRecursionDepth);

    // Prepare ray tracing pipeline description.
    RayTracingPipelineStateCreateInfoX PSOCreateInfo;

    PSOCreateInfo.PSODesc.Name         = "Ray tracing PSO";
    PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_RAY_TRACING;

    // Define shader macros
    ShaderMacroHelper Macros;
    Macros.AddShaderMacro("NUM_TEXTURES", NumTextures);

    ShaderCreateInfo ShaderCI;
    // We will not be using combined texture samplers as they
    // are only required for compatibility with OpenGL, and ray
    // tracing is not supported in OpenGL backend.
    ShaderCI.Desc.UseCombinedTextureSamplers = false;

    ShaderCI.Macros = Macros;

    // Only new DXC compiler can compile HLSL ray tracing shaders.
    ShaderCI.ShaderCompiler = SHADER_COMPILER_DXC;

    // Use row-major matrices.
    ShaderCI.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;

    // Shader model 6.3 is required for DXR 1.0, shader model 6.5 is required for DXR 1.1 and enables additional features.
    // Use 6.3 for compatibility with DXR 1.0 and VK_NV_ray_tracing.
    ShaderCI.HLSLVersion    = {6, 3};
    ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;

    // Create a shader source stream factory to load shaders from files.
    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
    m_pEngineFactory->CreateDefaultShaderSourceStreamFactory(nullptr, &pShaderSourceFactory);
    ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;

    // Create ray generation shader.
    RefCntAutoPtr<IShader> pRayGen;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_RAY_GEN;
        ShaderCI.Desc.Name       = "Ray tracing RG";
        ShaderCI.FilePath        = "RayTrace.rgen";
        ShaderCI.EntryPoint      = "main";
        m_pDevice->CreateShader(ShaderCI, &pRayGen);
        VERIFY_EXPR(pRayGen != nullptr);
    }

    // Create miss shaders.
    RefCntAutoPtr<IShader> pPrimaryMiss, pShadowMiss;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_RAY_MISS;
        ShaderCI.Desc.Name       = "Primary ray miss shader";
        ShaderCI.FilePath        = "PrimaryMiss.rmiss";
        ShaderCI.EntryPoint      = "main";
        m_pDevice->CreateShader(ShaderCI, &pPrimaryMiss);
        VERIFY_EXPR(pPrimaryMiss != nullptr);

        ShaderCI.Desc.Name  = "Shadow ray miss shader";
        ShaderCI.FilePath   = "ShadowMiss.rmiss";
        ShaderCI.EntryPoint = "main";
        m_pDevice->CreateShader(ShaderCI, &pShadowMiss);
        VERIFY_EXPR(pShadowMiss != nullptr);
    }

    // Create closest hit shaders.
    RefCntAutoPtr<IShader> pCubePrimaryHit, pGroundHit, pGlassPrimaryHit, pSpherePrimaryHit, hitSphereDiffuse, hitSphereGlass;
    ;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_RAY_CLOSEST_HIT;
        ShaderCI.Desc.Name       = "Cube primary ray closest hit shader";
        ShaderCI.FilePath        = "CubePrimaryHit.rchit";
        ShaderCI.EntryPoint      = "main";
        m_pDevice->CreateShader(ShaderCI, &pCubePrimaryHit);
        VERIFY_EXPR(pCubePrimaryHit != nullptr);

        ShaderCI.Desc.Name  = "Ground primary ray closest hit shader";
        ShaderCI.FilePath   = "Ground.rchit";
        ShaderCI.EntryPoint = "main";
        m_pDevice->CreateShader(ShaderCI, &pGroundHit);
        VERIFY_EXPR(pGroundHit != nullptr);

        ShaderCI.Desc.Name  = "Glass primary ray closest hit shader";
        ShaderCI.FilePath   = "GlassPrimaryHit.rchit";
        ShaderCI.EntryPoint = "main";
        m_pDevice->CreateShader(ShaderCI, &pGlassPrimaryHit);
        VERIFY_EXPR(pGlassPrimaryHit != nullptr);

        ShaderCI.Desc.Name  = "Sphere primary ray closest hit shader";
        ShaderCI.FilePath   = "SpherePrimaryHit.rchit";
        ShaderCI.EntryPoint = "main";
        m_pDevice->CreateShader(ShaderCI, &pSpherePrimaryHit);
        VERIFY_EXPR(pSpherePrimaryHit != nullptr);

        
        ShaderCI.Desc.Name  = "Sphere primary ray closest hit diffuse shader";
        ShaderCI.FilePath   = "SphereDiffuseHit.rchit";
        ShaderCI.EntryPoint = "main";
        m_pDevice->CreateShader(ShaderCI, &hitSphereDiffuse);
        VERIFY_EXPR(hitSphereDiffuse!= nullptr);

        ShaderCI.Desc.Name  = "Sphere primary ray closest hit glass shader";
        ShaderCI.FilePath   = "SphereGlassHit.rchit";
        ShaderCI.EntryPoint = "main";
        m_pDevice->CreateShader(ShaderCI, &hitSphereGlass);
        VERIFY_EXPR(hitSphereGlass != nullptr);


    }

    // Create intersection shader for a procedural sphere.
    RefCntAutoPtr<IShader> pSphereIntersection;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_RAY_INTERSECTION;
        ShaderCI.Desc.Name       = "Sphere intersection shader";
        ShaderCI.FilePath        = "SphereIntersection.rint";
        ShaderCI.EntryPoint      = "main";
        m_pDevice->CreateShader(ShaderCI, &pSphereIntersection);
        VERIFY_EXPR(pSphereIntersection != nullptr);
    }

    // Setup shader groups

    // Ray generation shader is an entry point for a ray tracing pipeline.
    PSOCreateInfo.AddGeneralShader("Main", pRayGen);
    // Primary ray miss shader.
    PSOCreateInfo.AddGeneralShader("PrimaryMiss", pPrimaryMiss);
    // Shadow ray miss shader.
    PSOCreateInfo.AddGeneralShader("ShadowMiss", pShadowMiss);

    // Primary ray hit group for the textured cube.
    PSOCreateInfo.AddTriangleHitShader("CubePrimaryHit", pCubePrimaryHit);
    // Primary ray hit group for the ground.
    PSOCreateInfo.AddTriangleHitShader("GroundHit", pGroundHit);
    // Primary ray hit group for the glass cube.
    PSOCreateInfo.AddTriangleHitShader("GlassPrimaryHit", pGlassPrimaryHit);

    // Intersection and closest hit shaders for the procedural sphere.
    PSOCreateInfo.AddProceduralHitShader("SpherePrimaryHit", pSphereIntersection, pSpherePrimaryHit);

    PSOCreateInfo.AddProceduralHitShader("SpherePrimaryDiffuseHit", pSphereIntersection, hitSphereDiffuse);
    PSOCreateInfo.AddProceduralHitShader("SphereGlassHit", pSphereIntersection, hitSphereGlass);

    // Only intersection shader is needed for shadows.
    PSOCreateInfo.AddProceduralHitShader("SphereShadowHit", pSphereIntersection);

    // Specify the maximum ray recursion depth.
    // WARNING: the driver does not track the recursion depth and it is the
    //          application's responsibility to not exceed the specified limit.
    //          The value is used to reserve the necessary stack size and
    //          exceeding it will likely result in driver crash.
    PSOCreateInfo.RayTracingPipeline.MaxRecursionDepth = static_cast<Uint8>(m_MaxRecursionDepth);

    // Per-shader data is not used.
    PSOCreateInfo.RayTracingPipeline.ShaderRecordSize = 0;

    // DirectX 12 only: set attribute and payload size. Values should be as small as possible to minimize the memory usage.
    PSOCreateInfo.MaxAttributeSize = std::max<Uint32>(sizeof(/*BuiltInTriangleIntersectionAttributes*/ float2), sizeof(HLSL::ProceduralGeomIntersectionAttribs));
    PSOCreateInfo.MaxPayloadSize   = std::max<Uint32>(sizeof(HLSL::PrimaryRayPayload), sizeof(HLSL::ShadowRayPayload));

    // Define immutable sampler for g_Texture and g_GroundTexture. Immutable samplers should be used whenever possible
    SamplerDesc SamLinearWrapDesc{
        FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
        TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP //
    };

    PipelineResourceLayoutDescX ResourceLayout;
    ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    ResourceLayout.AddImmutableSampler(SHADER_TYPE_RAY_CLOSEST_HIT, "g_SamLinearWrap", SamLinearWrapDesc);
    ResourceLayout
        .AddVariable(SHADER_TYPE_RAY_GEN | SHADER_TYPE_RAY_MISS | SHADER_TYPE_RAY_CLOSEST_HIT, "g_ConstantsCB", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
        .AddVariable(SHADER_TYPE_RAY_GEN, "g_ColorBuffer", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);

    PSOCreateInfo.PSODesc.ResourceLayout = ResourceLayout;

    m_pDevice->CreateRayTracingPipelineState(PSOCreateInfo, &m_pRayTracingPSO);
    VERIFY_EXPR(m_pRayTracingPSO != nullptr);

    m_pRayTracingPSO->GetStaticVariableByName(SHADER_TYPE_RAY_GEN, "g_ConstantsCB")->Set(m_ConstantsCB);
    m_pRayTracingPSO->GetStaticVariableByName(SHADER_TYPE_RAY_MISS, "g_ConstantsCB")->Set(m_ConstantsCB);
    m_pRayTracingPSO->GetStaticVariableByName(SHADER_TYPE_RAY_CLOSEST_HIT, "g_ConstantsCB")->Set(m_ConstantsCB);

    m_pRayTracingPSO->CreateShaderResourceBinding(&m_pRayTracingSRB, true);
    VERIFY_EXPR(m_pRayTracingSRB != nullptr);
}

void Tutorial21_RayTracing::LoadTextures()
{
    // Load textures
    IDeviceObject*          pTexSRVs[NumTextures] = {};
    RefCntAutoPtr<ITexture> pTex[NumTextures];
    StateTransitionDesc     Barriers[NumTextures];
    for (int tex = 0; tex < NumTextures; ++tex)
    {
        // Load current texture
        TextureLoadInfo loadInfo;
        loadInfo.IsSRGB = true;

        std::stringstream FileNameSS;
        FileNameSS << "DGLogo" << tex << ".png";
        auto FileName = FileNameSS.str();
        CreateTextureFromFile(FileName.c_str(), loadInfo, m_pDevice, &pTex[tex]);

        // Get shader resource view from the texture
        auto* pTextureSRV = pTex[tex]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        pTexSRVs[tex]     = pTextureSRV;
        Barriers[tex]     = StateTransitionDesc{pTex[tex], RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE};
    }
    m_pImmediateContext->TransitionResourceStates(_countof(Barriers), Barriers);


    m_pRayTracingSRB->GetVariableByName(SHADER_TYPE_RAY_CLOSEST_HIT, "g_CubeTextures")->SetArray(pTexSRVs, 0, NumTextures);

    // Load ground texture
    RefCntAutoPtr<ITexture> pGroundTex;
    CreateTextureFromFile("Ground.jpg", TextureLoadInfo{}, m_pDevice, &pGroundTex);

    m_pRayTracingSRB->GetVariableByName(SHADER_TYPE_RAY_CLOSEST_HIT, "g_GroundTexture")->Set(pGroundTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
}

void Tutorial21_RayTracing::CreateCubeBLAS(float cubeSize, RefCntAutoPtr<IBottomLevelAS>& OutBLAS)
{
    RefCntAutoPtr<IDataBlob> pCubeVerts;
    RefCntAutoPtr<IDataBlob> pCubeIndices;
    GeometryPrimitiveInfo    CubeGeoInfo;
    //constexpr float          CubeSize = 2.f;
    CreateGeometryPrimitive(CubeGeometryPrimitiveAttributes{cubeSize, GEOMETRY_PRIMITIVE_VERTEX_FLAG_ALL}, &pCubeVerts, &pCubeIndices, &CubeGeoInfo);

    struct CubeVertex
    {
        float3 Pos;
        float3 Normal;
        float2 UV;
    };
    VERIFY_EXPR(CubeGeoInfo.VertexSize == sizeof(CubeVertex));
    const CubeVertex* pVerts   = pCubeVerts->GetConstDataPtr<CubeVertex>();
    const Uint32*     pIndices = pCubeIndices->GetConstDataPtr<Uint32>();

    // Create a buffer with cube attributes.
    // These attributes will be used in the hit shader to calculate UVs and normal for intersection point.
    {
        HLSL::CubeAttribs Attribs;
        for (Uint32 v = 0; v < CubeGeoInfo.NumVertices; ++v)
        {
            Attribs.UVs[v]     = {pVerts[v].UV, 0, 0};
            Attribs.Normals[v] = pVerts[v].Normal;
        }

        for (Uint32 i = 0; i < CubeGeoInfo.NumIndices; i += 3)
        {
            const Uint32* TriIdx{&pIndices[i]};
            Attribs.Primitives[i / 3] = uint4{TriIdx[0], TriIdx[1], TriIdx[2], 0};
        }

        BufferDesc BuffDesc;
        BuffDesc.Name      = "Cube Attribs size";
        BuffDesc.Usage     = USAGE_IMMUTABLE;
        BuffDesc.BindFlags = BIND_UNIFORM_BUFFER;
        BuffDesc.Size      = sizeof(Attribs);

        BufferData BufData = {&Attribs, BuffDesc.Size};

        if (!m_CubeAttribsCB) {
        
        m_pDevice->CreateBuffer(BuffDesc, &BufData, &m_CubeAttribsCB);
            VERIFY_EXPR(m_CubeAttribsCB != nullptr);


            m_pRayTracingSRB->GetVariableByName(SHADER_TYPE_RAY_CLOSEST_HIT, "g_CubeAttribsCB")->Set(m_CubeAttribsCB);  
        
        }
        
    }

    // Create vertex and index buffers
    RefCntAutoPtr<IBuffer>             pCubeVertexBuffer;
    RefCntAutoPtr<IBuffer>             pCubeIndexBuffer;
    GeometryPrimitiveBuffersCreateInfo CubeBuffersCI;
    CubeBuffersCI.VertexBufferBindFlags = BIND_RAY_TRACING;
    CubeBuffersCI.IndexBufferBindFlags  = BIND_RAY_TRACING;
    CreateGeometryPrimitiveBuffers(m_pDevice, CubeGeometryPrimitiveAttributes{cubeSize, GEOMETRY_PRIMITIVE_VERTEX_FLAG_POSITION},
                                   &CubeBuffersCI, &pCubeVertexBuffer, &pCubeIndexBuffer);

      /* … generación de vértices/índices idéntica … */

    // ------------------ CREAR el BLAS ------------------
    BLASTriangleDesc Triangles;
    Triangles.GeometryName         = "Cube";
    Triangles.MaxVertexCount       = CubeGeoInfo.NumVertices;
    Triangles.VertexValueType      = VT_FLOAT32;
    Triangles.VertexComponentCount = 3;
    Triangles.MaxPrimitiveCount    = CubeGeoInfo.NumIndices / 3;
    Triangles.IndexType            = VT_UINT32;

    BottomLevelASDesc ASDesc;
    ASDesc.Name          = (cubeSize == 2.f ? "Cube BLAS" : "SmallCube BLAS");
    ASDesc.Flags         = RAYTRACING_BUILD_AS_PREFER_FAST_TRACE;
    ASDesc.pTriangles    = &Triangles;
    ASDesc.TriangleCount = 1;

    m_pDevice->CreateBLAS(ASDesc, &OutBLAS);
    VERIFY_EXPR(OutBLAS != nullptr);

    // ------------------ SCRATCH BUFFER -----------------
    RefCntAutoPtr<IBuffer> pScratchBuffer;
    {
        BufferDesc BuffDesc;
        BuffDesc.Name      = "BLAS Scratch Buffer";
        BuffDesc.Usage     = USAGE_DEFAULT;
        BuffDesc.BindFlags = BIND_RAY_TRACING;
        BuffDesc.Size      = OutBLAS->GetScratchBufferSizes().Build; // ← ¡ya existe!

        m_pDevice->CreateBuffer(BuffDesc, nullptr, &pScratchBuffer);
    }

    // ------------------ BUILD BLAS ---------------------
    BLASBuildTriangleData TriData;
    TriData.GeometryName         = Triangles.GeometryName;
    TriData.pVertexBuffer        = pCubeVertexBuffer;
    TriData.VertexStride         = sizeof(float3);
    TriData.VertexCount          = Triangles.MaxVertexCount;
    TriData.VertexValueType      = Triangles.VertexValueType;
    TriData.VertexComponentCount = Triangles.VertexComponentCount;
    TriData.pIndexBuffer         = pCubeIndexBuffer;
    TriData.PrimitiveCount       = Triangles.MaxPrimitiveCount;
    TriData.IndexType            = Triangles.IndexType;
    TriData.Flags                = RAYTRACING_GEOMETRY_FLAG_OPAQUE;

    BuildBLASAttribs BuildAttrs;
    BuildAttrs.pBLAS                  = OutBLAS; // ← usar OutBLAS
    BuildAttrs.pTriangleData          = &TriData;
    BuildAttrs.TriangleDataCount      = 1;
    BuildAttrs.pScratchBuffer         = pScratchBuffer;
    BuildAttrs.BLASTransitionMode     = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    BuildAttrs.GeometryTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    BuildAttrs.ScratchBufferTransitionMode =
        RESOURCE_STATE_TRANSITION_MODE_TRANSITION;

    m_pImmediateContext->BuildBLAS(BuildAttrs);
    }


void Tutorial21_RayTracing::CreateProceduralBLAS()
{
    static_assert(sizeof(HLSL::BoxAttribs) % 16 == 0, "BoxAttribs must be aligned by 16 bytes");

    const HLSL::BoxAttribs Boxes[] = {HLSL::BoxAttribs{-1.5f, -1.5f, -1.5f, 1.5f, 1.5f, 1.5f}, HLSL::BoxAttribs{-0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f}};

    // Create box buffer
    {
        BufferData BufData = {Boxes, sizeof(Boxes)};
        BufferDesc BuffDesc;
        BuffDesc.Name              = "AABB Buffer";
        BuffDesc.Usage             = USAGE_IMMUTABLE;
        BuffDesc.BindFlags         = BIND_RAY_TRACING | BIND_SHADER_RESOURCE;
        BuffDesc.Size              = sizeof(Boxes);
        BuffDesc.ElementByteStride = sizeof(Boxes[0]);
        BuffDesc.Mode              = BUFFER_MODE_STRUCTURED;

        m_pDevice->CreateBuffer(BuffDesc, &BufData, &m_BoxAttribsCB);
        VERIFY_EXPR(m_BoxAttribsCB != nullptr);

        m_pRayTracingSRB->GetVariableByName(SHADER_TYPE_RAY_INTERSECTION, "g_BoxAttribs")->Set(m_BoxAttribsCB->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
    }

    // Create & build bottom level acceleration structure
    {
        // Create BLAS
        BLASBoundingBoxDesc BoxInfo;
        {
            BoxInfo.GeometryName = "Box";
            BoxInfo.MaxBoxCount  = 1;

            BottomLevelASDesc ASDesc;
            ASDesc.Name     = "Procedural BLAS";
            ASDesc.Flags    = RAYTRACING_BUILD_AS_PREFER_FAST_TRACE;
            ASDesc.pBoxes   = &BoxInfo;
            ASDesc.BoxCount = 1;

            m_pDevice->CreateBLAS(ASDesc, &m_pProceduralBLAS);
            VERIFY_EXPR(m_pProceduralBLAS != nullptr);
        }

        // Create scratch buffer
        RefCntAutoPtr<IBuffer> pScratchBuffer;
        {
            BufferDesc BuffDesc;
            BuffDesc.Name      = "BLAS Scratch Buffer";
            BuffDesc.Usage     = USAGE_DEFAULT;
            BuffDesc.BindFlags = BIND_RAY_TRACING;
            BuffDesc.Size      = m_pProceduralBLAS->GetScratchBufferSizes().Build;

            m_pDevice->CreateBuffer(BuffDesc, nullptr, &pScratchBuffer);
            VERIFY_EXPR(pScratchBuffer != nullptr);
        }

        // Build BLAS
        BLASBuildBoundingBoxData BoxData;
        BoxData.GeometryName = BoxInfo.GeometryName;
        BoxData.BoxCount     = 1;
        BoxData.BoxStride    = sizeof(Boxes[0]);
        BoxData.pBoxBuffer   = m_BoxAttribsCB;

        BuildBLASAttribs Attribs;
        Attribs.pBLAS        = m_pProceduralBLAS;
        Attribs.pBoxData     = &BoxData;
        Attribs.BoxDataCount = 1;

        // Scratch buffer will be used to store temporary data during BLAS build.
        // Previous content in the scratch buffer will be discarded.
        Attribs.pScratchBuffer = pScratchBuffer;

        // Allow engine to change resource states.
        Attribs.BLASTransitionMode          = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        Attribs.GeometryTransitionMode      = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        Attribs.ScratchBufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;

        m_pImmediateContext->BuildBLAS(Attribs);
    }
}

void Tutorial21_RayTracing::ModifyEngineInitInfo(const ModifyEngineInitInfoAttribs& Attribs)
{
    SampleBase::ModifyEngineInitInfo(Attribs);

    // Require ray tracing feature.
    Attribs.EngineCI.Features.RayTracing = DEVICE_FEATURE_STATE_ENABLED;
}

void Tutorial21_RayTracing::UpdateTLAS()
{
    // Create or update top-level acceleration structure
    int  NumSmallSpheres = (2 * a) * (2 * b);
    int  NumSmallCubes   = (2 * c) * (2 * d); // Corregido: ahora con N mayúscula
    int  NumInstances    = 4 + NumSmallSpheres + NumSmallCubes;
    bool NeedUpdate      = true;

    // Create TLAS
    if (!m_pTLAS)
    {
        TopLevelASDesc TLASDesc;
        TLASDesc.Name             = "TLAS";
        TLASDesc.MaxInstanceCount = NumInstances;
        TLASDesc.Flags            = RAYTRACING_BUILD_AS_ALLOW_UPDATE | RAYTRACING_BUILD_AS_PREFER_FAST_TRACE;

        m_pDevice->CreateTLAS(TLASDesc, &m_pTLAS);
        VERIFY_EXPR(m_pTLAS != nullptr);

        NeedUpdate = false; // build on first run

        m_pRayTracingSRB->GetVariableByName(SHADER_TYPE_RAY_GEN, "g_TLAS")->Set(m_pTLAS);
        m_pRayTracingSRB->GetVariableByName(SHADER_TYPE_RAY_CLOSEST_HIT, "g_TLAS")->Set(m_pTLAS);

        // Reserva y genera posiciones solo UNA vez
        m_SmallSphereTransforms.resize(NumSmallSpheres);
        m_SmallCubeTransforms.resize(NumSmallCubes);
        m_cubesCustomIds.resize(NumSmallCubes);

        if (m_MaxSmallSpheres == 0)
        {
            m_MaxSmallSpheres       = NumSmallSpheres;
            m_NumActiveSmallSpheres = NumSmallSpheres;
        }

        if (m_MaxSmallCubes == 0)
        {
            m_MaxSmallCubes       = NumSmallCubes;
            m_NumActiveSmallCubes = NumSmallCubes;
        }

        // Nueva distribución: espiral para esferas
        std::vector<float3> positions;
        positions.reserve(NumSmallSpheres);

        // Parámetros para distribución en espiral
        const float radio_inicial     = 5.0f;
        const float incremento_radio  = 0.3f;
        const float incremento_angulo = 0.5f;
        const float altura_base       = -5.5f;

        // Generar posiciones en espiral para esferas
        for (int i = 0; i < NumSmallSpheres; ++i)
        {
            float radio  = radio_inicial + incremento_radio * i;
            float angulo = incremento_angulo * i;

            float x = radio * cos(angulo);
            float z = radio * sin(angulo);
            float y = altura_base + (i * 0.05f); // Ligera elevación en espiral

            float3 p = float3{x, y, z};
            positions.push_back(p);

            // Crea la matriz de instancia
            InstanceMatrix xf;
            xf.SetTranslation(p.x, p.y, p.z);
            m_SmallSphereTransforms[i] = xf;
        }

        // Nueva distribución: estructura de grilla para cubos pero en forma de pirámide
        std::vector<float3> cubePositions;
        cubePositions.reserve(NumSmallCubes); // Corregido con N mayúscula

        int capas = 5; // Número de capas de la pirámide
        int index = 0;

        for (int capa = 0; capa < capas && index < NumSmallCubes; capa++)
        { // Corregido con N mayúscula
            int   cubos_por_lado = capas - capa;
            float nivel_y        = altura_base + (capa * 1.5f); // Altura de cada capa

            // Generar cubos en forma de cuadrado para esta capa
            for (int i = -cubos_por_lado / 2; i <= cubos_por_lado / 2 && index < NumSmallCubes; i++)
            { // Corregido
                for (int j = -cubos_por_lado / 2; j <= cubos_por_lado / 2 && index < NumSmallCubes; j++)
                { // Corregido
                    // Solo agregar cubos en los bordes del cuadrado para formar un marco
                    if (i == -cubos_por_lado / 2 || i == cubos_por_lado / 2 ||
                        j == -cubos_por_lado / 2 || j == cubos_por_lado / 2)
                    {

                        float  x = i * 2.0f; // Espaciado de 2 unidades
                        float  z = j * 2.0f;
                        float3 p = float3{x, nivel_y, z};

                        cubePositions.push_back(p);

                        // Asignar materiales de manera diferente - asignar más variedad
                        int   instanceId;
                        float mat_selector = static_cast<float>(index) / NumSmallCubes; // Corregido

                        if (mat_selector < 0.6f)
                        {
                            instanceId = 0; // Textura cristal para mayoría
                        }
                        else if (mat_selector < 0.85f)
                        {
                            instanceId = 1; // Textura difusa
                        }
                        else
                        {
                            instanceId = 2; // Textura metal
                        }

                        m_cubesCustomIds[index] = instanceId;

                        // Crear matriz de instancia
                        InstanceMatrix xf;
                        xf.SetTranslation(p.x, p.y, p.z);
                        // Aplicar rotación adicional variada
                        float3x3 rot = float3x3::RotationY(index * 0.2f) * float3x3::RotationX(capa * 0.15f);
                        xf.SetRotation(rot.Data());
                        m_SmallCubeTransforms[index] = xf;

                        index++;
                    }
                }
            }
        }

        // Llenar los cubos restantes si aún no hemos alcanzado el límite
        while (index < NumSmallCubes)
        { // Corregido
            float x = (index % 10) * 2.0f - 10.0f;
            float z = (index / 10) * 2.0f - 10.0f;
            float y = altura_base - 2.0f; // Ponerlos más abajo que el resto

            float3 p = float3{x, y, z};
            cubePositions.push_back(p);

            // Material aleatorio
            m_cubesCustomIds[index] = index % 3;

            // Crear matriz de instancia
            InstanceMatrix xf;
            xf.SetTranslation(p.x, p.y, p.z);
            m_SmallCubeTransforms[index] = xf;

            index++;
        }

        // Inicializar nombres de instancias
        m_SphereInstanceNames.resize(NumSmallSpheres);
        m_CubeInstanceNames.resize(NumSmallCubes); // Corregido
    }

    // Resto del código permanece igual...
    // Create scratch buffer
    if (!m_ScratchBuffer)
    {
        BufferDesc BuffDesc;
        BuffDesc.Name      = "TLAS Scratch Buffer";
        BuffDesc.Usage     = USAGE_DEFAULT;
        BuffDesc.BindFlags = BIND_RAY_TRACING;
        BuffDesc.Size      = std::max(m_pTLAS->GetScratchBufferSizes().Build, m_pTLAS->GetScratchBufferSizes().Update);

        m_pDevice->CreateBuffer(BuffDesc, nullptr, &m_ScratchBuffer);
        VERIFY_EXPR(m_ScratchBuffer != nullptr);
    }

    // Create instance buffer
    if (!m_InstanceBuffer)
    {
        BufferDesc BuffDesc;
        BuffDesc.Name      = "TLAS Instance Buffer";
        BuffDesc.Usage     = USAGE_DEFAULT;
        BuffDesc.BindFlags = BIND_RAY_TRACING;
        BuffDesc.Size      = TLAS_INSTANCE_DATA_SIZE * NumInstances;

        m_pDevice->CreateBuffer(BuffDesc, nullptr, &m_InstanceBuffer);
        VERIFY_EXPR(m_InstanceBuffer != nullptr);
    }

    // Setup instances
    std::vector<TLASBuildInstanceData> Instances(NumInstances);

    // Configurar los objetos principales
    Instances[0].InstanceName = "Ground Instance";
    Instances[0].pBLAS        = m_pCubeBLAS;
    Instances[0].Mask         = OPAQUE_GEOM_MASK;
    Instances[0].Transform.SetRotation(float3x3::Scale(100.0f, 0.1f, 100.0f).Data());
    Instances[0].Transform.SetTranslation(0.0f, -6.0f, 0.0f);

    Instances[1].InstanceName = "Cube Instance 1";
    Instances[1].CustomId     = 0;
    Instances[1].pBLAS        = m_pCubeBLAS;
    Instances[1].Mask         = OPAQUE_GEOM_MASK;
    Instances[1].Transform.SetTranslation(-4.0f, -4.5f, -0.f);

    Instances[2].InstanceName = "Cube Instance 2";
    Instances[2].CustomId     = 1;
    Instances[2].pBLAS        = m_pCubeBLAS;
    Instances[2].Mask         = OPAQUE_GEOM_MASK;
    Instances[2].Transform.SetTranslation(0.0f, -4.5f, -3.f);

    Instances[3].InstanceName = "Cube Instance 3";
    Instances[3].CustomId     = 2;
    Instances[3].pBLAS        = m_pCubeBLAS;
    Instances[3].Mask         = OPAQUE_GEOM_MASK;
    Instances[3].Transform.SetTranslation(4.0f, -4.5f, -6.f);

    // Configurar las esferas pequeñas
    int idx = 4;
    for (int i = 0; i < (2 * a) * (2 * b); ++i)
    {
        auto& name = m_SphereInstanceNames[i];
        name       = "Sphere Instance " + std::to_string(idx);

        auto& Inst        = Instances[idx];
        Inst.InstanceName = name.c_str();
        Inst.CustomId     = 1;
        Inst.pBLAS        = m_pProceduralBLAS;
        Inst.Mask         = (i < m_NumActiveSmallSpheres) ? OPAQUE_GEOM_MASK : 0;
        Inst.Transform    = m_SmallSphereTransforms[i];

        ++idx;
    }

    // Configurar los cubos pequeños
    int idx_c2 = 0;
    for (int i = 0; i < (2 * c) * (2 * d); ++i)
    {
        auto& name = m_CubeInstanceNames[i];
        name       = "Cube Instance " + std::to_string(idx);

        auto& Inst        = Instances[idx];
        Inst.InstanceName = name.c_str();
        Inst.CustomId     = m_cubesCustomIds[i];
        Inst.pBLAS        = m_pSmallCubeBLAS;
        Inst.Mask         = (i < m_NumActiveSmallCubes) ? OPAQUE_GEOM_MASK : 0;
        Inst.Transform    = m_SmallCubeTransforms[i];

        ++idx;
        ++idx_c2;
    }

    // Build or update TLAS
    BuildTLASAttribs Attribs;
    Attribs.pTLAS                        = m_pTLAS;
    Attribs.Update                       = NeedUpdate;
    Attribs.pScratchBuffer               = m_ScratchBuffer;
    Attribs.pInstanceBuffer              = m_InstanceBuffer;
    Attribs.pInstances                   = Instances.data();
    Attribs.InstanceCount                = NumInstances;
    Attribs.BindingMode                  = HIT_GROUP_BINDING_MODE_PER_INSTANCE;
    Attribs.HitGroupStride               = HIT_GROUP_STRIDE;
    Attribs.TLASTransitionMode           = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    Attribs.BLASTransitionMode           = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    Attribs.InstanceBufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    Attribs.ScratchBufferTransitionMode  = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;

    m_pImmediateContext->BuildTLAS(Attribs);
}

void Tutorial21_RayTracing::Update(double CurrTime, double ElapsedTime)
{
    SampleBase::Update(CurrTime, ElapsedTime);
    UpdateUI();

    if (m_Animate)
    {
        m_AnimationTime += static_cast<float>(std::min(m_MaxAnimationTimeDelta, ElapsedTime));
    }

    m_Camera.Update(m_InputController, static_cast<float>(ElapsedTime));

    // Do not allow going underground
    auto oldPos = m_Camera.GetPos();
    if (oldPos.y < -5.7f)
    {
        oldPos.y = -5.7f;
        m_Camera.SetPos(oldPos);
        m_Camera.Update(m_InputController, 0.f);
    }
}

void Tutorial21_RayTracing::WindowResize(Uint32 Width, Uint32 Height)
{
    if (Width == 0 || Height == 0)
        return;

    // Update projection matrix.
    float AspectRatio = static_cast<float>(Width) / static_cast<float>(Height);
    m_Camera.SetProjAttribs(m_Constants.ClipPlanes.x, m_Constants.ClipPlanes.y, AspectRatio, PI_F / 4.f,
                            m_pSwapChain->GetDesc().PreTransform, m_pDevice->GetDeviceInfo().NDC.MinZ == -1);

    // Check if the image needs to be recreated.
    if (m_pColorRT != nullptr &&
        m_pColorRT->GetDesc().Width == Width &&
        m_pColorRT->GetDesc().Height == Height)
        return;

    m_pColorRT = nullptr;

    // Create window-size color image.
    TextureDesc RTDesc       = {};
    RTDesc.Name              = "Color buffer";
    RTDesc.Type              = RESOURCE_DIM_TEX_2D;
    RTDesc.Width             = Width;
    RTDesc.Height            = Height;
    RTDesc.BindFlags         = BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE;
    RTDesc.ClearValue.Format = m_ColorBufferFormat;
    RTDesc.Format            = m_ColorBufferFormat;

    m_pDevice->CreateTexture(RTDesc, nullptr, &m_pColorRT);
}
void Tutorial21_RayTracing::Initialize(const SampleInitInfo& InitInfo)
{
    SampleBase::Initialize(InitInfo);

    if ((m_pDevice->GetAdapterInfo().RayTracing.CapFlags & RAY_TRACING_CAP_FLAG_STANDALONE_SHADERS) == 0)
    {
        UNSUPPORTED("Ray tracing shaders are not supported by device");
        return;
    }

    // Create a buffer with shared constants.
    BufferDesc BuffDesc;
    BuffDesc.Name      = "Constant buffer";
    BuffDesc.Size      = sizeof(m_Constants);
    BuffDesc.Usage     = USAGE_DEFAULT;
    BuffDesc.BindFlags = BIND_UNIFORM_BUFFER;

    m_pDevice->CreateBuffer(BuffDesc, nullptr, &m_ConstantsCB);
    VERIFY_EXPR(m_ConstantsCB != nullptr);

    CreateGraphicsPSO();
    CreateRayTracingPSO();
    LoadTextures();
    CreateCubeBLAS(2.0f, m_pCubeBLAS);
    CreateCubeBLAS(0.5f, m_pSmallCubeBLAS);
    CreateProceduralBLAS();
    UpdateTLAS();
    CreateSBT();

    // Setup camera.
    m_Camera.SetPos(float3(7.f, -0.5f, -16.5f));
    m_Camera.SetRotation(0.48f, -0.145f);
    m_Camera.SetRotationSpeed(0.005f);
    m_Camera.SetMoveSpeed(5.f);
    m_Camera.SetSpeedUpScales(5.f, 10.f);

    // Initialize constants.
    {
        m_Constants.ClipPlanes   = float2{0.1f, 100.0f};
        m_Constants.ShadowPCF    = 1;
        m_Constants.MaxRecursion = std::min(Uint32{6}, m_MaxRecursionDepth);

        // Sphere constants.
        m_Constants.SphereReflectionColorMask = {0.81f, 1.0f, 0.45f};
        m_Constants.SphereReflectionBlur      = 1;

        // Glass cube constants.
        m_Constants.GlassReflectionColorMask = {0.22f, 0.83f, 0.93f};
        m_Constants.GlassAbsorption          = 0.5f;
        m_Constants.GlassMaterialColor       = {0.33f, 0.93f, 0.29f};
        m_Constants.GlassIndexOfRefraction   = {1.5f, 1.02f};
        m_Constants.GlassEnableDispersion    = 0;

        // Wavelength to RGB and index of refraction interpolation factor.
        m_Constants.DispersionSamples[0]  = {0.140000f, 0.000000f, 0.266667f, 0.53f};
        m_Constants.DispersionSamples[1]  = {0.130031f, 0.037556f, 0.612267f, 0.25f};
        m_Constants.DispersionSamples[2]  = {0.100123f, 0.213556f, 0.785067f, 0.16f};
        m_Constants.DispersionSamples[3]  = {0.050277f, 0.533556f, 0.785067f, 0.00f};
        m_Constants.DispersionSamples[4]  = {0.000000f, 0.843297f, 0.619682f, 0.13f};
        m_Constants.DispersionSamples[5]  = {0.000000f, 0.927410f, 0.431834f, 0.38f};
        m_Constants.DispersionSamples[6]  = {0.000000f, 0.972325f, 0.270893f, 0.27f};
        m_Constants.DispersionSamples[7]  = {0.000000f, 0.978042f, 0.136858f, 0.19f};
        m_Constants.DispersionSamples[8]  = {0.324000f, 0.944560f, 0.029730f, 0.47f};
        m_Constants.DispersionSamples[9]  = {0.777600f, 0.871879f, 0.000000f, 0.64f};
        m_Constants.DispersionSamples[10] = {0.972000f, 0.762222f, 0.000000f, 0.77f};
        m_Constants.DispersionSamples[11] = {0.971835f, 0.482222f, 0.000000f, 0.62f};
        m_Constants.DispersionSamples[12] = {0.886744f, 0.202222f, 0.000000f, 0.73f};
        m_Constants.DispersionSamples[13] = {0.715967f, 0.000000f, 0.000000f, 0.68f};
        m_Constants.DispersionSamples[14] = {0.459920f, 0.000000f, 0.000000f, 0.91f};
        m_Constants.DispersionSamples[15] = {0.218000f, 0.000000f, 0.000000f, 0.99f};
        m_Constants.DispersionSampleCount = 4;

        // Modificar el color del cielo (ambiente) a gris
        m_Constants.AmbientColor  = float4(0.5f, 0.5f, 0.5f, 0.f) * 0.025f;
        m_Constants.LightPos[0]   = {8.00f, +8.0f, +0.00f, 0.f};
        m_Constants.LightColor[0] = {1.00f, +0.8f, +0.80f, 0.f};
        m_Constants.LightPos[1]   = {0.00f, +4.0f, -5.00f, 0.f};
        m_Constants.LightColor[1] = {0.85f, +1.0f, +0.85f, 0.f};

        // Random points on disc.
        m_Constants.DiscPoints[0] = {+0.0f, +0.0f, +0.9f, -0.9f};
        m_Constants.DiscPoints[1] = {-0.8f, +1.0f, -1.1f, -0.8f};
        m_Constants.DiscPoints[2] = {+1.5f, +1.2f, -2.1f, +0.7f};
        m_Constants.DiscPoints[3] = {+0.1f, -2.2f, -0.2f, +2.4f};
        m_Constants.DiscPoints[4] = {+2.4f, -0.3f, -3.0f, +2.8f};
        m_Constants.DiscPoints[5] = {+2.0f, -2.6f, +0.7f, +3.5f};
        m_Constants.DiscPoints[6] = {-3.2f, -1.6f, +3.4f, +2.2f};
        m_Constants.DiscPoints[7] = {-1.8f, -3.2f, -1.1f, +3.6f};
    }
    static_assert(sizeof(HLSL::Constants) % 16 == 0, "must be aligned by 16 bytes");
}
void Tutorial21_RayTracing::CreateSBT()
{
    // Create shader binding table.
    ShaderBindingTableDesc SBTDesc;
    SBTDesc.Name = "SBT";
    SBTDesc.pPSO = m_pRayTracingPSO;

    m_pDevice->CreateSBT(SBTDesc, &m_pSBT);
    VERIFY_EXPR(m_pSBT != nullptr);

    m_pSBT->BindRayGenShader("Main");

    m_pSBT->BindMissShader("PrimaryMiss", PRIMARY_RAY_INDEX);
    m_pSBT->BindMissShader("ShadowMiss", SHADOW_RAY_INDEX);

    // Hit groups for primary ray
    m_pSBT->BindHitGroupForInstance(m_pTLAS, "Cube Instance 1", PRIMARY_RAY_INDEX, "GlassPrimaryHit");
    m_pSBT->BindHitGroupForInstance(m_pTLAS, "Cube Instance 2", PRIMARY_RAY_INDEX, "GlassPrimaryHit");
    m_pSBT->BindHitGroupForInstance(m_pTLAS, "Cube Instance 3", PRIMARY_RAY_INDEX, "GlassPrimaryHit");
    m_pSBT->BindHitGroupForInstance(m_pTLAS, "Ground Instance", PRIMARY_RAY_INDEX, "GroundHit");

    // Hit groups for shadow ray.
    m_pSBT->BindHitGroupForTLAS(m_pTLAS, SHADOW_RAY_INDEX, nullptr);

    // Distribuir aleatoriamente los tres tipos de materiales para esferas
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    for (int i = 0; i < m_SphereInstanceNames.size(); ++i)
    {
        // Nueva distribución de materiales: más metálico, menos vidrio
        float materialSelector = dist(rng);

        if (materialSelector < 0.7f) // 70% de probabilidad para metálico
        {
            m_pSBT->BindHitGroupForInstance(m_pTLAS, m_SphereInstanceNames[i].c_str(), PRIMARY_RAY_INDEX, "SpherePrimaryHit");
        }
        else if (materialSelector < 0.9f) // 20% de probabilidad para difuso
        {
            m_pSBT->BindHitGroupForInstance(m_pTLAS, m_SphereInstanceNames[i].c_str(), PRIMARY_RAY_INDEX, "SpherePrimaryDiffuseHit");
        }
        else // 10% de probabilidad para vidrio
        {
            m_pSBT->BindHitGroupForInstance(m_pTLAS, m_SphereInstanceNames[i].c_str(), PRIMARY_RAY_INDEX, "SphereGlassHit");
        }

        m_pSBT->BindHitGroupForInstance(m_pTLAS, m_SphereInstanceNames[i].c_str(), SHADOW_RAY_INDEX, "SphereShadowHit");
    }

    // Para los cubos, asignar materiales según los IDs personalizados
    for (int i = 0; i < m_CubeInstanceNames.size(); ++i)
    {
        // Usar un hit group diferente basado en el ID personalizado
        if (m_cubesCustomIds[i] == 0)
        {
            m_pSBT->BindHitGroupForInstance(m_pTLAS, m_CubeInstanceNames[i].c_str(), PRIMARY_RAY_INDEX, "GlassPrimaryHit");
        }
        else if (m_cubesCustomIds[i] == 1)
        {
            m_pSBT->BindHitGroupForInstance(m_pTLAS, m_CubeInstanceNames[i].c_str(), PRIMARY_RAY_INDEX, "CubePrimaryHit");
        }
        else
        {
            m_pSBT->BindHitGroupForInstance(m_pTLAS, m_CubeInstanceNames[i].c_str(), PRIMARY_RAY_INDEX, "GlassPrimaryHit");
        }
    }

    // Update SBT with the shader groups we bound
    m_pImmediateContext->UpdateSBT(m_pSBT);
}

void Tutorial21_RayTracing::UpdateUI()
{
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Ray Tracing Demo", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Scene Controls");
        ImGui::Text("Use WASD keys to move camera");

        // Essential controls only
        ImGui::Separator();
        ImGui::Text("Scene Objects");

        // Sphere control
        ImGui::SliderInt("Active Spheres", &m_NumActiveSmallSpheres, 0, m_MaxSmallSpheres);

        // Cube control
        ImGui::SliderInt("Active Cubes", &m_NumActiveSmallCubes, 0, m_MaxSmallCubes);

        // Render quality
        ImGui::Separator();
        ImGui::Text("Render Quality");
        ImGui::SliderInt("Recursion Depth", &m_Constants.MaxRecursion, 1, m_MaxRecursionDepth);
        ImGui::SliderInt("Shadow Quality", &m_Constants.ShadowPCF, 0, 4);
    }
    ImGui::End();
}

} // namespace Diligent
