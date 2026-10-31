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
    [DllImport("dwmapi.dll")] static extern int DwmGetWindowAttribute(IntPtr w, uint attribute, out Rect rect, int size);
    public static Rect VisibleFrame(IntPtr w) {
        Rect r; return DwmGetWindowAttribute(w,9,out r,Marshal.SizeOf<Rect>()) == 0 ? r : new Rect();
    }
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
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll",EntryPoint="GetWindowLongPtrW")] public static extern IntPtr WindowLong(IntPtr h, int index);
    public static long Query(IntPtr editor, uint message, int index=0) {
        UIntPtr result;
        if (SendMessageTimeout(editor,message,new IntPtr(index),IntPtr.Zero,2,3000,out result)==IntPtr.Zero) throw new Exception("Scintilla query timed out: " + message);
        return unchecked((long)result.ToUInt64());
    }
    public static void PrintClient(IntPtr editor, IntPtr dc) {
        UIntPtr result;
        if (SendMessageTimeout(editor,0x0318,dc,new IntPtr(4),2,4000,out result)==IntPtr.Zero) throw new Exception("WM_PRINTCLIENT timed out");
    }
    [StructLayout(LayoutKind.Sequential)] struct GuiThreadInfo {
        public uint cbSize, flags;
        public IntPtr active, focus, capture, menuOwner, moveSize, caret;
        public Rect caretRect;
    }
    [DllImport("user32.dll")] static extern bool GetGUIThreadInfo(uint thread, ref GuiThreadInfo info);
    // Windows x64 INPUT is 40 bytes, with its union starting at offset 8.
    [StructLayout(LayoutKind.Explicit,Size=40)] struct Input64 {
        [FieldOffset(0)] public uint type;
        [FieldOffset(8)] public ushort vk;
        [FieldOffset(12)] public uint flags;
    }
    [DllImport("user32.dll",SetLastError=true)] static extern uint SendInput(uint count, Input64[] input, int size);
    public static void SelectAllUsingKeyboard(IntPtr parent, IntPtr editor, int pid) {
        uint owner; uint tid=GetWindowThreadProcessId(editor,out owner);
        GuiThreadInfo info=new GuiThreadInfo(); info.cbSize=(uint)Marshal.SizeOf<GuiThreadInfo>();
        if(IntPtr.Size!=8 || owner!=(uint)pid || GetForegroundWindow()!=parent || !GetGUIThreadInfo(tid,ref info) || info.focus!=editor)
            throw new Exception("Ctrl+A test requires its own foreground editor to hold keyboard focus");
        var keys=new Input64[] {new Input64{type=1,vk=0x11}, new Input64{type=1,vk=0x41},
            new Input64{type=1,vk=0x41,flags=2}, new Input64{type=1,vk=0x11,flags=2}};
        if(SendInput(4,keys,Marshal.SizeOf<Input64>())!=4) throw new Exception("SendInput Ctrl+A failed: " + Marshal.GetLastWin32Error());
    }
    public static void Invalidate(IntPtr w) { RedrawWindow(w,IntPtr.Zero,IntPtr.Zero,0x581); }
}
'@

function Get-WindowBitmap([IntPtr]$Window, [string]$Mode) {
    $r = [Np4Capture]::Bounds($Window)
    $bitmap = [Drawing.Bitmap]::new($r.R-$r.L, $r.B-$r.T, [Drawing.Imaging.PixelFormat]::Format32bppRgb)
    $g = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $g.Clear([Drawing.Color]::Fuchsia)
        if ($Mode -eq 'desktop') {
            $g.CopyFromScreen($r.L,$r.T,0,0,$bitmap.Size)
        } else {
            $dc = $g.GetHdc()
            try {
                if ($Mode -eq 'child-wm-printclient') { [Np4Capture]::PrintClient($Window,$dc) }
                else { [void][Np4Capture]::PrintWindow($Window,$dc,2) }
            } finally { $g.ReleaseHdc($dc) }
        }
    } catch {
        $bitmap.Dispose()
        throw
    } finally { $g.Dispose() }
    return $bitmap
}

