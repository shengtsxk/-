import io

# 1. Types.h: add isCpuBuffer flag
p = r"D:\lingjing\include\core\Types.h"
with io.open(p, "r", encoding="utf-8") as f:
    s = f.read()
old = """    struct GpuTexture {
        void* nativeHandle = nullptr;      // ID3D11Texture2D* / ID3D12Resource* / cl_mem
        void* sharedHandle = nullptr;      // 共享句柄（跨进程）
"""
new = """    struct GpuTexture {
        void* nativeHandle = nullptr;      // ID3D11Texture2D* / ID3D12Resource* / cl_mem / CPU 像素缓冲
        void* sharedHandle = nullptr;      // 共享句柄（跨进程）
        bool isCpuBuffer = false;          // nativeHandle 指向 CPU 像素内存（读回缓冲），而非 GPU 纹理
"""
assert old in s, "Types.h anchor missing"
s = s.replace(old, new, 1)
old2 = """        void reset() {
            nativeHandle = nullptr;
            sharedHandle = nullptr;
"""
new2 = """        void reset() {
            nativeHandle = nullptr;
            sharedHandle = nullptr;
            isCpuBuffer = false;
"""
assert old2 in s, "Types.h reset anchor missing"
s = s.replace(old2, new2, 1)
with io.open(p, "w", encoding="utf-8", newline="") as f:
    f.write(s)
print("Types.h OK")

# 2. WGCCapture.cpp: set isCpuBuffer on readback; raise reserve cap to 4K
p = r"D:\lingjing\src\capture\WGCCapture.cpp"
with io.open(p, "r", encoding="utf-8") as f:
    s = f.read()

old3 = """        // 预分配 CPU 读回缓冲容量（避免运行中 realloc 导致悬垂指针/崩溃）
        {
            const size_t capBytes =
                (size_t)impl_->frameSize.width * impl_->frameSize.height * 4;
            impl_->cpuFrameBuffers[0].reserve(capBytes);
            impl_->cpuFrameBuffers[1].reserve(capBytes);
        }
"""
new3 = """        // 预分配 CPU 读回缓冲容量（固定 4K 上限，避免运行中尺寸变化触发
        // realloc 导致 Presenter 持有的旧 data() 指针悬垂崩溃）
        {
            const size_t frameBytes =
                (size_t)impl_->frameSize.width * impl_->frameSize.height * 4;
            const size_t capBytes =
                frameBytes > (size_t)4096 * 4096 * 4
                    ? frameBytes : (size_t)4096 * 4096 * 4;
            impl_->cpuFrameBuffers[0].reserve(capBytes);
            impl_->cpuFrameBuffers[1].reserve(capBytes);
        }
"""
assert old3 in s, "WGCCapture reserve anchor missing"
s = s.replace(old3, new3, 1)

old4 = """                    const int bi = cpuBufferIdx;
                    cpuBufferIdx = 1 - cpuBufferIdx;
                    auto& buf = cpuFrameBuffers[bi];
                    const size_t bytes = (size_t)desc.Width * desc.Height * 4;
                    if (buf.size() < bytes) buf.resize(bytes);
"""
new4 = """                    const int bi = cpuBufferIdx;
                    cpuBufferIdx = 1 - cpuBufferIdx;
                    auto& buf = cpuFrameBuffers[bi];
                    const size_t bytes = (size_t)desc.Width * desc.Height * 4;
                    if (buf.size() < bytes) buf.resize(bytes);  // 容量已预分配 4K，不会 realloc
"""
assert old4 in s, "WGCCapture readback anchor missing"
s = s.replace(old4, new4, 1)

old5 = """                    d3dContext->Unmap(stagingTex.Get(), 0);
                    gpuTex.nativeHandle = buf.data();
                }
            }
        }
        if (!gpuTex.nativeHandle) {
            gpuTex.nativeHandle = texture.Get();
        }
"""
new5 = """                    d3dContext->Unmap(stagingTex.Get(), 0);
                    gpuTex.nativeHandle = buf.data();
                    gpuTex.isCpuBuffer = true;
                }
            }
        }
        if (!gpuTex.nativeHandle) {
            gpuTex.nativeHandle = texture.Get();
            gpuTex.isCpuBuffer = false;
        }
"""
assert old5 in s, "WGCCapture isCpu anchor missing"
s = s.replace(old5, new5, 1)

with io.open(p, "w", encoding="utf-8", newline="") as f:
    f.write(s)
print("WGCCapture.cpp OK")

# 3. Presenter.cpp: distinguish CPU vs GPU nativeHandle
p = r"D:\lingjing\src\pipeline\Presenter.cpp"
with io.open(p, "r", encoding="utf-8") as f:
    s = f.read()

old6 = """        // D3D11 纹理（WGC 捕获帧）读回
        if (frame.nativeHandle && d3dDevice_ && d3dContext_) {
"""
new6 = """        // D3D11 纹理（WGC 捕获帧）读回（仅当 nativeHandle 是 GPU 纹理，
        // CPU 读回缓冲会走下方 memcpy 路径，避免把内存指针当纹理调用导致崩溃）
        if (!frame.isCpuBuffer && frame.nativeHandle && d3dDevice_ && d3dContext_) {
"""
assert old6 in s, "Presenter gpu anchor missing"
s = s.replace(old6, new6, 1)

old7 = """        // CPU 像素缓冲（nativeHandle 指向 BGRA 内存）
        if (frame.nativeHandle && fw > 0 && fh > 0) {
"""
new7 = """        // CPU 像素缓冲（nativeHandle 指向 BGRA 内存）
        if (frame.isCpuBuffer && frame.nativeHandle && fw > 0 && fh > 0) {
"""
assert old7 in s, "Presenter cpu anchor missing"
s = s.replace(old7, new7, 1)

with io.open(p, "w", encoding="utf-8", newline="") as f:
    f.write(s)
print("Presenter.cpp OK")
