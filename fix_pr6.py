p = r"D:\lingjing\src\pipeline\Presenter.cpp"
s = open(p, encoding="utf-8").read()

old = """        HDC hdcWin = GetDC(hwnd);   // 客户区 DC：与外部验证一致，抓取客户区内容
        if (!hdcWin) return false;
"""
new = """        HDC hdcWin = GetDC(hwnd);   // 客户区 DC：与外部验证一致，抓取客户区内容
        if (!hdcWin) {
            LOG_WARN("[dbg] grabTargetFrame: GetDC failed");
            return false;
        }
"""
if old in s:
    s = s.replace(old, new); print("dbg1 ok")
else:
    print("MISS1")

old2 = """        if (!ok) {
            SelectObject(hdcMem, oldBmp);
            DeleteObject(bmp);
            DeleteDC(hdcMem);
            ReleaseDC(hwnd, hdcWin);
            return false;   // 抓帧失败：本次不输出，避免黑屏
        }
"""
new2 = """        if (!ok) {
            LOG_WARN("[dbg] grabTargetFrame: BitBlt+PrintWindow both failed");
            SelectObject(hdcMem, oldBmp);
            DeleteObject(bmp);
            DeleteDC(hdcMem);
            ReleaseDC(hwnd, hdcWin);
            return false;   // 抓帧失败：本次不输出，避免黑屏
        }
"""
if old2 in s:
    s = s.replace(old2, new2); print("dbg2 ok")
else:
    print("MISS2")

old3 = """        GetDIBits(hdcMem, bmp, 0, h, curFrameBits_.data(), &bi, DIB_RGB_COLORS);

        SelectObject(hdcMem, oldBmp);
"""
new3 = """        int dib = GetDIBits(hdcMem, bmp, 0, h, curFrameBits_.data(), &bi, DIB_RGB_COLORS);

        LOG_INFO("[dbg] grab: w=%d h=%d bitblt=%d dib=%d pixel=%02X%02X%02X%02X",
            w, h, ok ? 1 : 0, dib,
            curFrameBits_[0], curFrameBits_[1], curFrameBits_[2], curFrameBits_[3]);

        SelectObject(hdcMem, oldBmp);
"""
if old3 in s:
    s = s.replace(old3, new3); print("dbg3 ok")
else:
    print("MISS3")

# presentInterpolated StretchDIBits 结果
old4 = """            StretchDIBits(hdcOut,
                0, 0, cw, ch,
                0, 0, (int)outW_, (int)outH_,
                blend.data(), &bi, DIB_RGB_COLORS, SRCCOPY);

            ReleaseDC(hOut, hdcOut);
"""
new4 = """            int sd = StretchDIBits(hdcOut,
                0, 0, cw, ch,
                0, 0, (int)outW_, (int)outH_,
                blend.data(), &bi, DIB_RGB_COLORS, SRCCOPY);

            LOG_INFO("[dbg] present: cw=%d ch=%d out=%ux%u sd=%d px=%02X%02X%02X%02X",
                cw, ch, (unsigned)outW_, (unsigned)outH_, sd,
                blend[0], blend[1], blend[2], blend[3]);

            ReleaseDC(hOut, hdcOut);
"""
if old4 in s:
    s = s.replace(old4, new4); print("dbg4 ok")
else:
    print("MISS4")

open(p, "w", encoding="utf-8", newline="").write(s)