function Measure-EditorPixels($Bitmap,[int]$Left,[int]$Top,[int]$Width,[int]$Height) {
    # Count ink and rows inside the body, separate near-grey glyphs from colours.
    # Starting a little beyond the left margin excludes its vertical divider.
    $white=0; $ink=0; $colour=0; $rows=0; $total=0; $black=0; $pink=0
    $x0=[Math]::Max(0,$Left+23); $y0=[Math]::Max(0,$Top+1)
    $x1=[Math]::Min($Bitmap.Width,$Left+[Math]::Min(390,$Width-20))
    $y1=[Math]::Min($Bitmap.Height,$Top+[Math]::Min(185,$Height-18))
    for ($y=$y0; $y -lt $y1; $y+=2) {
        $rowInk=0
        for ($x=$x0; $x -lt $x1; $x+=2) {
            $c=$Bitmap.GetPixel($x,$y); $total++
            $max=[Math]::Max($c.B,[Math]::Max($c.R,$c.G)); $min=[Math]::Min($c.B,[Math]::Min($c.R,$c.G))
            if ($min -gt 235) { $white++ }
            if ($max -lt 15) { $black++ }
            if ($c.R -gt 245 -and $c.G -lt 8 -and $c.B -gt 245) { $pink++; continue }
            if ($max -lt 195 -or ($min -lt 145 -and $max -lt 230)) {
                $ink++; $rowInk++; if ($max-$min -gt 20) { $colour++ }
            }
        }
        if ($rowInk -ge 3) { $rows++ }
    }
    return [pscustomobject]@{samples=$total; white=$white; ink=$ink; colour=$colour; greyInk=$ink-$colour; inkRows=$rows; black=$black; sentinel=$pink}
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
        $bytes = [Np4Capture]::Query($child,2006,0)
        if ($File -and $bytes -ne (Get-Item -LiteralPath $File).Length) { throw 'The input did not reach the fork document.' }
        $outer=[Np4Capture]::Bounds($wnd); $inner=[Np4Capture]::Bounds($child)
        $frame=[Np4Capture]::VisibleFrame($wnd)
        $frameValid=$frame.L -ge $outer.L -and $frame.T -ge $outer.T -and $frame.R -le $outer.R -and $frame.B -le $outer.B -and $frame.R -gt $frame.L -and $frame.B -gt $frame.T
        $left=$inner.L-$outer.L; $top=$inner.T-$outer.T; $width=$inner.R-$inner.L; $height=$inner.B-$inner.T
        $id=[IO.Path]::GetFileNameWithoutExtension($Target)
        $raw=[IO.Directory]::CreateDirectory((Join-Path ([IO.Path]::GetDirectoryName($Target)) 'diagnostic')).FullName
        $fg=[Np4Capture]::GetForegroundWindow()
        $state=[ordered]@{name=$id; hwnd=$wnd.ToInt64(); child=$child.ToInt64(); foreground=$fg.ToInt64();
            parentRect=@($outer.L,$outer.T,$outer.R,$outer.B); visibleFrame=@($frame.L,$frame.T,$frame.R,$frame.B); childRect=@($inner.L,$inner.T,$inner.R,$inner.B);
            childClient=[Np4Capture]::ClientSize($child); parentStyle=('0x{0:X}' -f [Np4Capture]::WindowLong($wnd,-16).ToInt64());
            childStyle=('0x{0:X}' -f [Np4Capture]::WindowLong($child,-16).ToInt64()); bytes=$bytes;
            selectionStart=[Np4Capture]::Query($child,2143,0); selectionEnd=[Np4Capture]::Query($child,2145,0);
            caret=[Np4Capture]::Query($child,2008,0); anchor=[Np4Capture]::Query($child,2009,0); focus=[Np4Capture]::Query($child,2381,0);
            lexer=[Np4Capture]::Query($child,4002,0); endStyled=[Np4Capture]::Query($child,2028,0);
            firstVisibleLine=[Np4Capture]::Query($child,2152,0); xOffset=[Np4Capture]::Query($child,2398,0);
            linesOnScreen=[Np4Capture]::Query($child,2370,0); frames=@(); selected=$null}
        if($File) {
            $source=[IO.File]::ReadAllText($File); $styles=[ordered]@{}
            foreach($token in 'editor','Notepad4','11','false') {
                $index=$source.IndexOf($token,[StringComparison]::Ordinal)
                $pos=[Text.Encoding]::UTF8.GetByteCount($source.Substring(0,$index))
                $style=[Np4Capture]::Query($child,2038,$pos)
                $styles[$token]=@{style=$style; pos=$pos; colourref=('0x{0:X6}' -f [Np4Capture]::Query($child,2481,[int]$style))}
            }
            $state['styles']=$styles
        }
        $statePath=Join-Path $raw "$id-state.json"
        $state | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $statePath -Encoding utf8
        Write-Host "Capture state $($state | ConvertTo-Json -Depth 7 -Compress)"
        $bestRank=0
        foreach($mode in 'desktop','parent-printwindow','child-printwindow','child-wm-printclient') {
            $isChild=$mode.StartsWith('child-'); $handle=if($isChild){$child}else{$wnd}
            $x=if($isChild){0}else{$left}; $y=if($isChild){0}else{$top}
            $img=$null
            try {
                $img=Get-WindowBitmap $handle $mode
                $filename=Join-Path $raw "$id-$mode.png"
                $img.Save($filename,[Drawing.Imaging.ImageFormat]::Png)
                $quality=Measure-EditorPixels $img $x $y $width $height
                $state.frames+=@{mode=$mode; width=$img.Width; height=$img.Height; pixels=$quality}
                Write-Host "$id $mode $($quality | ConvertTo-Json -Compress)"
                $expectedRows=if ($File) {10} else {2}
                $textPresent=$quality.ink -ge 10 -and $quality.inkRows -ge $expectedRows
                $ok=$quality.samples -gt 300 -and $quality.white -gt 0.45*$quality.samples -and (!$bytes -or $textPresent)
                $rank=switch($mode){ 'desktop' {if($fg -eq $wnd){4}else{0}} 'parent-printwindow'{3} 'child-printwindow'{2} default{1} }
                if($ok -and $rank -gt $bestRank) {
                    if ($isChild) {
                        Copy-Item -LiteralPath $filename -Destination $Target -Force
                    } elseif ($frameValid) {
                        # GetWindowRect includes the DWM transparent resize border on modern desktops.
                        # Keep only the actually visible native frame; preserve the uncropped original in diagnostics.
                        $bounds=[Drawing.Rectangle]::new($frame.L-$outer.L,$frame.T-$outer.T,$frame.R-$frame.L,$frame.B-$frame.T)
                        $visible=$img.Clone($bounds,[Drawing.Imaging.PixelFormat]::Format32bppRgb)
                        try { $visible.Save($Target,[Drawing.Imaging.ImageFormat]::Png) } finally { $visible.Dispose() }
                    } else { continue }
                    $state.selected=$mode; $bestRank=$rank
                }
            } catch { $state.frames+=@{mode=$mode; error=$_.Exception.Message}; Write-Warning "$id $mode capture failed: $($_.Exception.Message)" }
            finally { if($img){ $img.Dispose() } }
        }
        $state | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $statePath -Encoding utf8
        if ($state.selectionStart -ne $state.selectionEnd -or $state.caret -ne $state.anchor) {
            throw "Unexpected selection in $id (start=$($state.selectionStart) end=$($state.selectionEnd) anchor=$($state.anchor) caret=$($state.caret)). Original frames preserved."
        }
        if (!$bestRank) { throw "No painted editor body detected in $id. Original frames are in $raw." }
        Write-Host "Selected $id $($state.selected)"
        # Observe initial state and save all screenshots before sending any keyboard input.
        if ($File) {
            [Np4Capture]::SelectAllUsingKeyboard($wnd,$child,$p.Id)
            $until=[DateTime]::UtcNow.AddSeconds(3)
            do {
                Start-Sleep -Milliseconds 40
                $selStart=[Np4Capture]::Query($child,2143,0); $selEnd=[Np4Capture]::Query($child,2145,0)
            } while (($selStart -ne 0 -or $selEnd -ne $bytes) -and [DateTime]::UtcNow -lt $until)
            $state['afterCtrlA']=@{selectionStart=$selStart; selectionEnd=$selEnd; expectedEnd=$bytes}
            $state | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $statePath -Encoding utf8
            if ($selStart -ne 0 -or $selEnd -ne $bytes) { throw "Real Ctrl+A selected [$selStart,$selEnd), expected [0,$bytes)" }
            Write-Host "PASS real Ctrl+A ${id}: selected [0,$bytes) after the screenshots"
        }
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
        $fltkState = Get-Content -LiteralPath (Join-Path $Directory 'diagnostic/fltk-state.json') -Raw | ConvertFrom-Json
        $wxState = Get-Content -LiteralPath (Join-Path $Directory 'diagnostic/wx-state.json') -Raw | ConvertFrom-Json
        $fltkLabel = if($fltkState.selected -like 'child-*'){'FLTK  |  editor area'}else{'FLTK'}
        $wxLabel = if($wxState.selected -like 'child-*'){'wxWidgets (wxMSW)  |  editor area'}else{'wxWidgets (wxMSW)'}
        $g.DrawString($fltkLabel, $font, $brush, 12, 10)
        $g.DrawString($wxLabel, $font, $brush, $w+24, 10)
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
    $failed=@()
    foreach ($pair in @(@('fltk',$FltkExe),@('wx',$WxExe))) {
        $name, $exePath = $pair
        $exe = Get-Item -LiteralPath $exePath
        foreach ($startup in @($false,$true)) {
            $target=if($startup){"$name-startup.png"}else{"$name.png"}
            $file=if($startup){''}else{$sample}
            try { [void](Save-WindowCapture $exe (Join-Path $out $target) $file) }
            catch { Write-Warning "$target $($_.Exception.Message)"; $failed+=$target }
        }
    }
    if($failed.Count){ throw "Capture(s) failed: $($failed -join ', '). See original PNG and JSON diagnostics." }
    Join-Captures $out
} finally { if ($previousDpi -ne [IntPtr]::Zero) { [void][Np4Capture]::SetThreadDpiAwarenessContext($previousDpi) } }
