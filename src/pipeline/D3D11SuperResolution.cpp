#include "pipeline/D3D11SuperResolution.h"
#include "core/Logger.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <windows.h>

#include <cmath>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace Lingjing {

// ============================================================================
// HLSL 内核源码（cs_5_0）— ED-ASR 边缘引导自适应超分辨率
//
// 对每个输出像素：
//   1) 中心差分亮度梯度 -> 边缘方向（切线 = 垂直梯度方向）
//   2) 沿切线方向 5-tap 采样（±1、±2 输入纹素，保边缘清晰）
//   3) 沿法线方向 2-tap 采样（±0.5 输入纹素，平滑去锯齿）
//   4) 4 角点菱形均值 -> 模糊基线，提取高频细节注入
//   5) 非锐化掩模微锐化
// 总纹理采样约 13 次/输出像素，无分支发散（除边界），适合任何 D3D11 GPU。
// ============================================================================

static const char* kShaderSrc = R"HLSL(
cbuffer CBSR : register(b0) {
    uint gOutW;
    uint gOutH;
    uint gScale;
    uint gPad0;
    float gDetail;
    float gSharp;
    float gPad1;
    float gPad2;
};

SamplerState gLinear : register(s0);
Texture2D<float4> gIn : register(t0);
RWTexture2D<float4> gOut : register(u0);

// 8 方向模板（0°, 45°, 90°, 135°, 180°, 225°, 270°, 315°）
static const float2 kDir[8] = {
    float2(1.0f, 0.0f),      float2(0.7071f, 0.7071f),
    float2(0.0f, 1.0f),      float2(-0.7071f, 0.7071f),
    float2(-1.0f, 0.0f),     float2(-0.7071f, -0.7071f),
    float2(0.0f, -1.0f),     float2(0.7071f, -0.7071f),
};

static float Lum(float4 c) {
    // 纹理 R 通道 = 内存第一字节 = B（蓝）
    return dot(c.rgb, float3(0.114f, 0.587f, 0.299f));
}

[numthreads(16,16,1)]
void CS_EDASR(uint3 tid : SV_DispatchThreadID) {
    uint2 outPos = tid.xy;
    if (outPos.x >= gOutW || outPos.y >= gOutH) return;

    // 输出归一化 UV == 输入归一化 UV（线性采样）
    float2 srcUV = (float2((float)outPos.x, (float)outPos.y) + float2(0.5f, 0.5f))
        / float2((float)gOutW, (float)gOutH);
    // 输入归一化 UV 的单位步长
    float2 inStep = float2((float)gScale, (float)gScale)
        / float2((float)gOutW, (float)gOutH);

    // ---- 1. 亮度梯度（中心差分） ----
    float lC = Lum(gIn.SampleLevel(gLinear, srcUV, 0.0f));
    float lR = Lum(gIn.SampleLevel(gLinear, srcUV + float2(inStep.x, 0.0f), 0.0f));
    float lL = Lum(gIn.SampleLevel(gLinear, srcUV - float2(inStep.x, 0.0f), 0.0f));
    float lD = Lum(gIn.SampleLevel(gLinear, srcUV + float2(0.0f, inStep.y), 0.0f));
    float lU = Lum(gIn.SampleLevel(gLinear, srcUV - float2(0.0f, inStep.y), 0.0f));
    float2 grad = float2(lR - lL, lD - lU) * 0.5f;
    float gMag = length(grad);

    float2 tanDir = float2(1.0f, 0.0f);
    float2 normDir = float2(0.0f, 1.0f);
    if (gMag > 1e-4f) {
        float2 nrm = normalize(grad);
        // 将梯度方向量化为 8 方向之一（稳定边缘核，避免连续方向抖动）
        int best = 0;
        float bestDot = -1.0f;
        [unroll] for (int d = 0; d < 8; ++d) {
            float c = dot(nrm, kDir[d]);
            if (c > bestDot) { bestDot = c; best = d; }
        }
        normDir = kDir[best];
        tanDir = kDir[(best + 2) & 7];   // 垂直方向 = 沿边缘切向
    }

    // 边缘强度调制系数：强边缘 -> 细节增强更多；平坦区 -> 抑制噪声放大
    float edgeK = smoothstep(0.02f, 0.35f, gMag);

    // ---- 2. 方向自适应采样 ----
    float4 acc = float4(0, 0, 0, 0);
    float wsum = 0.0f;

    // 切向 5-tap：±1、±2 输入纹素（保边缘）
    [unroll] for (int i = -2; i <= 2; ++i) {
        float w = 1.0f / (1.0f + (float)abs(i) * 0.45f);
        float2 off = tanDir * ((float)i * 1.0f) * inStep;
        acc += gIn.SampleLevel(gLinear, srcUV + off, 0.0f) * w;
        wsum += w;
    }

    // 法向 2-tap：±0.5 输入纹素（去锯齿）
    [unroll] for (int j = -1; j <= 1; j += 2) {
        float w = 1.0f / (1.0f + (float)abs(j) * 0.8f);
        float2 off = normDir * ((float)j * 0.5f) * inStep;
        acc += gIn.SampleLevel(gLinear, srcUV + off, 0.0f) * w;
        wsum += w;
    }

    float4 base = acc / wsum;

    // ---- 3. 菱形 4-角均值（模糊基线） ----
    float4 blur = 0.25f * (
        gIn.SampleLevel(gLinear, srcUV + float2(-1.0f, -1.0f) * inStep, 0.0f) +
        gIn.SampleLevel(gLinear, srcUV + float2(1.0f, -1.0f) * inStep, 0.0f) +
        gIn.SampleLevel(gLinear, srcUV + float2(-1.0f, 1.0f) * inStep, 0.0f) +
        gIn.SampleLevel(gLinear, srcUV + float2(1.0f, 1.0f) * inStep, 0.0f));

    // ---- 4. 高频细节重建（边缘调制 + 振铃抑制） ----
    float4 detail = base - blur;
    // 限制细节幅度，避免强边缘过冲（振铃）
    detail = clamp(detail, float4(-0.25f, -0.25f, -0.25f, 0.0f),
        float4(0.25f, 0.25f, 0.25f, 0.0f));
    float4 result = base + detail * (gDetail * (0.35f + 0.65f * edgeK));
    // 非锐化掩模，同样按边缘强度调制
    result += (result - blur) * (gSharp * (0.20f + 0.80f * edgeK));

    gOut[outPos] = result;
}
)HLSL";

