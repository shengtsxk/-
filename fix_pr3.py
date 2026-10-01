p = r"D:\lingjing\src\pipeline\Presenter.cpp"
s = open(p, encoding="utf-8").read()

if "followTargetWindow" in s:
    print("already present, skip")
else:
    impl = r'''
    // ============================================================================
    // 画面跟随窗口：输出窗口跟随目标窗口的位置与尺寸
    // ============================================================================

    void Presenter::followTargetWindow() {
#ifdef _WIN32
        HWND hTarget = (HWND)targetHwnd_;
        HWND hOut = (HWND)hOutputWnd_;
        if (!hTarget || !hOut) return;

        RECT twr = {};
        RECT tcr = {};
        if (!GetWindowRect(hTarget, &twr) || !GetClientRect(hTarget, &tcr)) return;
        if (tcr.right <= 0 || tcr.bottom <= 0) return;

        const int tgtW = tcr.right - tcr.left;
        const int tgtH = tcr.bottom - tcr.top;
        const int newX = twr.right + 10;   // 输出窗口贴在目标窗口右侧
        const int newY = twr.top;

        RECT owr = {};
        GetWindowRect(hOut, &owr);

        if (newX != owr.left || newY != owr.top ||
            (int)outW_ != tgtW || (int)outH_ != tgtH) {
            MoveWindow(hOut, newX, newY, tgtW, tgtH, TRUE);
        }
#endif
    }

    // ============================================================================
    // GPU 后端：D3D11 着色器帧混合插帧
    // ============================================================================

    bool Presenter::initGpuBlend() {
        if (gpuBlendReady_) return true;

#ifdef _WIN32
        ID3D11Device* dev = nullptr;
        ID3D11DeviceContext* ctx = nullptr;

        D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };

        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE,
            nullptr, 0, levels, 3, D3D11_SDK_VERSION, &dev, nullptr, &ctx);

        if (FAILED(hr)) {
            // 回退 WARP（软件 GPU）
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP,
                nullptr, 0, levels, 3, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
        }

        if (FAILED(hr) || !dev || !ctx) {
            if (dev) dev->Release();
            if (ctx) ctx->Release();
            LOG_WARN("Presenter: D3D11 GPU backend unavailable, using CPU blend");
            return false;
        }

        d3dDevice_ = dev;
        d3dContext_ = ctx;

        // ---- 编译着色器（全屏三角形顶点 + 双纹理 alpha 混合像素） ----
        static const char* vsSrc =
            "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD; };\n"
            "VSOut mainVS(uint id : SV_VertexID) {\n"
            "  VSOut o;\n"
            "  float2 pos = float2((id << 1) & 2, id & 2);\n"
            "  o.pos = float4(pos * 2.0 - 1.0, 0.0, 1.0);\n"
            "  o.uv = float2(pos.x, 1.0 - pos.y);\n"
            "  return o;\n"
            "}\n";

        static const char* psSrc =
            "Texture2D<float4> texA : register(t0);\n"
            "Texture2D<float4> texB : register(t1);\n"
            "SamplerState samp : register(s0);\n"
            "cbuffer Params : register(b0) { float4 alpha; };\n"
            "float4 mainPS(float2 uv : TEXCOORD) : SV_Target {\n"
            "  float4 a = texA.Sample(samp, uv);\n"
            "  float4 b = texB.Sample(samp, uv);\n"
            "  return lerp(a, b, alpha.x);\n"
            "}\n";

        ID3DBlob* vsBlob = nullptr;
        ID3DBlob* psBlob = nullptr;
        ID3DBlob* errBlob = nullptr;

        hr = D3DCompile(vsSrc, strlen(vsSrc), nullptr, nullptr, nullptr,
            "mainVS", "vs_4_0", 0, 0, &vsBlob, &errBlob);
        if (FAILED(hr)) {
            if (errBlob) LOG_WARN("Presenter: VS compile: %s", (char*)errBlob->GetBufferPointer());
            goto fail;
        }
        hr = dev->CreateVertexShader(vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(), nullptr, (ID3D11VertexShader**)&shaderVS_);
        if (FAILED(hr)) goto fail;

        hr = D3DCompile(psSrc, strlen(psSrc), nullptr, nullptr, nullptr,
            "mainPS", "ps_4_0", 0, 0, &psBlob, &errBlob);
        if (FAILED(hr)) {
            if (errBlob) LOG_WARN("Presenter: PS compile: %s", (char*)errBlob->GetBufferPointer());
            goto fail;
        }
        hr = dev->CreatePixelShader(psBlob->GetBufferPointer(),
            psBlob->GetBufferSize(), nullptr, (ID3D11PixelShader**)&shaderPS_);
        if (FAILED(hr)) goto fail;

        // ---- 常量缓冲（alpha） ----
        D3D11_BUFFER_DESC cbd = {};
        cbd.ByteWidth = 16;
        cbd.Usage = D3D11_USAGE_DYNAMIC;
        cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = dev->CreateBuffer(&cbd, nullptr, (ID3D11Buffer**)&cbuffer_);
        if (FAILED(hr)) goto fail;

        // ---- 采样器 ----
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        hr = dev->CreateSamplerState(&sd, (ID3D11SamplerState**)&sampler_);
        if (FAILED(hr)) goto fail;

        if (vsBlob) vsBlob->Release();
        if (psBlob) psBlob->Release();

        gpuBlendReady_ = true;
        LOG_INFO("Presenter: GPU blend backend ready (D3D11)");
        return true;

    fail:
        if (vsBlob) vsBlob->Release();
        if (psBlob) psBlob->Release();
        if (errBlob) errBlob->Release();
        shutdownGpuBlend();
        LOG_WARN("Presenter: GPU blend backend unavailable, using CPU blend");
        return false;
#else
        return false;
#endif
    }

    void Presenter::shutdownGpuBlend() {
#ifdef _WIN32
        auto release = [](void*& p) {
            if (p) {
                reinterpret_cast<IUnknown*>(p)->Release();
                p = nullptr;
            }
        };
        release(rtvOut_);
        release(srvCur_);
        release(srvPrev_);
        release(texStaging_);
        release(texOut_);
        release(texCur_);
        release(texPrev_);
        release(cbuffer_);
        release(sampler_);
        release(shaderPS_);
        release(shaderVS_);
        release(d3dContext_);
        release(d3dDevice_);
#endif
        gpuBlendReady_ = false;
        gpuTexW_ = 0;
        gpuTexH_ = 0;
    }

    // 确保 GPU 纹理与当前帧尺寸匹配
    static bool ensureGpuTexturesImpl(void* devPtr, void*& texPrev, void*& texCur,
        void*& texOut, void*& texStaging, void*& srvPrev, void*& srvCur,
        void*& rtvOut, uint32_t& texW, uint32_t& texH,
        uint32_t w, uint32_t h)
    {
        if (texPrev && texCur && texOut && texStaging &&
            texW == w && texH == h) {
            return true;
        }

        ID3D11Device* dev = static_cast<ID3D11Device*>(devPtr);

        // 释放旧资源
        auto release = [](void*& p) {
            if (p) { reinterpret_cast<IUnknown*>(p)->Release(); p = nullptr; }
        };
        release(rtvOut);
        release(srvCur);
        release(srvPrev);
        release(texStaging);
        release(texOut);
        release(texCur);
        release(texPrev);

        D3D11_TEXTURE2D_DESC td = {};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = dev->CreateTexture2D(&td, nullptr, (ID3D11Texture2D**)&texPrev);
        if (FAILED(hr)) return false;
        hr = dev->CreateTexture2D(&td, nullptr, (ID3D11Texture2D**)&texCur);
        if (FAILED(hr)) return false;

        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        hr = dev->CreateTexture2D(&td, nullptr, (ID3D11Texture2D**)&texOut);
        if (FAILED(hr)) return false;

        td.Usage = D3D11_USAGE_STAGING;
        td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        hr = dev->CreateTexture2D(&td, nullptr, (ID3D11Texture2D**)&texStaging);
        if (FAILED(hr)) return false;

        // SRV（texPrev / texCur）
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        hr = dev->CreateShaderResourceView((ID3D11Resource*)texPrev, &srvDesc,
            (ID3D11ShaderResourceView**)&srvPrev);
        if (FAILED(hr)) return false;
        hr = dev->CreateShaderResourceView((ID3D11Resource*)texCur, &srvDesc,
            (ID3D11ShaderResourceView**)&srvCur);
        if (FAILED(hr)) return false;

        // RTV（texOut）
        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        hr = dev->CreateRenderTargetView((ID3D11Resource*)texOut, &rtvDesc,
            (ID3D11RenderTargetView**)&rtvOut);
        if (FAILED(hr)) return false;

        texW = w;
        texH = h;
        return true;
    }

    bool Presenter::gpuBlendFrame(const uint8_t* prev, const uint8_t* cur,
        uint8_t* out, uint32_t w, uint32_t h, float alpha)
    {
        if (!gpuBlendReady_ || !d3dDevice_ || !d3dContext_) return false;
        if (w == 0 || h == 0) return false;

        if (!ensureGpuTexturesImpl(d3dDevice_, texPrev_, texCur_, texOut_,
            texStaging_, srvPrev_, srvCur_, rtvOut_,
            gpuTexW_, gpuTexH_, w, h)) {
            return false;
        }

        ID3D11DeviceContext* ctx = static_cast<ID3D11DeviceContext*>(d3dContext_);
        const UINT pitch = w * 4;

        ctx->UpdateSubresource(static_cast<ID3D11Resource*>(texPrev_), 0,
            nullptr, prev, pitch, 0);
        ctx->UpdateSubresource(static_cast<ID3D11Resource*>(texCur_), 0,
            nullptr, cur, pitch, 0);

        // alpha 常量
        D3D11_MAPPED_SUBRESOURCE ms = {};
        if (SUCCEEDED(ctx->Map(static_cast<ID3D11Resource*>(cbuffer_), 0,
            D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            float* p = static_cast<float*>(ms.pData);
            p[0] = alpha;
            p[1] = 0.0f;
            p[2] = 0.0f;
            p[3] = 0.0f;
            ctx->Unmap(static_cast<ID3D11Resource*>(cbuffer_), 0);
        }

        ID3D11RenderTargetView* rtv = static_cast<ID3D11RenderTargetView*>(rtvOut_);
        ctx->OMSetRenderTargets(1, &rtv, nullptr);

        D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
        ctx->RSSetViewports(1, &vp);

        ID3D11ShaderResourceView* srvs[2] = {
            static_cast<ID3D11ShaderResourceView*>(srvPrev_),
            static_cast<ID3D11ShaderResourceView*>(srvCur_)
        };
        ctx->PSSetShaderResources(0, 2, srvs);

        ID3D11SamplerState* samp = static_cast<ID3D11SamplerState*>(sampler_);
        ctx->PSSetSamplers(0, 1, &samp);

        ID3D11Buffer* cb = static_cast<ID3D11Buffer*>(cbuffer_);
        ctx->PSSetConstantBuffers(0, 1, &cb);

        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->IASetInputLayout(nullptr);
        ctx->VSSetShader(static_cast<ID3D11VertexShader*>(shaderVS_), nullptr, 0);
        ctx->PSSetShader(static_cast<ID3D11PixelShader*>(shaderPS_), nullptr, 0);
        ctx->Draw(3, 0);

        // 读回
        ctx->CopyResource(static_cast<ID3D11Resource*>(texStaging_),
            static_cast<ID3D11Resource*>(texOut_));

        D3D11_MAPPED_SUBRESOURCE ms2 = {};
        if (FAILED(ctx->Map(static_cast<ID3D11Resource*>(texStaging_), 0,
            D3D11_MAP_READ, 0, &ms2))) {
            return false;
        }

        const uint8_t* src = static_cast<const uint8_t*>(ms2.pData);
        uint8_t* dst = out;
        for (UINT y = 0; y < h; ++y) {
            std::memcpy(dst, src + static_cast<size_t>(y) * ms2.RowPitch, pitch);
            dst += pitch;
        }
        ctx->Unmap(static_cast<ID3D11Resource*>(texStaging_), 0);

        return true;
    }

} // namespace Lingjing
'''
    # 在最后一个 "} // namespace Lingjing" 前插入
    marker = "\n} // namespace Lingjing\n"
    idx = s.rfind(marker)
    if idx == -1:
        print("MISS: namespace marker")
    else:
        s = s[:idx] + impl.rstrip() + "\n" + s[idx+1:]
        open(p, "w", encoding="utf-8", newline="").write(s)
        print("impl appended")
