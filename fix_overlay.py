p = r"D:\lingjing\src\pipeline\Presenter.cpp"
s = open(p, encoding="utf-8").read()

# 1) 初始位置：覆盖目标窗口客户区
old1 = """        RECT wr = {};
        GetWindowRect(hTarget, &wr);
        int posX = wr.left + (wr.right - wr.left) + 10;
        int posY = wr.top;
"""
new1 = """        // 覆盖到目标窗口客户区上方（无边框贴合）
        POINT origin = {0, 0};
        ClientToScreen(hTarget, &origin);
        int posX = origin.x;
        int posY = origin.y;
"""
if old1 in s:
    s = s.replace(old1, new1, 1); print("pos1 fixed")
else:
    print("MISS pos1")

# 2) 无边框 + 置顶样式
old2 = """        hOutputWnd_ = CreateWindowExW(0, L"LingjingOutputWnd", L"灵境 输出画面",
            WS_OVERLAPPEDWINDOW,
            posX, posY, (int)outW_, (int)outH_,
            nullptr, nullptr, hInst, nullptr);
"""
new2 = """        hOutputWnd_ = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"LingjingOutputWnd", L"灵境 输出画面",
            WS_POPUP,
            posX, posY, (int)outW_, (int)outH_,
            nullptr, nullptr, hInst, nullptr);
"""
if old2 in s:
    s = s.replace(old2, new2, 1); print("style2 fixed")
else:
    print("MISS style2")

# 3) followTargetWindow：覆盖到目标客户区
old3 = """        RECT twr = {};
        RECT tcr = {};
        if (!GetWindowRect(hTarget, &twr) || !GetClientRect(hTarget, &tcr)) return;
        if (tcr.right <= 0 || tcr.bottom <= 0) return;

        const int tgtW = tcr.right - tcr.left;
        const int tgtH = tcr.bottom - tcr.top;
        const int newX = twr.right + 10;   // 输出窗口贴在目标窗口右侧
        const int newY = twr.top;
"""
new3 = """        RECT tcr = {};
        if (!GetClientRect(hTarget, &tcr)) return;
        if (tcr.right <= 0 || tcr.bottom <= 0) return;

        // 无边框覆盖：输出窗口完全覆盖目标窗口客户区
        POINT origin = {0, 0};
        ClientToScreen(hTarget, &origin);

        const int tgtW = tcr.right - tcr.left;
        const int tgtH = tcr.bottom - tcr.top;
        const int newX = origin.x;
        const int newY = origin.y;
"""
if old3 in s:
    s = s.replace(old3, new3, 1); print("follow3 fixed")
else:
    print("MISS follow3")

open(p, "w", encoding="utf-8", newline="").write(s)
