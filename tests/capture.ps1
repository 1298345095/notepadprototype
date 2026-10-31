# Capture the actual application windows on a native Windows desktop.
param(
    [Parameter(Mandatory)][string]$FltkExe,
    [Parameter(Mandatory)][string]$WxExe,
    [Parameter(Mandatory)][string]$OutDir
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class Np4Capture {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L,T,R,B; }
    delegate bool WindowProc(IntPtr window, IntPtr arg);
    [DllImport("user32.dll")] static extern bool EnumWindows(WindowProc proc, IntPtr arg);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr root, WindowProc proc, IntPtr arg);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr w, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr w, StringBuilder name, int size);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr w);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr w, out Rect r);
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr w, out Rect r);
    [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr w, IntPtr insertAfter, int x,int y,int width,int height,uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr w);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr w, IntPtr dc, uint flags);
    [DllImport("user32.dll")] static extern bool RedrawWindow(IntPtr w, IntPtr rect, IntPtr region, uint flags);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr SendMessageTimeout(IntPtr w, uint m, IntPtr wp, IntPtr lp, uint flags, uint timeout, out UIntPtr result);
    public static IntPtr[] Find(int processId) {
        IntPtr parent = IntPtr.Zero, editor = IntPtr.Zero;
        EnumWindows((w,a) => {
            uint owner; GetWindowThreadProcessId(w,out owner);
            if (owner!=(uint)processId || !IsWindowVisible(w)) return true;
            EnumChildWindows(w,(c,b) => {
                var cls=new StringBuilder(128); GetClassName(c,cls,cls.Capacity);
                if (cls.ToString()!="Scintilla" || !IsWindowVisible(c)) return true;
                parent=w; editor=c; return false;
            },IntPtr.Zero);
            return editor==IntPtr.Zero;
        },IntPtr.Zero);
        return new IntPtr[]{parent,editor};
    }
    public static Rect Bounds(IntPtr w) { Rect r; if (!GetWindowRect(w,out r)) throw new Exception("GetWindowRect failed"); return r; }
    public static void Resize(IntPtr w, int width,int height) {
        Rect r=Bounds(w), c; if(!GetClientRect(w,out c)) throw new Exception("GetClientRect failed");
        if(!SetWindowPos(w,IntPtr.Zero,32,32,width+r.R-r.L-c.R,height+r.B-r.T-c.B,0x0040)) throw new Exception("SetWindowPos failed");
    }
    public static int[] ClientSize(IntPtr w) { Rect c; GetClientRect(w,out c); return new int[]{c.R,c.B}; }
    public static long Length(IntPtr editor) {
        UIntPtr result;
        if(SendMessageTimeout(editor,2006,IntPtr.Zero,IntPtr.Zero,2,3000,out result)==IntPtr.Zero) throw new Exception("SCI_GETLENGTH timeout");
        return (long)result.ToUInt64();
    }
    public static void Invalidate(IntPtr w) { RedrawWindow(w,IntPtr.Zero,IntPtr.Zero,0x581); }
}
'@

