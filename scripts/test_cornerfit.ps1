param(
    [string]$EigenInclude = 'E:/mambaforge/envs/qgis_dev/Library/include/eigen3',
    [switch]$WithPicking,
    [switch]$WithWireframe,
    [switch]$WithAlignment,
    [switch]$WithHeightControl,
    [switch]$WithSurfaces,
    [switch]$WithFit,
    [string[]]$SurfaceClouds = @(),
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
}
if ($WithWireframe) {
    $targets += 'test_wireframe_guides'
}
if ($WithAlignment) {
    $targets += @('test_alignment_geometry', 'test_regression_axes')
}
if ($WithHeightControl) {
    $targets += 'test_heightcontrol'
}
if ($WithSurfaces) {
    $targets += 'test_surface_classification'
}
if ($WithFit) {
    $targets += @('test_fit_metrics', 'test_fit_solids', 'test_fit_overlay')
}
if ($WithPicking -or $WithWireframe -or $WithAlignment -or $WithHeightControl -or $WithSurfaces -or $WithFit) {
    $qt = 'E:/mambaforge/envs/qgis_dev/Library'
    $env:PATH = (Join-Path $qt 'bin') + ';' + $env:PATH
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $qt 'plugins/platforms'
    $arguments += '/I' + (Join-Path $qt 'include/qt')
    foreach ($module in @('Core', 'Gui', '3DCore', '3DRender')) {
        $arguments += '/I' + (Join-Path $qt ('include/qt/Qt' + $module))
        $link += Join-Path $qt ('lib/Qt5' + $module + '_conda.lib')
    }
    $link += 'opengl32.lib'
    if ($WithHeightControl) {
        $arguments += '/I' + (Join-Path $qt 'include/qt/QtWidgets')
        $link += Join-Path $qt 'lib/Qt5Widgets_conda.lib'
    }
}
try {
    if ($WithFit) {
        # Compile the actual pure mesh builders without linking the QGIS plugin.
        $meshText = [IO.File]::ReadAllText((Join-Path $root 'buildmesh.cpp'))
        $methods = @('Cuboid', 'Cylinder', 'LHouse', 'ConeCylinder', 'GabledRoof', 'PyramidRoof',
                     'TruncatedPyramidRoof', 'HalfCylinderRoof', 'CylinderHemisphere', 'IndentedCuboid',
                     'AsymmetricGableHouse', 'FourStageRoundTower', 'TwoGableHouses')
        $meshHeader = (Join-Path $root 'buildmesh.h').Replace('\', '/')
        $meshParts = @("#include `"$meshHeader`"", '#include <QtMath>', '#include <cmath>', '#define DEBUG_LOG(msg)')
        foreach ($method in $methods) {
            $signature = if ($method -eq 'LHouse') { 'MeshData BuildMesh::buildLHouse( ParamModelerDock *dock )' }
                         else { "MeshData BuildMesh::build$method( const " + $method + 'Params &p )' }
            $start = $meshText.IndexOf($signature, [StringComparison]::Ordinal)
            if ($start -lt 0) { throw "Mesh builder not found: $method" }
            $position = $meshText.IndexOf('{', $start)
            $depth = 1
            while ($depth -gt 0) {
                $position++
                if ($meshText[$position] -eq '{') { $depth++ }
                elseif ($meshText[$position] -eq '}') { $depth-- }
            }
            $body = $meshText.Substring($start, $position - $start + 1)
            if ($method -eq 'LHouse') {
                # This builder exposes only Dock getters; substitute its plain-data equivalents.
                $body = $body.Replace($signature, 'MeshData BuildMesh::buildLHouse( const LHouseParams &p )')
                $body = $body.Replace('dock->LTotalLength()', 'p.totalLength').Replace('dock->LWingRatio()', 'p.wingRatio')
                $body = $body.Replace('dock->LTotalWidth()', 'p.totalWidth').Replace('dock->LWingWidthRatio()', 'p.wingWidthRatio')
                $body = $body.Replace('dock->LHeight()', 'p.height')
            }
            $meshParts += $body
        }
        $meshUnit = Join-Path $temp 'fit_meshes.cpp'
        [IO.File]::WriteAllText($meshUnit, ($meshParts -join "`n"), [Text.UTF8Encoding]::new($false))
    }
    foreach ($target in $targets) {
        $exe = Join-Path $temp ($target + '.exe')
        $obj = Join-Path $temp ($target + '.obj')
        $compileArgs = $arguments + @((Join-Path $root ('tests/' + $target + '.cpp')), ('/Fe:' + $exe), ('/Fo:' + $obj)) + $link
        if ($target -eq 'test_fit_solids') {
            $compileArgs = $arguments + @((Join-Path $root 'tests/test_fit_solids.cpp'), $meshUnit,
                                          ('/Fe:' + $exe), ('/Fo:' + $temp + '\')) + $link
        }
        & (Join-Path $msvc.FullName 'bin/Hostx64/x64/cl.exe') @compileArgs
        if ($LASTEXITCODE -ne 0) { throw ($target + ' compilation failed') }
        if ($target -eq 'test_surface_classification') { & $exe @SurfaceClouds }
        elseif ($target -eq 'test_pick3d_overlay' -and $ScreenshotDirectory) { & $exe $ScreenshotDirectory }
        else { & $exe }
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
    if ($WithFit) {
        foreach ($name in @('fit_meshes.cpp', 'fit_meshes.obj')) {
            $file = Join-Path $temp $name
            if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
        }
    }
    Remove-Item -LiteralPath $temp
}
