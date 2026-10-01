import ctypes, struct
from ctypes import wintypes as w
u32 = ctypes.windll.user32
g32 = ctypes.windll.gdi32
EnumWindows = u32.EnumWindows
WNDENUMPROC = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
def find(cls):
    found = []
    def cb(h, l):
        b = ctypes.create_unicode_buffer(256)
        u32.GetClassNameW(h, b, 256)
        if b.value == cls: found.append(h)
        return True
    u32.EnumWindows(WNDENUMPROC(cb), 0)
    return found[0] if found else None
def rect(h):
    r = w.RECT(); u32.GetWindowRect(h, ctypes.byref(r)); return (r.left, r.top, r.right-r.left, r.bottom-r.top)
def shot(h, path):
    x,y,wd,ht = rect(h)
    dc = u32.GetDC(h); mem = g32.CreateCompatibleDC(dc); bmp = g32.CreateCompatibleBitmap(dc, wd, ht)
    g32.SelectObject(mem, bmp); g32.BitBlt(mem,0,0,wd,ht,dc,0,0,0x00CC0020)
    class BM: pass
    bi = (ctypes.c_byte*40)()
    struct.pack_into('<IiiHHIIiiII', bi, 0, 40, wd, ht, 1, 32, 0, wd*ht*4, 0,0,0,0)
    buf = (ctypes.c_ubyte*(wd*ht*4))()
    g32.GetDIBits(mem, bmp, 0, ht, buf, bi, 0)
    # 顶层像素（看是否颠倒：源动画左上应亮色）
    top = [buf[i] for i in range(0,4)]
    g32.DeleteObject(bmp); g32.DeleteDC(mem); u32.ReleaseDC(h, dc)
    import struct as st
    # 保存 bmp 简单格式
    with open(path,'wb') as f:
        f.write(b'BM'+struct.pack('<I', 54+wd*ht*4)+b'\x00\x00\x00\x00'+struct.pack('<I',54)+struct.pack('<IiiHHIIiiII',40,wd,ht,1,32,0,wd*ht*4,0,0,0,0))
        rows = [bytes(buf[i*wd*4:(i+1)*wd*4]) for i in range(ht)]
        for r in reversed(rows): f.write(r)
    return (wd, ht, top)
src = find("LingjingSelftestSourceWnd")
out = find("LingjingOutputWnd")
print("source rect:", rect(src) if src else None)
print("output rect:", rect(out) if out else None)
if out:
    shot(out, r"D:\lingjing\outC.bmp")
    shot(src, r"D:\lingjing\srcC.bmp")
