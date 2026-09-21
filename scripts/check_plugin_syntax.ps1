param(
    [string[]]$Sources = @('parammodeler.cpp', 'parammodeler_dock.cpp', 'parammodeler_pointnet.cpp', 'parammodeler_pick3d.cpp')
)
$ErrorActionPreference = 'Stop'
$pluginRoot = Split-Path $PSScriptRoot -Parent
$projectPath = Join-Path $pluginRoot '../../../build/src/plugins/parammodeler/plugin_parammodeler.vcxproj'
[xml]$project = Get-Content -LiteralPath $projectPath
$compile = $project.Project.ItemDefinitionGroup[0].ClCompile
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -property installationPath
$toolset = Get-ChildItem -LiteralPath (Join-Path $vs 'VC/Tools/MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/Include'
$sdk = Get-ChildItem -LiteralPath $sdkRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1
$arguments = @('/nologo', '/Zs', '/std:c++17', '/EHsc', '/MD', '/utf-8', '/bigobj')
$arguments += '/IE:/mambaforge/envs/qgis_dev/Library/include/eigen3'
$qtBin = 'E:/mambaforge/envs/qgis_dev/Library/bin'
$uiTemp = Join-Path ([System.IO.Path]::GetTempPath()) ('parammodeler-syntax-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $uiTemp | Out-Null
$uiHeader = Join-Path $uiTemp 'ui_parammodeler_dock.h'
& (Join-Path $qtBin 'uic.exe') (Join-Path $pluginRoot 'parammodeler_dock.ui') -o $uiHeader
if ($LASTEXITCODE -ne 0) { throw 'Qt UI generation failed' }
$arguments += '/I' + $uiTemp
foreach ($path in $compile.AdditionalIncludeDirectories.Split(';')) {
    if ($path -and !$path.StartsWith('%(')) { $arguments += '/I' + $path }
}
foreach ($match in [regex]::Matches($compile.AdditionalOptions, '/external:I "([^"]+)"')) {
    $arguments += '/I' + $match.Groups[1].Value
}
foreach ($define in $compile.PreprocessorDefinitions.Split(';')) {
    if ($define -and !$define.StartsWith('%(')) { $arguments += '/D' + $define }
}
$arguments += '/I' + (Join-Path $toolset.FullName 'include')
foreach ($part in @('ucrt', 'shared', 'um', 'winrt')) {
    $arguments += '/I' + (Join-Path $sdk.FullName $part)
}
try {
    foreach ($source in $Sources) {
        & (Join-Path $toolset.FullName 'bin/Hostx64/x64/cl.exe') @arguments (Join-Path $pluginRoot $source)
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
} finally {
    Remove-Item -LiteralPath $uiHeader
    Remove-Item -LiteralPath $uiTemp
}
