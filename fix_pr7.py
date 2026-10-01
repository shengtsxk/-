p = r"D:\lingjing\src\pipeline\Presenter.cpp"
s = open(p, encoding="utf-8").read()

# gpuBlendFrame 开头加日志 + 读回后日志
old = """    bool Presenter::gpuBlendFrame(const uint8_t* prev, const uint8_t* cur,
        uint8_t* out, uint32_t w, uint32_t h, float alpha)
    {
        if (!gpuBlendReady_ || !d3dDevice_ || !d3dContext_) return false;
        if (w == 0 || h == 0) return false;
"""
new = """    bool Presenter::gpuBlendFrame(const uint8_t* prev, const uint8_t* cur,
        uint8_t* out, uint32_t w, uint32_t h, float alpha)
    {
        if (!gpuBlendReady_ || !d3dDevice_ || !d3dContext_) return false;
        if (w == 0 || h == 0) return false;

        LOG_INFO("[dbg] gpuBlend enter: alpha=%.2f w=%u h=%u prev=%02X%02X%02X cur=%02X%02X%02X",
            alpha, w, h, prev[0], prev[1], prev[2], cur[0], cur[1], cur[2]);
"""
if old in s:
    s = s.replace(old, new); print("dbg enter ok")
else:
    print("MISS enter")

old2 = """        const uint8_t* src = static_cast<const uint8_t*>(ms2.pData);
        uint8_t* dst = out;
        for (UINT y = 0; y < h; ++y) {
            std::memcpy(dst, src + static_cast<size_t>(y) * ms2.RowPitch, pitch);
            dst += pitch;
        }
        ctx->Unmap(static_cast<ID3D11Resource*>(texStaging_), 0);

        return true;
    }"""
new2 = """        const uint8_t* src = static_cast<const uint8_t*>(ms2.pData);
        uint8_t* dst = out;
        for (UINT y = 0; y < h; ++y) {
            std::memcpy(dst, src + static_cast<size_t>(y) * ms2.RowPitch, pitch);
            dst += pitch;
        }
        LOG_INFO("[dbg] gpuBlend readback: %02X%02X%02X%02X rowpitch=%u",
            out[0], out[1], out[2], out[3], ms2.RowPitch);
        ctx->Unmap(static_cast<ID3D11Resource*>(texStaging_), 0);

        return true;
    }"""
if old2 in s:
    s = s.replace(old2, new2); print("dbg readback ok")
else:
    print("MISS readback")

open(p, "w", encoding="utf-8", newline="").write(s)
