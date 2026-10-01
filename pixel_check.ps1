Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32 {
    [DllImport("user32.dll")] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
$h = (Get-Process -Name "Lingjing" -ErrorAction SilentlyContinue | Select-Object -First 1).MainWindowHandle
if (-not $h) { Write-Output "no window"; exit 1 }
$r = New-Object Win32+RECT
[Win32]::GetWindowRect([IntPtr]$h, [ref]$r) | Out-Null
$w = $r.R - $r.L; $hh = $r.B - $r.T
Write-Output ("window: " + $w + "x" + $hh + " @ " + $r.L + "," + $r.T)
$bmp = New-Object System.Drawing.Bitmap $w, $hh
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc()
[Win32]::PrintWindow([IntPtr]$h, $dc, 2) | Out-Null
$g.ReleaseHdc($dc)
$bmp.Save("D:\lingjing\settings_pixel.png")
# 采样：设置面板区域（窗口宽中间、从顶 20%~60% 高度）
$pts = @(
    @{n="panel-top-left"; x=[int]($w*0.30); y=[int]($hh*0.22)},
    @{n="panel-mid";      x=[int]($w*0.50); y=[int]($hh*0.35)},
    @{n="panel-groupbox"; x=[int]($w*0.30); y=[int]($hh*0.32)},
    @{n="panel-right";    x=[int]($w*0.70); y=[int]($hh*0.35)},
    @{n="panel-low";      x=[int]($w*0.50); y=[int]($hh*0.55)}
)
foreach ($p in $pts) {
    $c = $bmp.GetPixel($p.x, $p.y)
    Write-Output ($p.n + ": rgb(" + $c.R + "," + $c.G + "," + $c.B + ")")
}
$g.Dispose()