// ============================================================================
// 实现
// ============================================================================

struct D3D11SuperResolution::Impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11ComputeShader> csEDASR;
    ComPtr<ID3D11Buffer> cbSR;
    ComPtr<ID3D11Query> syncQuery;

    // 输入（只读）
    ComPtr<ID3D11Texture2D> texIn;
    ComPtr<ID3D11ShaderResourceView> srvIn;

    // 输出（可写）
    ComPtr<ID3D11Texture2D> texOut;
    ComPtr<ID3D11UnorderedAccessView> uavOut;

    // 读回
    ComPtr<ID3D11Texture2D> stagingOut;

    int inW = 0, inH = 0, scale_ = 0;
    int outW = 0, outH = 0;
    bool sizeReady = false;

    void releaseSizeResources() {
        texIn.Reset(); srvIn.Reset();
        texOut.Reset(); uavOut.Reset();
        stagingOut.Reset();
        inW = 0; inH = 0; scale_ = 0;
        outW = 0; outH = 0;
        sizeReady = false;
    }

    bool makeReadOnlyTexture(
        uint32_t w, uint32_t h, DXGI_FORMAT fmt,
        ComPtr<ID3D11Texture2D>& tex,
        ComPtr<ID3D11ShaderResourceView>& srv)
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = fmt; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        HRESULT hr = device->CreateTexture2D(&td, nullptr, &tex);
        if (FAILED(hr)) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format = fmt; sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        hr = device->CreateShaderResourceView(tex.Get(), &sd, &srv);
        return SUCCEEDED(hr);
    }

    bool makeWritableTexture(
        uint32_t w, uint32_t h, DXGI_FORMAT fmt,
        ComPtr<ID3D11Texture2D>& tex,
        ComPtr<ID3D11UnorderedAccessView>& uav)
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = fmt; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        HRESULT hr = device->CreateTexture2D(&td, nullptr, &tex);
        if (FAILED(hr)) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
        ud.Format = fmt; ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        ud.Texture2D.MipSlice = 0;
        hr = device->CreateUnorderedAccessView(tex.Get(), &ud, &uav);
        return SUCCEEDED(hr);
    }

    bool makeStaging(uint32_t w, uint32_t h, DXGI_FORMAT fmt,
        ComPtr<ID3D11Texture2D>& staging)
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = fmt; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_STAGING;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        HRESULT hr = device->CreateTexture2D(&td, nullptr, &staging);
        return SUCCEEDED(hr);
    }
};

