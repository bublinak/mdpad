$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

$assetDir = Join-Path $PSScriptRoot "..\src\MDpad\Assets"
$fixtureImageDir = Join-Path $PSScriptRoot "..\tests\fixtures\images"
New-Item -ItemType Directory -Force -Path $assetDir | Out-Null
New-Item -ItemType Directory -Force -Path $fixtureImageDir | Out-Null

function New-Color($A, $R, $G, $B) {
    [System.Drawing.Color]::FromArgb([int]$A, [int]$R, [int]$G, [int]$B)
}

function New-RoundedRectPath($X, $Y, $Width, $Height, $Radius) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $diameter = [single]([Math]::Min($Radius * 2, [Math]::Min($Width, $Height)))

    if ($diameter -le 0) {
        $path.AddRectangle((New-Object System.Drawing.RectangleF ([single]$X), ([single]$Y), ([single]$Width), ([single]$Height)))
        return $path
    }

    $path.AddArc([single]$X, [single]$Y, $diameter, $diameter, 180, 90)
    $path.AddArc([single]($X + $Width - $diameter), [single]$Y, $diameter, $diameter, 270, 90)
    $path.AddArc([single]($X + $Width - $diameter), [single]($Y + $Height - $diameter), $diameter, $diameter, 0, 90)
    $path.AddArc([single]$X, [single]($Y + $Height - $diameter), $diameter, $diameter, 90, 90)
    $path.CloseFigure()
    $path
}

function Fill-RoundedRect($Graphics, $Brush, $X, $Y, $Width, $Height, $Radius) {
    $path = New-RoundedRectPath $X $Y $Width $Height $Radius
    try {
        $Graphics.FillPath($Brush, $path)
    }
    finally {
        $path.Dispose()
    }
}

function Draw-RoundedRect($Graphics, $Pen, $X, $Y, $Width, $Height, $Radius) {
    $path = New-RoundedRectPath $X $Y $Width $Height $Radius
    try {
        $Graphics.DrawPath($Pen, $path)
    }
    finally {
        $path.Dispose()
    }
}

