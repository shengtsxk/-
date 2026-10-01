p = r"D:\lingjing\src\ui\MainWindow.cpp"
s = open(p, encoding="utf-8").read()
old = "                cfg.captureConfig.backend = (backend == 2)"
new = "                cfg.captureConfig.preferredBackend = (backend == 2)"
if old in s:
    s = s.replace(old, new); print("fixed backend field")
else:
    print("MISS")
open(p, "w", encoding="utf-8", newline="").write(s)
