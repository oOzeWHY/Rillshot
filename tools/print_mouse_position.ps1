Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class CursorNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct POINT {
        public int X;
        public int Y;
    }
    [DllImport("user32.dll")]
    public static extern bool GetCursorPos(out POINT lpPoint);
}
"@

Write-Host "Move the mouse to a target point. Press Ctrl+C to stop. Coordinates are physical screen pixels."
while ($true) {
    $p = New-Object CursorNative+POINT
    [void][CursorNative]::GetCursorPos([ref]$p)
    Write-Host ("x={0}, y={1}" -f $p.X, $p.Y)
    Start-Sleep -Milliseconds 500
}
