p = r"D:\lingjing\src\pipeline\Presenter.cpp"
s = open(p, encoding="utf-8").read()

fixes = [
    ("""        int dib = GetDIBits(hdcMem, bmp, 0, h, curFrameBits_.data(), &bi, DIB_RGB_COLORS);

            w, h, ok ? 1 : 0, dib,
            curFrameBits_[0], curFrameBits_[1], curFrameBits_[2], curFrameBits_[3]);

        SelectObject(hdcMem, oldBmp);""",
     """        GetDIBits(hdcMem, bmp, 0, h, curFrameBits_.data(), &bi, DIB_RGB_COLORS);

        SelectObject(hdcMem, oldBmp);"""),
    ("""            int sd = StretchDIBits(hdcOut,
                0, 0, cw, ch,
                0, 0, (int)outW_, (int)outH_,
                blend.data(), &bi, DIB_RGB_COLORS, SRCCOPY);

                cw, ch, (unsigned)outW_, (unsigned)outH_, sd,
                blend[0], blend[1], blend[2], blend[3]);

            ReleaseDC(hOut, hdcOut);""",
     """            StretchDIBits(hdcOut,
                0, 0, cw, ch,
                0, 0, (int)outW_, (int)outH_,
                blend.data(), &bi, DIB_RGB_COLORS, SRCCOPY);

            ReleaseDC(hOut, hdcOut);"""),
    ("""        if (w == 0 || h == 0) return false;

            alpha, w, h, prev[0], prev[1], prev[2], cur[0], cur[1], cur[2]);

        if (!ensureGpuTexturesImpl""",
     """        if (w == 0 || h == 0) return false;

        if (!ensureGpuTexturesImpl"""),
    ("""            dst += pitch;
        }
            out[0], out[1], out[2], out[3], ms2.RowPitch);
        ctx->Unmap(static_cast<ID3D11Resource*>(texStaging_), 0);""",
     """            dst += pitch;
        }
        ctx->Unmap(static_cast<ID3D11Resource*>(texStaging_), 0);"""),
]
for old, new in fixes:
    if old in s:
        s = s.replace(old, new); print("fixed")
    else:
        print("MISS: " + old[:60])
open(p, "w", encoding="utf-8", newline="").write(s)
