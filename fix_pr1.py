p = r"D:\lingjing\src\pipeline\Presenter.cpp"
s = open(p, encoding="utf-8").read()

# 1. includes
old = """#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif"""
new = """#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#endif"""
if old in s:
    s = s.replace(old, new); print("inc ok")
else:
    print("inc MISS")

# 2. initialize 末尾 initGpuBlend
old2 = """        initialized_ = true;

        LOG_INFO("Presenter initialized: mode=%u, queueSize=%u, output=%ux%u",
            static_cast<uint32_t>(config.mode),
            config.maxQueueSize,
            outW_, outH_);

        return true;"""
new2 = """        initialized_ = true;

        // 初始化 GPU（D3D11）插帧混合后端；失败自动回退 CPU 混合
        initGpuBlend();

        LOG_INFO("Presenter initialized: mode=%u, queueSize=%u, output=%ux%u, gpuBlend=%d",
            static_cast<uint32_t>(config.mode),
            config.maxQueueSize,
            outW_, outH_,
            gpuBlendReady_ ? 1 : 0);

        return true;"""
if old2 in s:
    s = s.replace(old2, new2); print("init ok")
else:
    print("init MISS")

# 3. shutdown 加 shutdownGpuBlend
old3 = """        prevFrameBits_.clear();
        curFrameBits_.clear();
        havePrevBits_ = false;

        initialized_ = false;"""
new3 = """        prevFrameBits_.clear();
        curFrameBits_.clear();
        havePrevBits_ = false;

        shutdownGpuBlend();

        initialized_ = false;"""
if old3 in s:
    s = s.replace(old3, new3); print("shutdown ok")
else:
    print("shutdown MISS")

# 4. presentIfReady 加 followTargetWindow
old4 = """    bool Presenter::presentIfReady() {
        if (!initialized_) return false;
        if (!grabTargetFrame()) return false;
        presentInterpolated();
        return true;
    }"""
new4 = """    bool Presenter::presentIfReady() {
        if (!initialized_) return false;
        followTargetWindow();          // 输出窗口跟随目标窗口位置/尺寸
        if (!grabTargetFrame()) return false;
        presentInterpolated();
        return true;
    }"""
if old4 in s:
    s = s.replace(old4, new4); print("follow call ok")
else:
    print("follow MISS")

open(p, "w", encoding="utf-8", newline="").write(s)
