# SPDX-License-Identifier: GPL-2.0-or-later
# Deterministic format/size conversion of the generated, transparent master.
# No drawing redesign, host desktop changes or installed icon replacement.
param([string]$InputPng = 'assets/kikiemu-icon.png', [string]$OutputIco = 'assets/kikiemu.ico')
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not [IO.Path]::IsPathRooted($InputPng)) { $InputPng = Join-Path $repoRoot $InputPng }
if (-not [IO.Path]::IsPathRooted($OutputIco)) { $OutputIco = Join-Path $repoRoot $OutputIco }
if (Test-Path -LiteralPath $OutputIco) { throw 'Output icon already exists; choose a new filename.' }
Add-Type -AssemblyName System.Drawing
$image = [Drawing.Image]::FromFile($InputPng)
try {
    if ($image.Width -ne $image.Height) { throw 'Application icon master must be square.' }
    $frames = @()
    foreach ($size in @(16, 24, 32, 48, 64, 128, 256)) {
        $bitmap = [Drawing.Bitmap]::new($size, $size, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $graphics = [Drawing.Graphics]::FromImage($bitmap)
            try {
                $graphics.CompositingMode = [Drawing.Drawing2D.CompositingMode]::SourceCopy
                $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                $graphics.DrawImage($image, [Drawing.Rectangle]::new(0, 0, $size, $size))
            } finally { $graphics.Dispose() }
            $memory = [IO.MemoryStream]::new()
            try {
                $bitmap.Save($memory, [Drawing.Imaging.ImageFormat]::Png)
                $frames += [pscustomobject]@{ Size = $size; Bytes = $memory.ToArray() }
            } finally { $memory.Dispose() }
        } finally { $bitmap.Dispose() }
    }
    $stream = [IO.File]::Open($OutputIco, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    $writer = [IO.BinaryWriter]::new($stream)
    try {
        $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$frames.Count)
        $offset = 6 + 16 * $frames.Count
        foreach ($frame in $frames) {
            $dimension = if ($frame.Size -eq 256) { 0 } else { $frame.Size }
            $writer.Write([byte]$dimension); $writer.Write([byte]$dimension)
            $writer.Write([byte]0); $writer.Write([byte]0)
            $writer.Write([uint16]1); $writer.Write([uint16]32)
            $writer.Write([uint32]$frame.Bytes.Length); $writer.Write([uint32]$offset)
            $offset += $frame.Bytes.Length
        }
        foreach ($frame in $frames) { $writer.Write([byte[]]$frame.Bytes) }
    } finally { $writer.Dispose(); $stream.Dispose() }
    Write-Output "Created seven-size transparent Windows icon: $OutputIco"
} finally { $image.Dispose() }
