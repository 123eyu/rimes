# Reproducible Windows resource icons. Text is rendered with the same installed
# CJK UI font as the native candidate window; icons contain all supported DPIs.
param([string]$OutputDirectory = (Join-Path $PSScriptRoot '../native/resources'))
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$sizes = @(16, 20, 24, 32, 40, 48, 64)
foreach ($mode in @(@('zh', [string][char]0x4E2D), @('en', [string][char]0x82F1), @('disabled', [string][char]0x2014))) {
    $frames = @()
    foreach ($size in $sizes) {
        $bitmap = New-Object Drawing.Bitmap($size, $size)
        $g = [Drawing.Graphics]::FromImage($bitmap)
        $g.Clear([Drawing.Color]::FromArgb(255, 32, 32, 32))
        $g.DrawRectangle([Drawing.Pens]::White, 0, 0, ($size - 1), ($size - 1))
        $g.TextRenderingHint = [Drawing.Text.TextRenderingHint]::AntiAliasGridFit
        $font = New-Object Drawing.Font('Microsoft YaHei UI', ([single]($size * 0.8)), [Drawing.FontStyle]::Regular, [Drawing.GraphicsUnit]::Pixel)
        $format = New-Object Drawing.StringFormat
        $format.Alignment = [Drawing.StringAlignment]::Center
        $format.LineAlignment = [Drawing.StringAlignment]::Center
        $g.DrawString($mode[1], $font, [Drawing.Brushes]::White, [Drawing.RectangleF]::new(0, 0, $size, $size), $format)
        $stream = New-Object IO.MemoryStream
        $bitmap.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
        $frames += ,$stream.ToArray()
        $stream.Dispose(); $format.Dispose(); $font.Dispose(); $g.Dispose(); $bitmap.Dispose()
    }
    $file = [IO.File]::Create((Join-Path $OutputDirectory ('input-mode-' + $mode[0] + '.ico')))
    $writer = New-Object IO.BinaryWriter($file)
    try {
        $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$sizes.Count)
        $offset = 6 + 16 * $sizes.Count
        for ($i = 0; $i -lt $sizes.Count; $i++) {
            $writer.Write([byte]$sizes[$i]); $writer.Write([byte]$sizes[$i])
            $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]32)
            $writer.Write([uint32]$frames[$i].Length); $writer.Write([uint32]$offset)
            $offset += $frames[$i].Length
        }
        foreach ($frame in $frames) { $writer.Write([byte[]]$frame) }
    } finally { $writer.Dispose(); $file.Dispose() }
}
