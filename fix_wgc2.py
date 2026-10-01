p = r"D:\lingjing\src\capture\WGCCapture.cpp"
s = open(p, encoding="utf-8").read()
old = "        // 提前释放 WGC 帧对象，避免函数尾析构阶段异常逃逸\n        frame = nullptr;"
new = "        // 提前释放 WGC 帧/表面/访问/纹理对象，避免函数尾析构阶段异常逃逸\n        frame = nullptr;\n        surface = nullptr;\n        access = nullptr;\n        texture.Reset();"
if old in s:
    s = s.replace(old, new)
    print("patched")
else:
    print("MISS")
open(p, "w", encoding="utf-8", newline="").write(s)