// ============================================================================
// 构造/析构
// ============================================================================

D3D11SuperResolution::D3D11SuperResolution()
    : impl_(new Impl()) {
}

D3D11SuperResolution::~D3D11SuperResolution() {
    shutdown();
    delete impl_;
    impl_ = nullptr;
}

bool D3D11SuperResolution::initialize() {
    if (ready_) return true;

    UINT flags = 0;
    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    D3D_FEATURE_LEVEL got = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        levels, 2, D3D11_SDK_VERSION,
        &impl_->device, &got, &impl_->ctx);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
            levels, 2, D3D11_SDK_VERSION,
            &impl_->device, &got, &impl_->ctx);
        if (FAILED(hr)) {
            LOG_ERROR("D3D11SuperResolution: device creation failed (0x%08X)", hr);
            return false;
        }
    }

    // 采样器（线性，钳位）
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    hr = impl_->device->CreateSamplerState(&sd, &impl_->sampler);
    if (FAILED(hr)) {
        LOG_ERROR("D3D11SuperResolution: sampler creation failed (0x%08X)", hr);
        return false;
    }

    // 常量缓冲
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = 64;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = impl_->device->CreateBuffer(&bd, nullptr, &impl_->cbSR);
    if (FAILED(hr)) {
        LOG_ERROR("D3D11SuperResolution: constant buffer failed (0x%08X)", hr);
        return false;
    }

    // GPU 同步 query
    D3D11_QUERY_DESC qd = {};
    qd.Query = D3D11_QUERY_EVENT;
    hr = impl_->device->CreateQuery(&qd, &impl_->syncQuery);
    if (FAILED(hr)) {
        LOG_WARN("D3D11SuperResolution: sync query failed (0x%08X)", hr);
    }

    // 编译超分内核
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> err;
    hr = D3DCompile(kShaderSrc, strlen(kShaderSrc), "EDASR",
        nullptr, nullptr, "CS_EDASR", "cs_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &err);
    if (FAILED(hr)) {
        const char* msg = err ? (const char*)err->GetBufferPointer() : "unknown";
        LOG_ERROR("D3D11SuperResolution: shader compile failed (0x%08X): %s",
            hr, msg);
        return false;
    }

    hr = impl_->device->CreateComputeShader(
        blob->GetBufferPointer(), blob->GetBufferSize(), nullptr,
        &impl_->csEDASR);
    if (FAILED(hr)) {
        LOG_ERROR("D3D11SuperResolution: shader create failed (0x%08X)", hr);
        return false;
    }

    ready_ = true;
    LOG_INFO("D3D11SuperResolution: ED-ASR ready (compute, cs_5_0)");
    return true;
}

void D3D11SuperResolution::shutdown() {
    if (!ready_ && !impl_->device) return;
    impl_->releaseSizeResources();
    impl_->csEDASR.Reset();
    impl_->cbSR.Reset();
    impl_->sampler.Reset();
    impl_->syncQuery.Reset();
    impl_->ctx.Reset();
    impl_->device.Reset();
    ready_ = false;
}

