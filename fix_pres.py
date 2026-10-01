p = r"D:\lingjing\src\pipeline\Presenter.cpp"
s = open(p, encoding="utf-8").read()
old = """    bool Presenter::presentIfReady() {
        return false;  // TEMP-DEBUG: disable output
    }"""
new = """    bool Presenter::presentIfReady() {
        if (!initialized_) return false;
        if (!grabTargetFrame()) return false;
        presentInterpolated();
        return true;
    }"""
if old in s:
    s = s.replace(old, new); print("patched")
else:
    print("MISS")
open(p, "w", encoding="utf-8", newline="").write(s)
