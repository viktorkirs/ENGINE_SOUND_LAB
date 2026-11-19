param(
    [string]$QtRoot = 'C:\Qt\6.11.1\mingw_64',
    [string]$MinGWRoot = 'C:\Qt\Tools\mingw1310_64',
    [string]$NinjaDirectory = 'C:\Qt\Tools\Ninja',
    [switch]$SkipPackage
)

$ErrorActionPreference = 'Stop'
$eslPreviousPath = $env:Path
try {
    $qtBin = Join-Path $QtRoot 'bin'
    $compilerBin = Join-Path $MinGWRoot 'bin'
    $compiler = Join-Path $compilerBin 'g++.exe'
    if (!(Test-Path -LiteralPath $compiler -PathType Leaf)) { throw "MinGW compiler not found at $compiler" }
    if (!(Test-Path -LiteralPath (Join-Path $qtBin 'Qt6Widgets.dll') -PathType Leaf)) { throw "Qt Widgets not found in $qtBin" }
    $env:Path = "$qtBin;$compilerBin;$NinjaDirectory;$eslPreviousPath"

    $build = Join-Path $PSScriptRoot 'build'
    cmake -S $PSScriptRoot -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$QtRoot" "-DCMAKE_CXX_COMPILER=$compiler" -DESL_BUILD_APP=ON -DESL_BUILD_CLI=ON -DBUILD_TESTING=ON
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    cmake --build $build --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    ctest --test-dir $build --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Verification failed.' }
    if ($SkipPackage) { return }

    $dist = Join-Path $PSScriptRoot 'dist'
    $portable = Join-Path $dist 'Engine Sound Lab'
    New-Item -ItemType Directory -Force -Path $portable | Out-Null
    $binary = Join-Path $portable 'EngineSoundLab.exe'
    Copy-Item -LiteralPath (Join-Path $build 'EngineSoundLab.exe') -Destination $binary -Force
    Copy-Item -LiteralPath (Join-Path $build 'EngineSoundLabCLI.exe') -Destination $portable -Force
    & (Join-Path $qtBin 'windeployqt.exe') --release --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler --no-translations --skip-plugin-types 'iconengines,imageformats,styles,networkinformation,tls,generic,platformthemes' $binary
    if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed.' }
    foreach ($name in 'README.md','README.ru.md','AGENTS.md','THIRD-PARTY.md','licenses','presets','docs') {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $portable -Recurse -Force
    }
    $prepTools = New-Item -ItemType Directory -Force -Path (Join-Path $portable 'tools')
    foreach ($name in 'prepare_recordings.py','prepare_fleet.py') {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot "tools\$name") -Destination $prepTools.FullName -Force
    }
    $examples = Join-Path $PSScriptRoot 'examples'
    if (Test-Path -LiteralPath $examples -PathType Container) {
        Copy-Item -LiteralPath $examples -Destination $portable -Recurse -Force
    }

    Compress-Archive -LiteralPath $portable -DestinationPath (Join-Path $dist 'Engine-Sound-Lab-Windows-x64.zip') -Force
    Write-Output $binary
} finally {
    $env:Path = $eslPreviousPath
}
