#ifdef _WIN32

#include "capture/CursorRenderer.h"
#include "core/Logger.h"

#include <d3dcompiler.h>
#include <windows.h>

#pragma comment(lib, "d3dcompiler.lib")

using namespace Microsoft::WRL;

namespace Lingjing {

    // ============================================================================
    // 顶点/像素着色器源码
    // ============================================================================

    static const char* kCursorShaderSrc = R"(
// 顶点着色器
struct VSInput {
    float3 position : POSITION;
    float2 texCoord : TEXCOORD0;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

cbuffer Constants : register(b0) {
    float4 screenSize;    // width, height, 0, 0
    float4 cursorRect;    // x, y, width, height
};

VSOutput VSMain(VSInput input) {
    VSOutput output;

    // 将顶点从 [0,1] 空间映射到光标的屏幕矩形
    float2 pos;
    pos.x = cursorRect.x + input.position.x * cursorRect.z;
    pos.y = cursorRect.y + input.position.y * cursorRect.w;

    // 转换到裁剪空间 [-1, 1]
    pos.x = (pos.x / screenSize.x) * 2.0 - 1.0;
    pos.y = 1.0 - (pos.y / screenSize.y) * 2.0;

    output.position = float4(pos, 0.0, 1.0);
    output.texCoord = input.texCoord;

    return output;
}

// 像素着色器
Texture2D cursorTexture : register(t0);
SamplerState cursorSampler : register(s0);

float4 PSMain(VSOutput input) : SV_TARGET {
    float4 color = cursorTexture.Sample(cursorSampler, input.texCoord);

    // 光标纹理通常是 BGRA 格式
    // alpha 通道用于透明度
    return color;
}
)";

    // ============================================================================
    // 光标顶点数据
    // ============================================================================

    struct CursorVertex {
        float position[3];
        float texCoord[2];
    };

