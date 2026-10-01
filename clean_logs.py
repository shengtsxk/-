import re

markers = [
    r'LOG_INFO\("\[pf\]',
    r'LOG_INFO\("\[init-step\]',
    r'LOG_INFO\("\[crash-check\]',
    r'LOG_INFO\("\[sd\]',
    r'LOG_INFO\("\[pr\]',
    r'if \(\(\+\+cbCount\) % 30 == 1\) LOG_INFO\("WGC callback',
    r'if \(cbCount % 30 == 1\) LOG_INFO\("WGC step',
    r'static uint32_t cbCount = 0;',
]

for path in [r"D:\lingjing\src\pipeline\FrameGenPipeline.cpp",
             r"D:\lingjing\src\pipeline\Presenter.cpp",
             r"D:\lingjing\src\capture\WGCCapture.cpp"]:
    lines = open(path, encoding="utf-8").read().splitlines(keepends=True)
    out = []
    removed = 0
    for ln in lines:
        hit = False
        for m in markers:
            if re.search(m, ln):
                hit = True
                break
        if hit:
            removed += 1
            # if the line had a multi-line LOG continuation, also drop following lines until ';'
            # (our LOGs are single-line, so fine)
            continue
        out.append(ln)
    open(path, "w", encoding="utf-8", newline="").write("".join(out))
    print(f"{path}: removed {removed} lines")