bool D3D11SuperResolution::upscale(
    const uint8_t* frame,
    int width,
    int height,
    int scale,
    std::vector<uint8_t>& out,
    float& ms)
{
    ms = 0.0f;
    if (!ready_ || !frame || width <= 0 || height <= 0) return false;
    if (scale < 2 || scale > 5) return false;

    Impl& d = *impl_;
    // 只处理目标窗口区域（width x height），不生成放大后的全图：
    // 输出分辨率 = 输入分辨率，shader 内通过 UV 映射实现 scale 倍放大，
    // GPU 计算量与读回量降为全图方案的 1/scale^2，大幅降低延迟。
    const int outW = width;
    const int outH = height;
    const int outPitch = outW * 4;

    // 尺寸变化时重建资源
    if (!d.sizeReady || d.inW != width || d.inH != height || d.scale_ != scale) {
        d.releaseSizeResources();

        // 输入纹理（默认用途，UpdateSubresource 上传）
        {
            D3D11_TEXTURE2D_DESC td = {};
            td.Width = width; td.Height = height; td.MipLevels = 1; td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            HRESULT hr = d.device->CreateTexture2D(&td, nullptr, &d.texIn);
            if (FAILED(hr)) return false;

            D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
            sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            sd.Texture2D.MipLevels = 1;
            hr = d.device->CreateShaderResourceView(d.texIn.Get(), &sd, &d.srvIn);
            if (FAILED(hr)) return false;
        }

        // 输出纹理 + UAV
        if (!d.makeWritableTexture(outW, outH, DXGI_FORMAT_R8G8B8A8_UNORM,
                d.texOut, d.uavOut)) {
            return false;
        }

        // staging 读回
        if (!d.makeStaging(outW, outH, DXGI_FORMAT_R8G8B8A8_UNORM, d.stagingOut)) {
            return false;
        }

        d.inW = width; d.inH = height; d.scale_ = scale;
        d.outW = outW; d.outH = outH;
        d.sizeReady = true;
    }

    auto& ctx = d.ctx;

    // 上传输入（DEFAULT 用途纹理使用 UpdateSubresource，不能 Map）
    {
        D3D11_BOX box = {};
        box.left = 0;
        box.top = 0;
        box.right = static_cast<UINT>(width);
        box.bottom = static_cast<UINT>(height);
        box.front = 0;
        box.back = 1;
        ctx->UpdateSubresource(d.texIn.Get(), 0, &box,
            frame, static_cast<UINT>(width * 4), 0);
    }

    // 常量
    struct SRCB {
        UINT outW, outH, scale, pad0;
        float detail, sharp, pad1, pad2;
    } cb = {};
    cb.outW = static_cast<UINT>(outW);
    cb.outH = static_cast<UINT>(outH);
    cb.scale = static_cast<UINT>(scale);
    cb.detail = 0.85f;
    cb.sharp = 0.35f;
    ctx->UpdateSubresource(d.cbSR.Get(), 0, nullptr, &cb, 0, 0);

    ctx->CSSetConstantBuffers(0, 1, d.cbSR.GetAddressOf());
    ctx->CSSetSamplers(0, 1, d.sampler.GetAddressOf());
    ctx->CSSetShaderResources(0, 1, d.srvIn.GetAddressOf());
    ctx->CSSetUnorderedAccessViews(0, 1, d.uavOut.GetAddressOf(), nullptr);
    ctx->CSSetShader(d.csEDASR.Get(), nullptr, 0);

    // dispatch（只计算目标窗口区域）
    ctx->Dispatch((outW + 15) / 16, (outH + 15) / 16, 1);

    // GPU 同步（计时含 GPU 执行时间）
    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    if (d.syncQuery) {
        ctx->End(d.syncQuery.Get());
        // D3D11 事件查询：GPU 未完成时 GetData 返回 S_FALSE（≠ S_OK）。
        // 必须等待 S_OK 且 done==TRUE 才能读回，否则 CopyResource 读到
        // 未完成计算的输出纹理 -> 每帧黑屏。
        BOOL done = FALSE;
        for (;;) {
            HRESULT hq = ctx->GetData(d.syncQuery.Get(), &done, sizeof(done), 0);
            if (hq == S_OK && done) break;
            if (FAILED(hq)) break;   // 设备错误：放弃等待，避免死循环
            Sleep(0);                // 让出 CPU，避免忙等占满核心
        }
    }
    QueryPerformanceCounter(&t1);
    ms = static_cast<float>(t1.QuadPart - t0.QuadPart) * 1000.0f
        / static_cast<float>(freq.QuadPart);

    // 解绑 UAV（重要：避免残留绑定影响后续）
    ID3D11UnorderedAccessView* nullUav[] = { nullptr };
    ctx->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr);
    ID3D11ShaderResourceView* nullSrv[] = { nullptr };
    ctx->CSSetShaderResources(0, 1, nullSrv);

    // 读回（staging 与输出同尺寸，均为目标窗口区域）
    ctx->CopyResource(d.stagingOut.Get(), d.texOut.Get());

    D3D11_MAPPED_SUBRESOURCE rd;
    if (FAILED(ctx->Map(d.stagingOut.Get(), 0, D3D11_MAP_READ, 0, &rd))) {
        return false;
    }

    out.resize(static_cast<size_t>(outPitch) * outH);
    const uint8_t* src = static_cast<const uint8_t*>(rd.pData);
    uint8_t* dst = out.data();
    for (int y = 0; y < outH; ++y) {
        std::memcpy(dst + static_cast<size_t>(y) * outPitch,
            src + static_cast<size_t>(y) * rd.RowPitch,
            static_cast<size_t>(outPitch));
    }
    ctx->Unmap(d.stagingOut.Get(), 0);

    lastMs_ = ms;
    return true;
}

} // namespace Lingjing
