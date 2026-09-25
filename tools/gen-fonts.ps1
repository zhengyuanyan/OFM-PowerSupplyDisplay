# ============================================================================
#  Generate the Chinese subset fonts used by OFM-PowerSupplyDisplay.
#
#  Why a subset:  a full CJK font is several MB, while the display only ever
#  shows a few dozen different characters.  This script collects every
#  non-ASCII character that appears inside a string literal of the module
#  sources, adds ASCII 0x20-0x7E and the degree sign, and asks lv_font_conv
#  to build a small LVGL font containing exactly those glyphs.
#
#  Run it again whenever user visible text changes, otherwise new characters
#  are drawn as placeholder boxes.
#
#  Usage:  powershell -ExecutionPolicy Bypass -File tools\gen-fonts.ps1
#
#  Requires: node / npm (lv_font_conv is fetched on demand via npx)
#  NOTE: keep this file pure ASCII - Windows PowerShell 5.1 reads BOM-less
#        .ps1 files using the ANSI code page.
# ============================================================================

param(
    [string]$FontFile = "$env:WINDIR\Fonts\simhei.ttf",
    [string]$LvFontConvVersion = "1.5.3"
)

$ErrorActionPreference = "Stop"

$moduleRoot = Split-Path -Parent $PSScriptRoot
$srcDir = Join-Path $moduleRoot "src"
$outDir = Join-Path $srcDir "fonts"

if (-not (Test-Path $FontFile)) {
    Write-Host "Font file not found: $FontFile" -ForegroundColor Red
    Write-Host "Pass -FontFile <path to a Chinese .ttf>" -ForegroundColor Red
    exit 1
}

# ---- 1. collect the characters used in string literals ---------------------
$chars = New-Object 'System.Collections.Generic.HashSet[char]'
$files = @(Get-ChildItem -Path $srcDir -Filter *.cpp) + @(Get-ChildItem -Path $srcDir -Filter *.h)

foreach ($file in $files) {
    # -Encoding UTF8 is mandatory: the sources contain Chinese text
    foreach ($line in (Get-Content -LiteralPath $file.FullName -Encoding UTF8)) {
        $code = $line

        # drop the line comment part (our strings never contain "//")
        $comment = $code.IndexOf("//")
        if ($comment -ge 0) { $code = $code.Substring(0, $comment) }

        # keep only double quoted string literals
        foreach ($match in [regex]::Matches($code, '"((?:[^"\\]|\\.)*)"')) {
            foreach ($ch in $match.Groups[1].Value.ToCharArray()) {
                if ([int]$ch -gt 127) { [void]$chars.Add($ch) }
            }
        }
    }
}

# the degree sign is used in the temperature unit
[void]$chars.Add([char]0x00B0)

$symbols = -join ($chars | Sort-Object)
Write-Host ("Characters collected: {0}" -f $chars.Count) -ForegroundColor Cyan
Write-Host $symbols -ForegroundColor DarkGray

# ---- 2. generate ------------------------------------------------------------------
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }

$sizes = @(
    @{ Name = "lv_font_ps_16"; Size = 16 },
    @{ Name = "lv_font_ps_20"; Size = 20 }
)

foreach ($entry in $sizes) {
    $outFile = Join-Path $outDir ("{0}.c" -f $entry.Name)
    Write-Host ("Generating {0} ({1} px) ..." -f $entry.Name, $entry.Size) -ForegroundColor Yellow

    & npx --yes ("lv_font_conv@{0}" -f $LvFontConvVersion) `
        --font $FontFile `
        --size $entry.Size `
        --bpp 4 `
        --format lvgl `
        --range 0x20-0x7E `
        --symbols $symbols `
        --lv-font-name $entry.Name `
        --lv-include "lvgl.h" `
        --force-fast-kern-format `
        -o $outFile

    if ($LASTEXITCODE -ne 0) {
        Write-Host ("lv_font_conv failed for {0}" -f $entry.Name) -ForegroundColor Red
        exit $LASTEXITCODE
    }

    # sanity check: the sparse cmap must list exactly our collected characters
    $content = Get-Content -LiteralPath $outFile -Raw -Encoding UTF8
    $lengths = [regex]::Matches($content, '\.list_length\s*=\s*(\d+)') | ForEach-Object { [int]$_.Groups[1].Value }
    $sparse = ($lengths | Measure-Object -Maximum).Maximum
    Write-Host ("  sparse cmap entries: {0} (expected {1})" -f $sparse, $chars.Count) -ForegroundColor Cyan

    if ($sparse -ne $chars.Count) {
        Write-Host "  !! glyph count mismatch - did the Chinese characters survive the command line?" -ForegroundColor Red
        exit 1
    }
}

Write-Host "Done." -ForegroundColor Green
