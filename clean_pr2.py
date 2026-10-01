p = r"D:\lingjing\src\pipeline\Presenter.cpp"
lines = open(p, encoding="utf-8").read().splitlines(keepends=True)
out = []
removed = 0
i = 0
while i < len(lines):
    ln = lines[i]
    # 删除被截断的多行 LOG 残留参数行
    if re.search(r'^\s*(w, h, ok \? 1 : 0, dib,|curFrameBits_\[0\],|cw, ch, \(unsigned\)outW_,|blend\[0\],|out\[0\], out\[1\], out\[2\], out\[3\], rowpitch|prev\[0\], prev\[1\], prev\[2\], cur\[0\], cur\[1\], cur\[2\]\);|alpha, w, h, prev|out\[0\], out\[1\], out\[2\], out\[3\]\);|alpha, w, h|prev=%02X%02X%02X cur=%02X%02X%02X",|alpha=%.2f w=%u h=%u prev=%02X)', ln) or re.search(r'\[dbg\]', ln):
        removed += 1
        i += 1
        continue
    out.append(ln)
    i += 1
open(p, "w", encoding="utf-8", newline="").write("".join(out))
print(f"removed {removed}")
