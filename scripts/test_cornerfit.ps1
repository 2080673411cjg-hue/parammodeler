param(
    [string]$EigenInclude = 'E:/mambaforge/envs/qgis_dev/Library/include/eigen3',
    [switch]$WithPicking,
    [string]$ScreenshotDirectory
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vs = & (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe') -latest -property installationPath
$msvc = Get-ChildItem -LiteralPath (Join-Path $vs 'VC/Tools/MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$sdk = Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'Include') -Directory | Sort-Object Name -Descending | Select-Object -First 1
$temp = Join-Path ([IO.Path]::GetTempPath()) ('parammodeler-corner-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temp | Out-Null
$arguments = @('/nologo', '/std:c++17', '/EHsc', '/MD', '/O2', '/utf-8', '/DNOMINMAX', ('/I' + $EigenInclude), ('/I' + (Join-Path $msvc.FullName 'include')))
foreach ($part in @('ucrt', 'shared', 'um')) { $arguments += '/I' + (Join-Path $sdk.FullName $part) }
$link = @('/link', ('/LIBPATH:' + (Join-Path $msvc.FullName 'lib/x64')))
foreach ($part in @('ucrt', 'um')) { $link += '/LIBPATH:' + (Join-Path $sdkRoot ('Lib/' + $sdk.Name + '/' + $part + '/x64')) }
$targets = @('test_cornerfit')
$oldPath = $env:PATH
$oldPlugins = $env:QT_QPA_PLATFORM_PLUGIN_PATH
if ($WithPicking) {
    $targets += @('test_pick3d_math', 'test_pick3d_overlay')
    $qt = 'E:/mambaforge/envs/qgis_dev/Library'
    $env:PATH = (Join-Path $qt 'bin') + ';' + $env:PATH
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $qt 'plugins/platforms'
    $arguments += '/I' + (Join-Path $qt 'include/qt')
    foreach ($module in @('Core', 'Gui', '3DCore', '3DRender')) {
        $arguments += '/I' + (Join-Path $qt ('include/qt/Qt' + $module))
        $link += Join-Path $qt ('lib/Qt5' + $module + '_conda.lib')
    }
    $link += 'opengl32.lib'
}
try {
    foreach ($target in $targets) {
        $exe = Join-Path $temp ($target + '.exe')
        $obj = Join-Path $temp ($target + '.obj')
        $compileArgs = $arguments + @((Join-Path $root ('tests/' + $target + '.cpp')), ('/Fe:' + $exe), ('/Fo:' + $obj)) + $link
        & (Join-Path $msvc.FullName 'bin/Hostx64/x64/cl.exe') @compileArgs
        if ($LASTEXITCODE -ne 0) { throw ($target + ' compilation failed') }
        if ($target -eq 'test_pick3d_overlay' -and $ScreenshotDirectory) { & $exe $ScreenshotDirectory } else { & $exe }
        if ($LASTEXITCODE -ne 0) { throw ($target + ' failed') }
    }
} finally {
    $env:PATH = $oldPath
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $oldPlugins
    foreach ($target in $targets) {
        foreach ($extension in @('.exe', '.obj')) {
            $file = Join-Path $temp ($target + $extension)
            if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
        }
    }
    Remove-Item -LiteralPath $temp
}