function Get-WindowBitmap([IntPtr]$Window, [bool]$FromScreen) {
    $r = [Np4Capture]::Bounds($Window)
    $bitmap = [Drawing.Bitmap]::new($r.R-$r.L, $r.B-$r.T, [Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $g.Clear([Drawing.Color]::Fuchsia)
        if ($FromScreen) {
            $g.CopyFromScreen($r.L, $r.T, 0, 0, $bitmap.Size)
        } else {
            $dc = $g.GetHdc()
            try { [void][Np4Capture]::PrintWindow($Window, $dc, 2) } finally { $g.ReleaseHdc($dc) }
        }
    } finally { $g.Dispose() }
    return $bitmap
}

function Test-EditorPixels($Bitmap, [int]$Left, [int]$Top, [bool]$HasText) {
    # Inspect only the upper editor body, excluding the title, borders and scrollbars.
    $white = 0; $ink = 0
    for ($y = $Top + 2; $y -lt $Top + 122; $y += 2) {
        for ($x = $Left + 5; $x -lt $Left + 305; $x += 2) {
            $c = $Bitmap.GetPixel($x,$y)
            if ($c.R -gt 235 -and $c.G -gt 235 -and $c.B -gt 235) { $white++ }
            if ($c.G -lt 165 -and [Math]::Max($c.R, $c.B) -gt $c.G + 16 -and $c.R -lt 225) { $ink++ }
        }
    }
    return $white -gt 4500 -and (!$HasText -or $ink -gt 6)
}

function Save-WindowCapture([IO.FileInfo]$Exe, [string]$Target, [string]$File) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Exe.FullName
    $start.UseShellExecute = $false
    if ($File) { $start.ArgumentList.Add($File) }
    $p = [Diagnostics.Process]::Start($start)
    try {
        $limit = [DateTime]::UtcNow.AddSeconds(20)
        do {
            if ($p.HasExited) { throw "$($Exe.Name) exited before showing its editor (code $($p.ExitCode))" }
            $wnd, $child = [Np4Capture]::Find($p.Id)
            if ($child -ne [IntPtr]::Zero) { break }
            Start-Sleep -Milliseconds 120
        } while ([DateTime]::UtcNow -lt $limit)
        if ($child -eq [IntPtr]::Zero) { throw 'Missing visible Scintilla child HWND' }

        [Np4Capture]::Resize($wnd, 466, 250)
        [void][Np4Capture]::SetForegroundWindow($wnd)
        [Np4Capture]::Invalidate($wnd)
        Start-Sleep -Milliseconds 650
        $client = [Np4Capture]::ClientSize($wnd)
        if ($client[0] -ne 466 -or $client[1] -ne 250) { throw "Unexpected client dimensions: $client" }
        $bytes = [Np4Capture]::Length($child)
        if ($File -and $bytes -ne (Get-Item -LiteralPath $File).Length) { throw 'The input did not reach the fork document.' }
        $outer = [Np4Capture]::Bounds($wnd); $inner = [Np4Capture]::Bounds($child)
        $left = $inner.L - $outer.L; $top = $inner.T - $outer.T
        $saved = $false
        foreach ($screen in @($false,$true)) {
            $img = Get-WindowBitmap $wnd $screen
            try {
                if (Test-EditorPixels $img $left $top ($bytes -gt 0)) {
                    $img.Save($Target, [Drawing.Imaging.ImageFormat]::Png)
                    Write-Host "Captured $($Exe.Name) bytes=$bytes client=$client via $(if($screen){'screen'}else{'PrintWindow'})"
                    $saved = $true; break
                }
            } finally { $img.Dispose() }
        }
        if (!$saved) { throw "No correctly painted native editor in $($Exe.Name) capture." }
        return $bytes
    } finally {
        if (!$p.HasExited) { [void]$p.CloseMainWindow(); if (!$p.WaitForExit(2000)) { $p.Kill($true); $p.WaitForExit() } }
        $p.Dispose()
    }
}

function Join-Captures([string]$Directory) {
    $left = [Drawing.Bitmap]::new((Join-Path $Directory 'fltk.png'))
    $right = [Drawing.Bitmap]::new((Join-Path $Directory 'wx.png'))
    $w = [Math]::Max($left.Width, $right.Width)
    $h = [Math]::Max($left.Height, $right.Height)
    $canvas = [Drawing.Bitmap]::new(2*$w + 36, $h + 60)
    $g = [Drawing.Graphics]::FromImage($canvas)
    $font = [Drawing.Font]::new('Segoe UI', 16, [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)
    $brush = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(29,41,56))
    try {
        $g.Clear([Drawing.Color]::FromArgb(240,243,246))
        $g.TextRenderingHint = [Drawing.Text.TextRenderingHint]::AntiAliasGridFit
        $g.DrawString('FLTK', $font, $brush, 12, 10)
        $g.DrawString('wxWidgets (wxMSW)', $font, $brush, $w+24, 10)
        $g.DrawImageUnscaled($left,12,40)
        $g.DrawImageUnscaled($right,$w+24,40)
        $canvas.Save((Join-Path $Directory 'side-by-side.png'), [Drawing.Imaging.ImageFormat]::Png)
    } finally { $brush.Dispose(); $font.Dispose(); $g.Dispose(); $canvas.Dispose(); $left.Dispose(); $right.Dispose() }
}

$out = [IO.Directory]::CreateDirectory($OutDir).FullName
$sample = Join-Path $out 'appearance.json'
$text = @'
{
  "editor": "Notepad4",
  "profile": "Windows desktop",
  "font": { "family": "Consolas", "size": 11 },
  "lineNumbers": false,
  "indentWidth": 4,
  "encoding": "UTF-8",
  "languages": ["JSON", "C++", "Markdown"],
  "autosave": true,
  "fallback": null
}
'@
[IO.File]::WriteAllText($sample, $text.Replace("`r`n","`n"), [Text.UTF8Encoding]::new($false))
$previousDpi = [Np4Capture]::SetThreadDpiAwarenessContext([IntPtr](-4))
try {
    foreach ($pair in @(@('fltk',$FltkExe),@('wx',$WxExe))) {
        $name, $exePath = $pair
        $exe = Get-Item -LiteralPath $exePath
        [void](Save-WindowCapture $exe (Join-Path $out "$name.png") $sample)
        $initialBytes = Save-WindowCapture $exe (Join-Path $out "$name-startup.png") ''
        Write-Host "$name no-argument document bytes: $initialBytes"
    }
    Join-Captures $out
} finally { if ($previousDpi -ne [IntPtr]::Zero) { [void][Np4Capture]::SetThreadDpiAwarenessContext($previousDpi) } }
