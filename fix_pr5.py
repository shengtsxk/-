p = r"D:\lingjing\src\pipeline\Presenter.cpp"
s = open(p, encoding="utf-8").read()

old = """        HDC hdcWin = GetWindowDC(hwnd);
        if (!hdcWin) return false;

        HDC hdcMem = CreateCompatibleDC(hdcWin);
        HBITMAP bmp = CreateCompatibleBitmap(hdcWin, w, h);
        HGDIOBJ oldBmp = SelectObject(hdcMem, bmp);

        // BitBlt 从屏幕直接读（快、不挂起）；PrintWindow 仅作回退（PW_RENDERFULLCONTENT 对动画窗口会等待重绘挂起）
        BOOL ok = BitBlt(hdcMem, 0, 0, w, h, hdcWin, 0, 0, SRCCOPY);
        if (!ok) {
            ok = PrintWindow(hwnd, hdcMem, PW_RENDERFULLCONTENT);
        }
"""
new = """        HDC hdcWin = GetDC(hwnd);   // 客户区 DC：与外部验证一致，抓取客户区内容
        if (!hdcWin) return false;

        HDC hdcMem = CreateCompatibleDC(hdcWin);
        HBITMAP bmp = CreateCompatibleBitmap(hdcWin, w, h);
        HGDIOBJ oldBmp = SelectObject(hdcMem, bmp);

        // BitBlt 从窗口客户区读（快、不挂起）；PrintWindow 仅作回退（PW_RENDERFULLCONTENT 对动画窗口会等待重绘挂起）
        BOOL ok = BitBlt(hdcMem, 0, 0, w, h, hdcWin, 0, 0, SRCCOPY);
        if (!ok) {
            ok = PrintWindow(hwnd, hdcMem, PW_RENDERFULLCONTENT);
        }
        if (!ok) {
            SelectObject(hdcMem, oldBmp);
            DeleteObject(bmp);
            DeleteDC(hdcMem);
            ReleaseDC(hwnd, hdcWin);
            return false;   // 抓帧失败：本次不输出，避免黑屏
        }
"""
if old in s:
    s = s.replace(old, new); print("grabTargetFrame fixed")
else:
    print("MISS")
open(p, "w", encoding="utf-8", newline="").write(s)
