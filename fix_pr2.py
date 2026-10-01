p = r"D:\lingjing\src\pipeline\Presenter.cpp"
s = open(p, encoding="utf-8").read()

# 5. presentInterpolated：GPU 混合优先 + CPU uint32 定点优化
old5 = """        if (havePrevBits_) {
            // 帧混合插帧：在上一帧与当前帧之间生成中间帧
            // 每调用一次推进一个插值位置（倍率 m 对应 m 个中间位置）
            frameCounter_ = (frameCounter_ + 1) % m;
            float alpha = (frameCounter_ + 1) / (float)m;
            if (frameCounter_ == m - 1) alpha = 1.0f;  // 末位置为原帧

            const uint8_t* A = prevFrameBits_.data();
            const uint8_t* B = curFrameBits_.data();
            uint8_t* O = blend.data();

            const float invAlpha = 1.0f - alpha;
            for (size_t k = 0; k < n; ++k) {
                O[k] = (uint8_t)(A[k] * invAlpha + B[k] * alpha);
            }
        } else {"""
new5 = """        if (havePrevBits_) {
            // 帧混合插帧：在上一帧与当前帧之间生成中间帧
            // 每调用一次推进一个插值位置（倍率 m 对应 m 个中间位置）
            frameCounter_ = (frameCounter_ + 1) % m;
            float alpha = (frameCounter_ + 1) / (float)m;
            if (frameCounter_ == m - 1) alpha = 1.0f;  // 末位置为原帧

            const uint8_t* A = prevFrameBits_.data();
            const uint8_t* B = curFrameBits_.data();
            uint8_t* O = blend.data();

            // GPU（D3D11）着色器混合优先，失败回退 CPU 定点混合
            if (!gpuBlendFrame(A, B, O, outW_, outH_, alpha)) {
                // CPU 混合：定点 alpha（0-256），按 32bit 一次混合 4 字节，减少延迟
                const int ia = (int)(alpha * 256.0f);
                const int inv = 256 - ia;
                const uint32_t* A4 = reinterpret_cast<const uint32_t*>(A);
                const uint32_t* B4 = reinterpret_cast<const uint32_t*>(B);
                uint32_t* O4 = reinterpret_cast<uint32_t*>(O);
                const size_t count = n / 4;
                for (size_t k = 0; k < count; ++k) {
                    const uint32_t a = A4[k];
                    const uint32_t b = B4[k];
                    uint32_t r = (((a & 0xFF) * inv + (b & 0xFF) * ia) >> 8);
                    uint32_t g = ((((a >> 8) & 0xFF) * inv + ((b >> 8) & 0xFF) * ia) >> 8);
                    uint32_t bl = ((((a >> 16) & 0xFF) * inv + ((b >> 16) & 0xFF) * ia) >> 8);
                    uint32_t al = ((((a >> 24) & 0xFF) * inv + ((b >> 24) & 0xFF) * ia) >> 8);
                    O4[k] = r | (g << 8) | (bl << 16) | (al << 24);
                }
            }
        } else {"""
if old5 in s:
    s = s.replace(old5, new5); print("blend ok")
else:
    print("blend MISS")

open(p, "w", encoding="utf-8", newline="").write(s)
