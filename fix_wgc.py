p = r"D:\lingjing\src\capture\WGCCapture.cpp"
s = open(p, encoding="utf-8").read()
old = "        frameCv.notify_one();\n        if (cbCount % 30 == 1) LOG_INFO(\"WGC step: after notify\");"
new = "        frameCv.notify_one();\n        if (cbCount % 30 == 1) LOG_INFO(\"WGC step: after notify\");\n\n        // 提前释放 WGC 帧对象，避免函数尾析构阶段异常逃逸\n        frame = nullptr;"
if old in s:
    s = s.replace(old, new)
    print("patched")
else:
    print("MISS")
open(p, "w", encoding="utf-8", newline="").write(s)
