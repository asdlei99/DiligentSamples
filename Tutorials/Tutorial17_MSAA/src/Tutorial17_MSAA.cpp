/*     Copyright 2019 Diligent Graphics LLC
 *  
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF ANY PROPRIETARY RIGHTS.
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

#include <array>

#include "Tutorial17_MSAA.h"
#include "MapHelper.h"
#include "GraphicsUtilities.h"
#include "TextureUtilities.h"
#include "../../Common/src/TexturedCube.h"
#include "imgui.h"
#include "ImGuiUtils.h"

namespace Diligent
{

SampleBase* CreateSample()
{
    return new Tutorial17_MSAA();
}

void Tutorial17_MSAA::CreateCubePSO()
{
    // Create a shader source stream factory to load shaders from files.
    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
    m_pEngineFactory->CreateDefaultShaderSourceStreamFactory(nullptr, &pShaderSourceFactory);

    m_pCubePSO = TexturedCube::CreatePipelineState(m_pDevice, 
        m_pSwapChain->GetDesc().ColorBufferFormat,
        DepthBufferFormat,
        pShaderSourceFactory,
        "cube.vsh",
        "cube.psh",
        nullptr, 0,
        m_SampleCount);
      
    // Since we did not explcitly specify the type for 'Constants' variable, default
    // type (SHADER_RESOURCE_VARIABLE_TYPE_STATIC) will be used. Static variables never
    // change and are bound directly through the pipeline state object.
    m_pCubePSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "Constants")->Set(m_CubeVSConstants);

    m_pCubeSRB.Release();
    // Since we are using mutable variable, we must create a shader resource binding object
    // http://diligentgraphics.com/2016/03/23/resource-binding-model-in-diligent-engine-2-0/
    m_pCubePSO->CreateShaderResourceBinding(&m_pCubeSRB, true);
    // Set cube texture SRV in the SRB
    m_pCubeSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_Texture")->Set(m_CubeTextureSRV);
}

void Tutorial17_MSAA::UpdateUI()
{
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        std::array<std::pair<Uint8, const char*>, 4> ComboItems;
        Uint32 NumItems = 0;
        ComboItems[NumItems++] = std::make_pair(Uint8{1}, "1");
        if (m_SupportedSampleCounts & 0x02)
            ComboItems[NumItems++] = std::make_pair(Uint8{2}, "2");
        if (m_SupportedSampleCounts & 0x04)
            ComboItems[NumItems++] = std::make_pair(Uint8{4}, "4");
        if (m_SupportedSampleCounts & 0x08)
            ComboItems[NumItems++] = std::make_pair(Uint8{8}, "8");
        if (ImGui::Combo("Sample count", &m_SampleCount, ComboItems.data(), NumItems))
        {
            CreateCubePSO();
            CreateMSAARenderTarget();
        }

        ImGui::Checkbox("Rotate gird", &m_bRotateGrid);
    }
    ImGui::End();   
}

void Tutorial17_MSAA::Initialize(IEngineFactory*   pEngineFactory,
                                 IRenderDevice*    pDevice,
                                 IDeviceContext**  ppContexts,
                                 Uint32            NumDeferredCtx,
                                 ISwapChain*       pSwapChain)
{
    SampleBase::Initialize(pEngineFactory, pDevice, ppContexts, NumDeferredCtx, pSwapChain);
    
    const auto& ColorFmtInfo = pDevice->GetTextureFormatInfoExt(m_pSwapChain->GetDesc().ColorBufferFormat);
    const auto& DepthFmtInfo = pDevice->GetTextureFormatInfoExt(m_pSwapChain->GetDesc().DepthBufferFormat);
    m_SupportedSampleCounts = ColorFmtInfo.SampleCounts & DepthFmtInfo.SampleCounts;
    if (m_SupportedSampleCounts & 0x04)
        m_SampleCount = 4;
    else if (m_SupportedSampleCounts & 0x02)
        m_SampleCount = 2;
    else
    {
        LOG_WARNING_MESSAGE(ColorFmtInfo.Name, " + ", DepthFmtInfo.Name, " pair does not allow multisampling on this device");
        m_SampleCount = 1;
    }

    // Create dynamic uniform buffer that will store our transformation matrix
    // Dynamic buffers can be frequently updated by the CPU
    CreateUniformBuffer(m_pDevice, sizeof(float4x4), "VS constants CB", &m_CubeVSConstants);

    // Load textured cube
    m_CubeVertexBuffer = TexturedCube::CreateVertexBuffer(pDevice);
    m_CubeIndexBuffer  = TexturedCube::CreateIndexBuffer(pDevice);
    m_CubeTextureSRV = TexturedCube::LoadTexture(pDevice, "DGLogo.png")->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

    CreateCubePSO();
}

void Tutorial17_MSAA::WindowResize(Uint32 Width, Uint32 Height)
{
    CreateMSAARenderTarget();
}

void Tutorial17_MSAA::CreateMSAARenderTarget()
{
    if (m_SampleCount == 1)
        return;

    // Create window-size multi-sampled offscreen render target
    TextureDesc ColorDesc;
    ColorDesc.Name        = "Multisampled render target";
    ColorDesc.Type        = RESOURCE_DIM_TEX_2D;
    ColorDesc.Width       = m_pSwapChain->GetDesc().Width;
    ColorDesc.Height      = m_pSwapChain->GetDesc().Height;
    ColorDesc.MipLevels   = 1;
    ColorDesc.Format      = m_pSwapChain->GetDesc().ColorBufferFormat;
    ColorDesc.SampleCount = m_SampleCount;

    // The render target can be bound as a shader resource and as a render target
    ColorDesc.BindFlags   = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
    // Define optimal clear value
    ColorDesc.ClearValue.Format = ColorDesc.Format;
    ColorDesc.ClearValue.Color[0] = 0.125f;
    ColorDesc.ClearValue.Color[1] = 0.125f;
    ColorDesc.ClearValue.Color[2] = 0.125f;
    ColorDesc.ClearValue.Color[3] = 1.f;
    RefCntAutoPtr<ITexture> pColor;
    m_pDevice->CreateTexture(ColorDesc, nullptr, &pColor);
    // Store the render target view
    m_pMSColorRTV = pColor->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    

    // Create window-size multi-sampled depth buffer
    TextureDesc DepthDesc = ColorDesc;
    DepthDesc.Name        = "Multisampled depth buffer";
    DepthDesc.Format = DepthBufferFormat;
    // Define optimal clear value
    DepthDesc.ClearValue.Format = DepthDesc.Format;
    DepthDesc.ClearValue.DepthStencil.Depth = 1;
    DepthDesc.ClearValue.DepthStencil.Stencil = 0;
    // The depth buffer can be bound as a shader resource and as a depth-stencil buffer
    DepthDesc.BindFlags = BIND_SHADER_RESOURCE | BIND_DEPTH_STENCIL;
    RefCntAutoPtr<ITexture> pDepth;
    m_pDevice->CreateTexture(DepthDesc, nullptr, &pDepth);
    // Store the depth-stencil view
    m_pMSDepthDSV = pDepth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
}

// Render a frame
void Tutorial17_MSAA::Render()
{
    const float ClearColor[] = { 0.125f,  0.125f,  0.125f, 1.0f };
    ITextureView* pRTV = nullptr;
    ITextureView* pDSV = nullptr;
    if (m_SampleCount > 1)
    {
        m_pImmediateContext->SetRenderTargets(1, &m_pMSColorRTV, m_pMSDepthDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        pRTV = m_pMSColorRTV;
        pDSV = m_pMSDepthDSV;
    }
    else
    {
        m_pImmediateContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    m_pImmediateContext->ClearRenderTarget(pRTV, ClearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_pImmediateContext->ClearDepthStencil(pDSV, CLEAR_DEPTH_FLAG, 1.0f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    {
        // Map the cube's constant buffer and fill it in with its model-view-projection matrix
        MapHelper<float4x4> CBConstants(m_pImmediateContext, m_CubeVSConstants, MAP_WRITE, MAP_FLAG_DISCARD);
        *CBConstants = m_WorldViewProjMatrix.Transpose();
    }

    // Bind vertex and index buffers
    Uint32 offset = 0;
    IBuffer* pBuffs[] = {m_CubeVertexBuffer};
    m_pImmediateContext->SetVertexBuffers(0, 1, pBuffs, &offset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
    m_pImmediateContext->SetIndexBuffer(m_CubeIndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Set the cube's pipeline state
    m_pImmediateContext->SetPipelineState(m_pCubePSO);

    // Commit the cube shader's resources
    m_pImmediateContext->CommitShaderResources(m_pCubeSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Draw the cube
    DrawIndexedAttribs DrawAttrs;
    DrawAttrs.IndexType    = VT_UINT32; // Index type
    DrawAttrs.NumIndices   = 36;
    DrawAttrs.NumInstances = 49;
    DrawAttrs.Flags        = DRAW_FLAG_VERIFY_ALL; // Verify the state of vertex and index buffers
    m_pImmediateContext->DrawIndexed(DrawAttrs);

    if (m_SampleCount > 1)
    {
        auto pCurrentBackBuffer = m_pSwapChain->GetCurrentBackBufferRTV()->GetTexture();
        ResolveTextureSubresourceAttribs ResolveAttribs;
        ResolveAttribs.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        ResolveAttribs.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        m_pImmediateContext->ResolveTextureSubresource(m_pMSColorRTV->GetTexture(), pCurrentBackBuffer, ResolveAttribs);
    }
}

void Tutorial17_MSAA::Update(double CurrTime, double ElapsedTime)
{
    SampleBase::Update(CurrTime, ElapsedTime);
    UpdateUI();

    if (m_bRotateGrid)
        m_fCurrentTime += static_cast<float>(ElapsedTime);
    // Set cube world view matrix
    float4x4 WorldView = float4x4::RotationZ(m_fCurrentTime * 0.1f) * float4x4::Translation(0.0f, 0.0f, 30.0f);
    float NearPlane = 0.1f;
    float FarPlane = 100.f;
    float aspectRatio = static_cast<float>(m_pSwapChain->GetDesc().Width) / static_cast<float>(m_pSwapChain->GetDesc().Height);

    // Projection matrix differs between DX and OpenGL
    auto Proj = float4x4::Projection(PI_F / 4.0f, aspectRatio, NearPlane, FarPlane, m_pDevice->GetDeviceCaps().IsGLDevice());

    // Compute world-view-projection matrix
    m_WorldViewProjMatrix = WorldView * Proj;
}

}
