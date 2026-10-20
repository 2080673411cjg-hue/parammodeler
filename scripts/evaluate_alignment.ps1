param(
    [string]$Python = 'E:/mambaforge/envs/ptv3_cpu/python.exe',
    [string]$Output = (Join-Path (Split-Path $PSScriptRoot -Parent) 'evaluations/2026-10-20-alignment'),
    [switch]$ValidateOnly
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
New-Item -ItemType Directory -Path $Output -Force | Out-Null
$Output = (Resolve-Path -LiteralPath $Output).Path
& $Python (Join-Path $PSScriptRoot 'evaluate_alignment.py') prepare --output $Output
if ($LASTEXITCODE -ne 0) { throw 'Evaluation manifest failed' }
$vs = & (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe') -latest -property installationPath
$msvc = Get-ChildItem -LiteralPath (Join-Path $vs 'VC/Tools/MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$sdk = Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'Include') -Directory | Sort-Object Name -Descending | Select-Object -First 1
$qt = 'E:/mambaforge/envs/qgis_dev/Library'
$build = [IO.Path]::GetFullPath((Join-Path $root '../../../build'))
[xml]$project = Get-Content -LiteralPath (Join-Path $build 'src/plugins/parammodeler/plugin_parammodeler.vcxproj')
$compile = $project.Project.ItemDefinitionGroup[0].ClCompile
$work = Join-Path $Output 'compiler'
New-Item -ItemType Directory -Path $work -Force | Out-Null
& (Join-Path $qt 'bin/uic.exe') (Join-Path $root 'parammodeler_dock.ui') -o (Join-Path $work 'ui_parammodeler_dock.h')
if ($LASTEXITCODE -ne 0) { throw 'UI header generation failed' }
$args = @('/nologo', '/std:c++17', '/EHsc', '/MD', '/O2', '/Gy', '/utf-8', '/bigobj', '/DNOMINMAX', '/D_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING')
$args += '/I' + $work
$args += '/I' + (Join-Path $msvc.FullName 'include')
$args += '/I' + (Join-Path $qt 'include/eigen3')
foreach ($part in @('ucrt', 'shared', 'um', 'winrt')) { $args += '/I' + (Join-Path $sdk.FullName $part) }
foreach ($path in $compile.AdditionalIncludeDirectories.Split(';')) {
    if ($path -and !$path.StartsWith('%(')) { $args += '/I' + $path }
}
foreach ($match in [regex]::Matches($compile.AdditionalOptions, '/external:I "([^"]+)"')) { $args += '/I' + $match.Groups[1].Value }
foreach ($define in $compile.PreprocessorDefinitions.Split(';')) {
    if ($define -and !$define.StartsWith('%(')) { $args += '/D' + $define }
}
$sources = @('tests/evaluate_alignment.cpp', 'buildmesh.cpp', 'parammodeler_dlutils.cpp', 'parammodeler_pcdloader.cpp', 'parammodeler_config.cpp', 'parammodeler_params.cpp')
foreach ($source in $sources) { $args += Join-Path $root $source }
$exe = Join-Path $work 'evaluate_alignment.exe'
$args += @(('/Fe:' + $exe), ('/Fo:' + $work + '\'), '/link', '/OPT:REF', '/INCREMENTAL:NO', '/SUBSYSTEM:CONSOLE')
$args += '/LIBPATH:' + (Join-Path $msvc.FullName 'lib/x64')
foreach ($part in @('ucrt', 'um')) { $args += '/LIBPATH:' + (Join-Path $sdkRoot ('Lib/' + $sdk.Name + '/' + $part + '/x64')) }
foreach ($module in @('Core', 'Gui', 'Widgets', 'Network', 'Xml')) { $args += Join-Path $qt ('lib/Qt5' + $module + '_conda.lib') }
$args += Join-Path $build 'output/lib/qgis_core.lib'
$args += 'user32.lib'
& (Join-Path $msvc.FullName 'bin/Hostx64/x64/cl.exe') @args
if ($LASTEXITCODE -ne 0) { throw 'Standalone evaluator compilation failed' }
$oldPath = $env:PATH
try {
    $env:PATH = (Join-Path $build 'output/bin/RelWithDebInfo') + ';' + (Join-Path $qt 'bin') + ';' + $env:PATH
    if (!$ValidateOnly) {
        & $exe (Join-Path $Output 'manifest.json') (Join-Path $Output 'results.json')
        if ($LASTEXITCODE -ne 0) { throw 'Standalone evaluation failed' }
    }
    & $exe (Join-Path $Output 'manifest.json') (Join-Path $Output 'raw_validation.json') --validate-raw
    if ($LASTEXITCODE -ne 0) { throw 'Raw geometry validation failed' }
} finally { $env:PATH = $oldPath }
& $Python (Join-Path $PSScriptRoot 'evaluate_alignment.py') summarize --output $Output
if ($LASTEXITCODE -ne 0) { throw 'Evaluation summary failed' }
foreach ($source in $sources) {
    $object = Join-Path $work ([IO.Path]::GetFileNameWithoutExtension($source) + '.obj')
    if (Test-Path -LiteralPath $object) { Remove-Item -LiteralPath $object }
}
Remove-Item -LiteralPath $exe, (Join-Path $work 'ui_parammodeler_dock.h')
if (!(Get-ChildItem -LiteralPath $work -Force)) { Remove-Item -LiteralPath $work }