function New-MDpadIconBitmap($Width, $Height, $Kind) {
    $bitmap = New-Object System.Drawing.Bitmap -ArgumentList $Width, $Height, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit
    $graphics.Clear([System.Drawing.Color]::Transparent)

    $s = [single]([Math]::Min($Width, $Height))
    $wide = $Width -gt ($Height * 1.4)
    $isFile = $Kind -eq "File"

    $iconSize = $s
    $iconOffsetX = [single](($Width - $iconSize) / 2)
    if ($wide) {
        $iconSize = [single]($Height * 0.72)
        $iconOffsetX = [single]($Height * 0.18)
    }
    $iconOffsetY = [single](($Height - $iconSize) / 2)

    $paperX = $iconOffsetX + ($iconSize * 0.20)
    $paperY = $iconOffsetY + ($iconSize * 0.08)
    $paperW = $iconSize * 0.60
    $paperH = $iconSize * 0.80
    if ($isFile) {
        $paperX = $iconOffsetX + ($iconSize * 0.18)
        $paperY = $iconOffsetY + ($iconSize * 0.07)
        $paperW = $iconSize * 0.64
        $paperH = $iconSize * 0.82
    }
    $radius = [Math]::Max(1.5, $iconSize * 0.055)
    if ($isFile) {
        $radius = [Math]::Max(1.0, $iconSize * 0.038)
    }

    $shadowBrush = New-Object System.Drawing.SolidBrush (New-Color 42 0 0 0)
    $pageBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.RectangleF ([single]$paperX), ([single]$paperY), ([single]$paperW), ([single]$paperH)),
        (New-Color 255 255 255 255),
        (New-Color 255 232 238 245),
        [System.Drawing.Drawing2D.LinearGradientMode]::Vertical)
    $pageBorder = New-Object System.Drawing.Pen (New-Color 255 92 109 125), ([Math]::Max(1.0, $iconSize * 0.016))
    $bindingBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.RectangleF ([single]$paperX), ([single]$paperY), ([single]$paperW), ([single]($paperH * 0.17))),
        (New-Color 255 0 120 212),
        (New-Color 255 77 172 235),
        [System.Drawing.Drawing2D.LinearGradientMode]::Horizontal)
    $linePen = New-Object System.Drawing.Pen (New-Color 150 74 101 128), ([Math]::Max(1.0, $iconSize * 0.012))
    $foldBrush = New-Object System.Drawing.SolidBrush (New-Color 255 218 228 238)
    $foldPen = New-Object System.Drawing.Pen (New-Color 180 106 124 142), ([Math]::Max(1.0, $iconSize * 0.010))

    if ($iconSize -ge 32) {
        Fill-RoundedRect $graphics $shadowBrush ($paperX + $iconSize * 0.035) ($paperY + $iconSize * 0.045) $paperW $paperH $radius
    }

    Fill-RoundedRect $graphics $pageBrush $paperX $paperY $paperW $paperH $radius
    $bindingHeightFactor = 0.18
    if ($isFile) {
        $bindingHeightFactor = 0.13
    }
    Fill-RoundedRect $graphics $bindingBrush $paperX $paperY $paperW ($paperH * $bindingHeightFactor) $radius
    Draw-RoundedRect $graphics $pageBorder $paperX $paperY $paperW $paperH $radius

    if ((-not $isFile) -and $iconSize -ge 28) {
        $holeBrush = New-Object System.Drawing.SolidBrush (New-Color 245 236 248 255)
        $holePen = New-Object System.Drawing.Pen (New-Color 140 35 82 130), ([Math]::Max(1.0, $iconSize * 0.006))
        for ($i = 0; $i -lt 3; $i++) {
            $cx = $paperX + ($paperW * (0.25 + ($i * 0.25)))
            $cy = $paperY + ($paperH * 0.09)
            $r = [Math]::Max(1.5, $iconSize * 0.030)
            $graphics.FillEllipse($holeBrush, [single]($cx - $r), [single]($cy - $r), [single]($r * 2), [single]($r * 2))
            $graphics.DrawEllipse($holePen, [single]($cx - $r), [single]($cy - $r), [single]($r * 2), [single]($r * 2))
        }
        $holeBrush.Dispose()
        $holePen.Dispose()
    }

    $lineStartX = $paperX + ($paperW * 0.18)
    $lineEndX = $paperX + ($paperW * 0.78)
    $lineBase = 0.34
    if ($isFile) {
        $lineBase = 0.26
    }
    for ($i = 0; $i -lt 4; $i++) {
        $y = $paperY + ($paperH * ($lineBase + ($i * 0.115)))
        $graphics.DrawLine($linePen, [single]$lineStartX, [single]$y, [single]$lineEndX, [single]$y)
    }

    if ($iconSize -ge 40) {
        $fold = $iconSize * 0.13
        $points = @(
            (New-Object System.Drawing.PointF ([single]($paperX + $paperW - $fold)), ([single]($paperY + $paperH))),
            (New-Object System.Drawing.PointF ([single]($paperX + $paperW)), ([single]($paperY + $paperH - $fold))),
            (New-Object System.Drawing.PointF ([single]($paperX + $paperW)), ([single]($paperY + $paperH)))
        )
        $graphics.FillPolygon($foldBrush, $points)
        $graphics.DrawPolygon($foldPen, $points)
    }

    if ($iconSize -ge 44) {
        $badgeX = $paperX + ($paperW * 0.19)
        $badgeY = $paperY + ($paperH * 0.63)
        $badgeW = $paperW * 0.62
        $badgeH = $paperH * 0.20
        if ($isFile) {
            $badgeY = $paperY + ($paperH * 0.66)
        }
        $badgeColor = New-Color 255 25 53 74
        if ($isFile) {
            $badgeColor = New-Color 255 0 99 177
        }
        $badgeBrush = New-Object System.Drawing.SolidBrush $badgeColor
        $badgeTextBrush = New-Object System.Drawing.SolidBrush (New-Color 255 238 248 255)
        Fill-RoundedRect $graphics $badgeBrush $badgeX $badgeY $badgeW $badgeH ([Math]::Max(2, $iconSize * 0.025))

        $fontSize = [Math]::Max(8, [int]($iconSize * 0.115))
        $font = New-Object System.Drawing.Font "Segoe UI Semibold", $fontSize, ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
        $format = New-Object System.Drawing.StringFormat
        $format.Alignment = [System.Drawing.StringAlignment]::Center
        $format.LineAlignment = [System.Drawing.StringAlignment]::Center
        $graphics.DrawString("MD", $font, $badgeTextBrush, (New-Object System.Drawing.RectangleF ([single]$badgeX), ([single]$badgeY), ([single]$badgeW), ([single]$badgeH)), $format)
        $format.Dispose()
        $font.Dispose()
        $badgeBrush.Dispose()
        $badgeTextBrush.Dispose()
    }

    if ($wide) {
        $textBrush = New-Object System.Drawing.SolidBrush (New-Color 255 32 44 56)
        $fontSize = [Math]::Max(26, [int]($Height * 0.30))
        $font = New-Object System.Drawing.Font "Segoe UI Semibold", $fontSize, ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
        $format = New-Object System.Drawing.StringFormat
        $format.Alignment = [System.Drawing.StringAlignment]::Near
        $format.LineAlignment = [System.Drawing.StringAlignment]::Center
        $textRect = New-Object System.Drawing.RectangleF ([single]($iconOffsetX + $iconSize + $Height * 0.10)), 0, ([single]($Width - ($iconOffsetX + $iconSize + $Height * 0.18))), ([single]$Height)
        $graphics.DrawString("MDpad", $font, $textBrush, $textRect, $format)
        $format.Dispose()
        $font.Dispose()
        $textBrush.Dispose()
    }

    $shadowBrush.Dispose()
    $pageBrush.Dispose()
    $pageBorder.Dispose()
    $bindingBrush.Dispose()
    $linePen.Dispose()
    $foldBrush.Dispose()
    $foldPen.Dispose()
    $graphics.Dispose()
    $bitmap
}

