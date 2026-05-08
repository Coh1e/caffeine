# make_icons.ps1 — generate full.ico / empty.ico for Amped
# Renders a stylized lightning bolt at 16/32/48 px, packs PNG frames into
# multi-resolution .ico files. Also writes large PNG previews next to them
# so a human can eyeball the result before building.

Add-Type -AssemblyName System.Drawing

# Lightning bolt polygon, normalized [0..1] coords (origin top-left).
# 7 vertices: classic zigzag bolt with two kinks.
$boltPoints = @(
    [System.Drawing.PointF]::new(0.60, 0.05),  # 1 top point
    [System.Drawing.PointF]::new(0.25, 0.55),  # 2 descend down-left
    [System.Drawing.PointF]::new(0.50, 0.55),  # 3 kink (right inner)
    [System.Drawing.PointF]::new(0.35, 0.95),  # 4 bottom tip
    [System.Drawing.PointF]::new(0.75, 0.45),  # 5 ascend up-right
    [System.Drawing.PointF]::new(0.50, 0.45),  # 6 kink (left inner)
    [System.Drawing.PointF]::new(0.65, 0.05)   # 7 close near top
)

function Render-Bolt {
    param(
        [Parameter(Mandatory)][int]$Size,
        [Parameter(Mandatory)][bool]$Filled
    )

    $bmp = New-Object System.Drawing.Bitmap($Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g   = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)

    $pts = @()
    foreach ($p in $boltPoints) {
        $pts += [System.Drawing.PointF]::new($p.X * $Size, $p.Y * $Size)
    }

    $strokeWidth = [single]([Math]::Max(1.0, $Size / 16.0))

    if ($Filled) {
        $fill   = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 215, 0))   # bright yellow
        $stroke = New-Object System.Drawing.Pen([System.Drawing.Color]::Black, $strokeWidth)
        $g.FillPolygon($fill, $pts)
        $g.DrawPolygon($stroke, $pts)
        $fill.Dispose()
        $stroke.Dispose()
    } else {
        $stroke = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(180, 128, 128, 128), $strokeWidth)
        $g.DrawPolygon($stroke, $pts)
        $stroke.Dispose()
    }

    $g.Dispose()

    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bytes = $ms.ToArray()
    $ms.Dispose()
    $bmp.Dispose()
    return ,$bytes
}

function Write-Ico {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][bool]$Filled
    )

    $sizes = @(16, 32, 48)
    $pngs  = New-Object 'System.Collections.Generic.List[byte[]]'
    foreach ($s in $sizes) {
        $pngs.Add((Render-Bolt -Size $s -Filled $Filled))
    }

    $fs = [System.IO.File]::Create($Path)
    $bw = New-Object System.IO.BinaryWriter($fs)

    # ICONDIR (6 bytes): reserved=0, type=1 (icon), count
    $bw.Write([uint16]0)
    $bw.Write([uint16]1)
    $bw.Write([uint16]$sizes.Count)

    # ICONDIRENTRY (16 bytes each): width, height, colorCount, reserved,
    # planes, bpp, sizeInBytes, dataOffset
    $offset = 6 + 16 * $sizes.Count
    for ($i = 0; $i -lt $sizes.Count; $i++) {
        $w  = $sizes[$i]
        $wb = if ($w -ge 256) { 0 } else { $w }
        $bw.Write([byte]$wb)
        $bw.Write([byte]$wb)
        $bw.Write([byte]0)        # color count (0 for >=8bpp)
        $bw.Write([byte]0)        # reserved
        $bw.Write([uint16]1)      # planes
        $bw.Write([uint16]32)     # bits per pixel
        $bw.Write([uint32]$pngs[$i].Length)
        $bw.Write([uint32]$offset)
        $offset += $pngs[$i].Length
    }

    foreach ($png in $pngs) {
        $bw.Write($png)
    }

    $bw.Dispose()
    $fs.Dispose()
}

function Write-PreviewPng {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][bool]$Filled
    )
    $bytes = Render-Bolt -Size 128 -Filled $Filled
    [System.IO.File]::WriteAllBytes($Path, $bytes)
}

Write-Ico        -Path "$PSScriptRoot\full.ico"          -Filled $true
Write-Ico        -Path "$PSScriptRoot\empty.ico"         -Filled $false
Write-PreviewPng -Path "$PSScriptRoot\full_preview.png"  -Filled $true
Write-PreviewPng -Path "$PSScriptRoot\empty_preview.png" -Filled $false

Write-Host "Generated:"
Write-Host "  full.ico, empty.ico  (multi-res 16/32/48, used by amped.rc)"
Write-Host "  full_preview.png, empty_preview.png  (128px, for visual check only)"
