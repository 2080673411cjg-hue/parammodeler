param(
    [string]$QtBin = 'E:/mambaforge/envs/qgis_dev/Library/bin',
    [switch]$Extract
)
$ErrorActionPreference = 'Stop'
$pluginRoot = Split-Path $PSScriptRoot -Parent
$ts = Join-Path $pluginRoot 'i18n/parammodeler_zh_CN.ts'
if ($Extract) {
    $sources = Get-ChildItem -LiteralPath $pluginRoot -File | Where-Object { $_.Extension -in '.cpp', '.h' } | Select-Object -ExpandProperty FullName
    & (Join-Path $QtBin 'lupdate.exe') @sources (Join-Path $pluginRoot 'parammodeler_dock.ui') -no-obsolete -ts $ts
    if ($LASTEXITCODE -ne 0) { throw 'Translation extraction failed' }
}
& (Join-Path $QtBin 'lrelease.exe') -nounfinished $ts -qm (Join-Path $pluginRoot 'i18n/parammodeler_zh_CN.qm')
if ($LASTEXITCODE -ne 0) { throw 'Translation compilation failed' }