function Save-Png($Path, $Bitmap) {
    $Bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
}

function New-SquareAsset($Name, $Size, $Kind) {
    $bitmap = New-MDpadIconBitmap $Size $Size $Kind
    try {
        Save-Png (Join-Path $assetDir $Name) $bitmap
    }
    finally {
        $bitmap.Dispose()
    }
}

function New-WideAsset($Name, $Width, $Height) {
    $bitmap = New-MDpadIconBitmap $Width $Height "App"
    try {
        Save-Png (Join-Path $assetDir $Name) $bitmap
    }
    finally {
        $bitmap.Dispose()
    }
}

function New-IcoFile($Path, $Sizes, $Kind) {
    $images = @()
    foreach ($size in $Sizes) {
        $bitmap = New-MDpadIconBitmap $size $size $Kind
        $stream = New-Object System.IO.MemoryStream
        try {
            $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
            $images += [PSCustomObject]@{
                Size = $size
                Bytes = $stream.ToArray()
            }
        }
        finally {
            $stream.Dispose()
            $bitmap.Dispose()
        }
    }

    $file = [System.IO.File]::Open($Path, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
    $writer = New-Object System.IO.BinaryWriter $file
    try {
        $writer.Write([UInt16]0)
        $writer.Write([UInt16]1)
        $writer.Write([UInt16]$images.Count)

        $offset = 6 + (16 * $images.Count)
        foreach ($image in $images) {
            if ($image.Size -eq 256) {
                $dimension = [byte]0
            } else {
                $dimension = [byte]$image.Size
            }

            $writer.Write($dimension)
            $writer.Write($dimension)
            $writer.Write([byte]0)
            $writer.Write([byte]0)
            $writer.Write([UInt16]1)
            $writer.Write([UInt16]32)
            $writer.Write([UInt32]$image.Bytes.Length)
            $writer.Write([UInt32]$offset)
            $offset += $image.Bytes.Length
        }

        foreach ($image in $images) {
            $writer.Write($image.Bytes)
        }
    }
    finally {
        $writer.Dispose()
        $file.Dispose()
    }
}

function New-FixtureImage($Path) {
    $width = 240
    $height = 120
    $bitmap = New-Object System.Drawing.Bitmap -ArgumentList $width, $height, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.Clear((New-Color 255 246 248 250))

    $accent = New-Object System.Drawing.SolidBrush (New-Color 255 0 120 212)
    $line = New-Object System.Drawing.Pen (New-Color 255 120 136 150), 2
    $graphics.FillRectangle($accent, 0, 0, $width, 18)
    for ($i = 0; $i -lt 4; $i++) {
        $y = 42 + ($i * 16)
        $graphics.DrawLine($line, 28, $y, 210, $y)
    }

    $accent.Dispose()
    $line.Dispose()
    $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose()
    $bitmap.Dispose()
}

$scales = @(
    @{ Name = "scale-100"; Factor = 1.00 },
    @{ Name = "scale-125"; Factor = 1.25 },
    @{ Name = "scale-150"; Factor = 1.50 },
    @{ Name = "scale-200"; Factor = 2.00 },
    @{ Name = "scale-400"; Factor = 4.00 }
)

New-SquareAsset "Square44x44Logo.png" 44 "App"
New-SquareAsset "Square150x150Logo.png" 150 "App"
New-SquareAsset "StoreLogo.png" 50 "App"
New-WideAsset "Wide310x150Logo.png" 310 150
New-SquareAsset "FileTypeMarkdown.png" 44 "File"

foreach ($scale in $scales) {
    New-SquareAsset "Square44x44Logo.$($scale.Name).png" ([int][Math]::Round(44 * $scale.Factor)) "App"
    New-SquareAsset "Square150x150Logo.$($scale.Name).png" ([int][Math]::Round(150 * $scale.Factor)) "App"
    New-SquareAsset "StoreLogo.$($scale.Name).png" ([int][Math]::Round(50 * $scale.Factor)) "App"
    New-WideAsset "Wide310x150Logo.$($scale.Name).png" ([int][Math]::Round(310 * $scale.Factor)) ([int][Math]::Round(150 * $scale.Factor))
    New-SquareAsset "FileTypeMarkdown.$($scale.Name).png" ([int][Math]::Round(44 * $scale.Factor)) "File"
}

foreach ($size in @(16, 20, 24, 30, 32, 36, 40, 44, 48, 60, 64, 72, 80, 96, 256)) {
    New-SquareAsset "Square44x44Logo.targetsize-$size.png" $size "App"
    New-SquareAsset "Square44x44Logo.targetsize-$($size)_altform-unplated.png" $size "App"
    New-SquareAsset "FileTypeMarkdown.targetsize-$size.png" $size "File"
}

New-IcoFile (Join-Path $assetDir "MDpad.ico") @(16, 20, 24, 32, 40, 48, 64, 128, 256) "App"
New-FixtureImage (Join-Path $fixtureImageDir "sample.png")
