#pragma once
#ifdef _WIN32

#include <d3d11.h>
#include <wrl/client.h>
#include <memory>

namespace Lingjing {

    class CursorRenderer {
    public:
        CursorRenderer();
        ~CursorRenderer();

        bool initialize(ID3D11Device* device,
            ID3D11DeviceContext* context);

        void shutdown();

        // 将光标绘制到目标纹理上
        bool render(ID3D11Texture2D* targetTexture);

        // 绘制外部光标纹理
        bool render(ID3D11Texture2D* targetTexture,
            int32_t cursorX, int32_t cursorY,
            ID3D11Texture2D* cursorTexture);

        // 获取系统光标信息
        static bool getSystemCursorInfo(int32_t& x, int32_t& y,
            bool& visible);

        // 是否已初始化
        bool isInitialized() const { return initialized_; }

    private:
        bool createPipeline();
        bool createShaders();
        bool createBlendState();
        bool createSampler();
        bool createRasterizer();

        ID3D11Device* device_ = nullptr;
        ID3D11DeviceContext* context_ = nullptr;

        Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader_;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader_;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer_;
        Microsoft::WRL::ComPtr<ID3D11BlendState> blendState_;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_;

        // 系统光标缓存
        Microsoft::WRL::ComPtr<ID3D11Texture2D> systemCursorTexture_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> systemCursorSrv_;
        int32_t systemCursorHotX_ = 0;
        int32_t systemCursorHotY_ = 0;

        bool initialized_ = false;
    };

} // namespace Lingjing
#endif