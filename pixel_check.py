import ctypes, ctypes.wintypes as wt, subprocess
from PIL import Image

u32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32

# 找灵境主窗口：枚举该进程的所有可见窗口取最大的
out = subprocess.check_output("powershell -NoProfile -Command \"(Get-Process -Name Lingjing -ErrorAction SilentlyContinue | Select-Object -First 1).Id\"", shell=True).decode().strip()
pid = int(out)
print("pid:", pid)

EnumWindowsProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
candidates = []
def cb(hwnd, lparam):
    if u32.IsWindowVisible(hwnd):
        p = wt.DWORD()
        u32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if p.value == pid:
            r = wt.RECT(); u32.GetWindowRect(hwnd, ctypes.byref(r))
            w = r.right - r.left; h = r.bottom - r.top
            if w > 200 and h > 200:
                candidates.append((w*h, hwnd))
    return True
u32.EnumWindows(EnumWindowsProc(cb), 0)
if not candidates:
    print("no visible window")
    raise SystemExit
candidates.sort(reverse=True)
hwnd = candidates[0][1]
rect = wt.RECT(); u32.GetWindowRect(hwnd, ctypes.byref(rect))
w, hh = rect.right - rect.left, rect.bottom - rect.top
print("window:", w, "x", hh)

hdc = u32.GetWindowDC(hwnd)
mdc = gdi32.CreateCompatibleDC(hdc)
hbm = gdi32.CreateCompatibleBitmap(hdc, w, hh)
gdi32.SelectObject(mdc, hbm)
u32.PrintWindow(hwnd, mdc, 2)

class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", ctypes.c_uint32), ("biWidth", ctypes.c_int32), ("biHeight", ctypes.c_int32),
                ("biPlanes", ctypes.c_uint16), ("biBitCount", ctypes.c_uint16),
                ("biCompression", ctypes.c_uint32), ("biSizeImage", ctypes.c_uint32),
                ("biXPelsPerMeter", ctypes.c_int32), ("biYPelsPerMeter", ctypes.c_int32),
                ("biClrUsed", ctypes.c_uint32), ("biClrImportant", ctypes.c_uint32)]
bih = BITMAPINFOHEADER()
bih.biSize = ctypes.sizeof(BITMAPINFOHEADER)
bih.biWidth = w; bih.biHeight = -hh; bih.biPlanes = 1; bih.biBitCount = 32; bih.biCompression = 0
buf = ctypes.create_string_buffer(w * hh * 4)
gdi32.GetDIBits(mdc, hbm, 0, hh, buf, ctypes.byref(bih), 0)
img = Image.frombuffer("RGB", (w, hh), buf, "raw", "BGRX", 0, 1)
img.save(r"D:\lingjing\settings_pixel.png")
pts = [("panel-top-left", int(w*0.30), int(hh*0.22)),
       ("panel-mid", int(w*0.50), int(hh*0.35)),
       ("panel-groupbox", int(w*0.30), int(hh*0.32)),
       ("panel-right", int(w*0.70), int(hh*0.35)),
       ("panel-low", int(w*0.50), int(hh*0.55))]
for n, x, y in pts:
    c = img.getpixel((x, y))
    print(n, "rgb", c)
u32.ReleaseDC(hwnd, hdc)
gdi32.DeleteObject(hbm)
gdi32.DeleteDC(mdc)