    static const CursorVertex kCursorVertices[] = {
        // 位置        纹理坐标
        {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
        {{1.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
    };

    // ============================================================================
    // 常量缓冲结构
    // ============================================================================

    struct CursorConstants {
        float screenSize[4];
        float cursorRect[4];
    };

    // ============================================================================
    // 构造/析构
    // ============================================================================

    CursorRenderer::CursorRenderer() = default;

    CursorRenderer::~CursorRenderer() {
        shutdown();
    }

    // ============================================================================
    // 初始化
    // ============================================================================

    bool CursorRenderer::initialize(ID3D11Device* device,
        ID3D11DeviceContext* context)
    {
        if (initialized_) return true;

        if (!device || !context) {
            LOG_ERROR("CursorRenderer: null device or context");
            return false;
        }

        device_ = device;
        context_ = context;

        // 注意：不 AddRef，因为调用者保证生命周期

        if (!createShaders()) {
            LOG_ERROR("CursorRenderer: shader creation failed");
            return false;
        }

        if (!createBlendState()) {
            LOG_ERROR("CursorRenderer: blend state creation failed");
            return false;
        }

        if (!createSampler()) {
            LOG_ERROR("CursorRenderer: sampler creation failed");
            return false;
        }

        if (!createRasterizer()) {
            LOG_ERROR("CursorRenderer: rasterizer creation failed");
            return false;
        }

        // 创建顶点缓冲
        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.ByteWidth = sizeof(kCursorVertices);
        vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        D3D11_SUBRESOURCE_DATA vbData = {};
        vbData.pSysMem = kCursorVertices;

        HRESULT hr = device->CreateBuffer(&vbDesc, &vbData,
            vertexBuffer_.GetAddressOf());
        if (FAILED(hr)) {
            LOG_ERROR("CursorRenderer: vertex buffer creation failed: 0x%08X", hr);
            return false;
        }

        initialized_ = true;
        LOG_INFO("CursorRenderer initialized");
        return true;
    }

    // ============================================================================
    // 着色器
    // ============================================================================

    bool CursorRenderer::createShaders() {
        ComPtr<ID3DBlob> vsBlob;
        ComPtr<ID3DBlob> psBlob;
        ComPtr<ID3DBlob> errorBlob;

        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG;
#endif

        // 编译顶点着色器
        HRESULT hr = D3DCompile(
            kCursorShaderSrc, strlen(kCursorShaderSrc),
            nullptr, nullptr, nullptr,
            "VSMain", "vs_5_0",
            flags, 0,
            vsBlob.GetAddressOf(),
            errorBlob.GetAddressOf());

        if (FAILED(hr)) {
            if (errorBlob) {
                LOG_ERROR("VS compile failed: %s",
                    static_cast<const char*>(errorBlob->GetBufferPointer()));
            }
            return false;
        }

        hr = device_->CreateVertexShader(
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(),
            nullptr,
            vertexShader_.GetAddressOf());

        if (FAILED(hr)) return false;

        // 输入布局
        D3D11_INPUT_ELEMENT_DESC inputDesc[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
             D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12,
             D3D11_INPUT_PER_VERTEX_DATA, 0},
        };

        hr = device_->CreateInputLayout(
            inputDesc, ARRAYSIZE(inputDesc),
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(),
            inputLayout_.GetAddressOf());

        if (FAILED(hr)) return false;

        // 编译像素着色器
        errorBlob.Reset();
        hr = D3DCompile(
            kCursorShaderSrc, strlen(kCursorShaderSrc),
            nullptr, nullptr, nullptr,
            "PSMain", "ps_5_0",
            flags, 0,
            psBlob.GetAddressOf(),
            errorBlob.GetAddressOf());

        if (FAILED(hr)) {
            if (errorBlob) {
                LOG_ERROR("PS compile failed: %s",
                    static_cast<const char*>(errorBlob->GetBufferPointer()));
            }
            return false;
        }

        hr = device_->CreatePixelShader(
            psBlob->GetBufferPointer(),
            psBlob->GetBufferSize(),
            nullptr,
            pixelShader_.GetAddressOf());

        return SUCCEEDED(hr);
    }

    // ============================================================================
    // 混合状态
    // ============================================================================

    bool CursorRenderer::createBlendState() {
        D3D11_BLEND_DESC desc = {};

        desc.RenderTarget[0].BlendEnable = TRUE;
        desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[0].RenderTargetWriteMask =
            D3D11_COLOR_WRITE_ENABLE_ALL;

        HRESULT hr = device_->CreateBlendState(&desc,
            blendState_.GetAddressOf());
        return SUCCEEDED(hr);
    }

    // ============================================================================
    // 采样器
    // ============================================================================

    bool CursorRenderer::createSampler() {
        D3D11_SAMPLER_DESC desc = {};

        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        desc.MinLOD = 0;
        desc.MaxLOD = D3D11_FLOAT32_MAX;

        HRESULT hr = device_->CreateSamplerState(&desc,
            sampler_.GetAddressOf());
        return SUCCEEDED(hr);
    }

    // ============================================================================
    // 光栅化状态
    // ============================================================================

    bool CursorRenderer::createRasterizer() {
        D3D11_RASTERIZER_DESC desc = {};

        desc.FillMode = D3D11_FILL_SOLID;
        desc.CullMode = D3D11_CULL_NONE;
        desc.FrontCounterClockwise = FALSE;
        desc.DepthClipEnable = TRUE;

        HRESULT hr = device_->CreateRasterizerState(&desc,
            rasterizer_.GetAddressOf());
        return SUCCEEDED(hr);
    }

    // ============================================================================
    // 渲染
    // ============================================================================

    bool CursorRenderer::render(ID3D11Texture2D* targetTexture) {
        // 获取系统光标
        int32_t x, y;
        bool visible;

        if (!getSystemCursorInfo(x, y, visible)) {
            return false;
        }

        if (!visible) return true;

        // 使用缓存的光标纹理
        if (!systemCursorTexture_) {
            return true;
        }

        return render(targetTexture, x, y, systemCursorTexture_.Get());
    }

    bool CursorRenderer::render(ID3D11Texture2D* targetTexture,
        int32_t cursorX, int32_t cursorY,
        ID3D11Texture2D* cursorTexture)
    {
        if (!initialized_ || !targetTexture || !cursorTexture) {
            return false;
        }

        D3D11_TEXTURE2D_DESC targetDesc;
        targetTexture->GetDesc(&targetDesc);

        D3D11_TEXTURE2D_DESC cursorDesc;
        cursorTexture->GetDesc(&cursorDesc);

        // 创建或更新 SRV
        ComPtr<ID3D11ShaderResourceView> cursorSrv;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = cursorDesc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        HRESULT hr = device_->CreateShaderResourceView(
            cursorTexture, &srvDesc, cursorSrv.GetAddressOf());

        if (FAILED(hr)) return false;

        // 创建常量缓冲
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth = sizeof(CursorConstants);
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        CursorConstants constants;
        constants.screenSize[0] = static_cast<float>(targetDesc.Width);
        constants.screenSize[1] = static_cast<float>(targetDesc.Height);
        constants.screenSize[2] = 0.0f;
        constants.screenSize[3] = 0.0f;

        constants.cursorRect[0] = static_cast<float>(cursorX);
        constants.cursorRect[1] = static_cast<float>(cursorY);
        constants.cursorRect[2] = static_cast<float>(cursorDesc.Width);
        constants.cursorRect[3] = static_cast<float>(cursorDesc.Height);

        D3D11_SUBRESOURCE_DATA cbData = {};
        cbData.pSysMem = &constants;

        ComPtr<ID3D11Buffer> constantBuffer;
        hr = device_->CreateBuffer(&cbDesc, &cbData,
            constantBuffer.GetAddressOf());

        if (FAILED(hr)) return false;

        // 创建 RTV
        ComPtr<ID3D11RenderTargetView> rtv;
        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = targetDesc.Format;
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

        hr = device_->CreateRenderTargetView(
            targetTexture, &rtvDesc, rtv.GetAddressOf());

        if (FAILED(hr)) return false;

        // 设置管线
        context_->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);

        D3D11_VIEWPORT viewport = {};
        viewport.Width = static_cast<float>(targetDesc.Width);
        viewport.Height = static_cast<float>(targetDesc.Height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        context_->RSSetViewports(1, &viewport);
        context_->RSSetState(rasterizer_.Get());

        context_->IASetInputLayout(inputLayout_.Get());

        UINT stride = sizeof(CursorVertex);
        UINT offset = 0;
        ID3D11Buffer* vb = vertexBuffer_.Get();
        context_->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

        context_->VSSetShader(vertexShader_.Get(), nullptr, 0);
        context_->VSSetConstantBuffers(0, 1, constantBuffer.GetAddressOf());

        context_->PSSetShader(pixelShader_.Get(), nullptr, 0);
        context_->PSSetShaderResources(0, 1, cursorSrv.GetAddressOf());
        context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());

        float blendFactor[4] = { 0, 0, 0, 0 };
        context_->OMSetBlendState(blendState_.Get(), blendFactor, 0xFFFFFFFF);

        context_->Draw(4, 0);

        return true;
    }

    // ============================================================================
    // 系统光标
    // ============================================================================

    bool CursorRenderer::getSystemCursorInfo(int32_t& x, int32_t& y,
        bool& visible)
    {
        CURSORINFO ci = {};
        ci.cbSize = sizeof(ci);

        if (!GetCursorInfo(&ci)) {
            visible = false;
            return false;
        }

        visible = (ci.flags & CURSOR_SHOWING) != 0;
        x = ci.ptScreenPos.x;
        y = ci.ptScreenPos.y;

        return true;
    }

    // ============================================================================
    // 关闭
    // ============================================================================

    void CursorRenderer::shutdown() {
        systemCursorSrv_.Reset();
        systemCursorTexture_.Reset();
        rasterizer_.Reset();
        sampler_.Reset();
        blendState_.Reset();
        vertexBuffer_.Reset();
        inputLayout_.Reset();
        pixelShader_.Reset();
        vertexShader_.Reset();

        device_ = nullptr;
        context_ = nullptr;
        initialized_ = false;
    }

} // namespace Lingjing
#endif