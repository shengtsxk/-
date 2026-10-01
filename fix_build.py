# 1. Presenter.cpp: 去掉重复 namespace 行
p = r"D:\lingjing\src\pipeline\Presenter.cpp"
s = open(p, encoding="utf-8").read()
old = "} // namespace Lingjing\n} // namespace Lingjing\n"
if old in s:
    s = s.replace(old, "} // namespace Lingjing\n")
    print("dup namespace removed")
open(p, "w", encoding="utf-8", newline="").write(s)

# 2. Presenter.cpp: cbd/sd 声明移到函数开头（避免 goto 跳过初始化）
s = open(p, encoding="utf-8").read()
old2 = """        d3dDevice_ = dev;
        d3dContext_ = ctx;

        // ---- 编译着色器（全屏三角形顶点 + 双纹理 alpha 混合像素） ----"""
new2 = """        d3dDevice_ = dev;
        d3dContext_ = ctx;

        D3D11_BUFFER_DESC cbd = {};
        D3D11_SAMPLER_DESC sd = {};

        // ---- 编译着色器（全屏三角形顶点 + 双纹理 alpha 混合像素） ----"""
if old2 in s:
    s = s.replace(old2, new2); print("cbd/sd declared early")
else:
    print("MISS2")

old3 = """        // ---- 常量缓冲（alpha） ----
        D3D11_BUFFER_DESC cbd = {};
        cbd.ByteWidth = 16;"""
new3 = """        // ---- 常量缓冲（alpha） ----
        cbd.ByteWidth = 16;"""
if old3 in s:
    s = s.replace(old3, new3); print("cbd init removed")
else:
    print("MISS3")

old4 = """        // ---- 采样器 ----
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;"""
new4 = """        // ---- 采样器 ----
        sd.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;"""
if old4 in s:
    s = s.replace(old4, new4); print("sd init removed")
else:
    print("MISS4")
open(p, "w", encoding="utf-8", newline="").write(s)

# 3. SettingsDialog.cpp: 加 QCoreApplication include
p2 = r"D:\lingjing\src\ui\SettingsDialog.cpp"
s2 = open(p2, encoding="utf-8").read()
old5 = '#include <QSettings>\n#include <QStandardPaths>'
new5 = '#include <QSettings>\n#include <QStandardPaths>\n#include <QCoreApplication>'
if old5 in s2:
    s2 = s2.replace(old5, new5); print("QCoreApplication included")
else:
    print("MISS5")
open(p2, "w", encoding="utf-8", newline="").write(s2)

# 4. MainWindow.cpp: DXGI -> DXGI_DD
p3 = r"D:\lingjing\src\ui\MainWindow.cpp"
s3 = open(p3, encoding="utf-8").read()
old6 = "                    ? CaptureBackend::DXGI"
new6 = "                    ? CaptureBackend::DXGI_DD"
if old6 in s3:
    s3 = s3.replace(old6, new6); print("DXGI_DD fixed")
else:
    print("MISS6")
open(p3, "w", encoding="utf-8", newline="").write(s3)
