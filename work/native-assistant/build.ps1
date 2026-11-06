param([switch]$TestsOnly,[ValidateSet('ets2','ats')][string]$Game='ets2',[string]$BuildToolsRoot)
$ErrorActionPreference = 'Stop'
$nativeRoot = $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot=$BuildToolsRoot
if(-not $vsRoot -and (Test-Path -LiteralPath $vswhere)) {$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath}
if(-not $vsRoot){$vsRoot=$env:AA_BUILD_TOOLS_ROOT}
if(-not $vsRoot){$portableTools=Join-Path $nativeRoot '../../../tools/BuildTools';if(Test-Path -LiteralPath (Join-Path $portableTools 'VC/Tools/MSVC')){$vsRoot=(Resolve-Path -LiteralPath $portableTools).Path}}
if (-not $vsRoot) { throw 'Visual Studio C++ x64 tools are required.' }
$compilerRoot = Get-ChildItem -LiteralPath (Join-Path $vsRoot 'VC\Tools\MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$sdkVersion = Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'Include') -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'um\Windows.h') } | Sort-Object Name -Descending | Select-Object -First 1
$compiler = Join-Path $compilerRoot.FullName 'bin\Hostx64\x64\cl.exe'
$compileGame=@();$binName='bin';$buildName='build'
if($Game -eq 'ats'){$compileGame=@('/DAA_GAME_ATS');$binName='bin-ats';$buildName='build-ats'}
$binRoot = Join-Path $nativeRoot $binName
$objRoot = Join-Path $nativeRoot $buildName
New-Item -ItemType Directory -Force -Path $binRoot,$objRoot | Out-Null
$includes = @((Join-Path $compilerRoot.FullName 'include'), (Join-Path $nativeRoot 'third_party\scs_sdk\include'), (Join-Path $nativeRoot 'third_party\minhook\include'), (Join-Path $nativeRoot 'third_party\imgui'), (Join-Path $nativeRoot 'third_party\json'), (Join-Path $nativeRoot 'src'))
foreach ($part in @('ucrt','shared','um','winrt')) { $includes += Join-Path $sdkVersion.FullName $part }
$libs = @((Join-Path $compilerRoot.FullName 'lib\x64'))
foreach ($part in @('ucrt','um')) { $libs += Join-Path $sdkRoot ('Lib\' + $sdkVersion.Name + '\' + $part + '\x64') }
$savedInclude = $env:INCLUDE
$savedLib = $env:LIB
try {
    $env:INCLUDE = $includes -join ';'
    $env:LIB = $libs -join ';'
    if(-not $TestsOnly){
    python (Join-Path $nativeRoot 'generate_localization.py')
    if($LASTEXITCODE -ne 0){throw 'Localization generation failed'}
    $sources=@('third_party/minhook/src/buffer.c','third_party/minhook/src/hook.c','third_party/minhook/src/trampoline.c','third_party/minhook/src/hde/hde64.c',
        'third_party/imgui/imgui.cpp','third_party/imgui/imgui_draw.cpp','third_party/imgui/imgui_tables.cpp','third_party/imgui/imgui_widgets.cpp',
        'third_party/imgui/backends/imgui_impl_dx11.cpp','third_party/imgui/backends/imgui_impl_win32.cpp','src/InputGate.cpp','src/MapProbe.cpp','src/JobProbe.cpp','src/MapTint.cpp','src/Overlay.cpp','src/Plugin.cpp')
    $objects=@()
    foreach ($relative in $sources) {
        $object=Join-Path $objRoot (([IO.Path]::GetFileNameWithoutExtension($relative))+'.obj')
        & $compiler @compileGame '/nologo' '/std:c++17' '/EHsc' '/O2' '/MT' '/W3' '/utf-8' '/c' (Join-Path $nativeRoot $relative) ('/Fo'+$object)
        if ($LASTEXITCODE -ne 0) { throw ('Compile failed: '+$relative) }
        $objects+=$object
    }
    $assembler=Join-Path $compilerRoot.FullName 'bin\Hostx64\x64\ml64.exe'
    & $assembler '/nologo' '/c' ('/Fo'+(Join-Path $objRoot 'MapTintBridge.obj')) (Join-Path $nativeRoot 'src/MapTintBridge.asm')
    if ($LASTEXITCODE -ne 0) { throw 'Map tint bridge assembly failed.' }
    $objects+=Join-Path $objRoot 'MapTintBridge.obj'
    $linker=Join-Path $compilerRoot.FullName 'bin\Hostx64\x64\link.exe'
    & $linker '/NOLOGO' '/DLL' @objects ('/DEF:'+(Join-Path $nativeRoot 'src/Plugin.def')) ('/OUT:'+(Join-Path $binRoot 'AchievementAssistant.dll')) '/INCREMENTAL:NO' '/DYNAMICBASE' '/NXCOMPAT' 'D3D11.lib' 'D3DCompiler.lib' 'DXGI.lib' 'DXGUID.lib' 'User32.lib' 'Gdi32.lib' 'Dwmapi.lib' 'Shell32.lib' 'Ole32.lib' 'Advapi32.lib' 'Imm32.lib'
    if ($LASTEXITCODE -ne 0) { throw 'DLL link failed.' }
    }
    if (Test-Path -LiteralPath (Join-Path $nativeRoot 'tests/NativeHost.cpp')) {
        & $compiler @compileGame '/nologo' '/std:c++17' '/EHsc' '/O2' '/MT' '/utf-8' (Join-Path $nativeRoot 'tests/NativeHost.cpp') ('/Fo'+(Join-Path $objRoot 'NativeHost.obj')) ('/Fe'+(Join-Path $binRoot 'NativeHost.exe')) '/link' 'D3D11.lib' 'D3DCompiler.lib' 'User32.lib' 'Gdi32.lib'
        if ($LASTEXITCODE -ne 0) { throw 'Native host build failed.' }
    }
    if (Test-Path -LiteralPath (Join-Path $nativeRoot 'tests/InputGateTests.cpp')) {
        $inputObjects=@('InputGate','buffer','hook','trampoline','hde64') | ForEach-Object { Join-Path $objRoot ($_+'.obj') }
        & $compiler @compileGame '/nologo' '/std:c++17' '/EHsc' '/O2' '/MT' '/utf-8' (Join-Path $nativeRoot 'tests/InputGateTests.cpp') ('/Fo'+(Join-Path $objRoot 'InputGateTests.obj')) ('/Fe'+(Join-Path $binRoot 'InputGateTests.exe')) @inputObjects '/link' 'Dinput8.lib' 'DXGUID.lib' 'User32.lib'
        if ($LASTEXITCODE -ne 0) { throw 'Input test build failed.' }
    }
    if (Test-Path -LiteralPath (Join-Path $nativeRoot 'tests/MapProbeTests.cpp')) {
        $probeObjects=@('JobProbe','MapTint','MapTintBridge','buffer','hook','trampoline','hde64') | ForEach-Object { Join-Path $objRoot ($_+'.obj') }
        & $compiler @compileGame '/nologo' '/std:c++17' '/EHsc' '/O2' '/MT' '/utf-8' (Join-Path $nativeRoot 'tests/MapProbeTests.cpp') ('/Fo'+(Join-Path $objRoot 'MapProbeTests.obj')) ('/Fe'+(Join-Path $binRoot 'MapProbeTests.exe')) @probeObjects '/link' 'User32.lib' 'Advapi32.lib'
        if ($LASTEXITCODE -ne 0) { throw 'Map probe test build failed.' }
    }
    if (Test-Path -LiteralPath (Join-Path $nativeRoot 'tests/MapTintTests.cpp')) {
        $assembler=Join-Path $compilerRoot.FullName 'bin\Hostx64\x64\ml64.exe'
        & $assembler '/nologo' '/c' ('/Fo'+(Join-Path $objRoot 'MapTintTestBridge.obj')) (Join-Path $nativeRoot 'tests/MapTintTestBridge.asm')
        if ($LASTEXITCODE -ne 0) { throw 'Map tint test bridge assembly failed.' }
        $tintObjects=@('MapTintBridge','MapTintTestBridge','buffer','hook','trampoline','hde64') | ForEach-Object { Join-Path $objRoot ($_+'.obj') }
        & $compiler @compileGame '/nologo' '/std:c++17' '/EHsc' '/O2' '/MT' '/utf-8' (Join-Path $nativeRoot ('tests/'+$(if($Game -eq 'ats'){'ATSMapTintTests'}else{'MapTintTests'})+'.cpp')) ('/Fo'+(Join-Path $objRoot 'MapTintTests.obj')) ('/Fe'+(Join-Path $binRoot 'MapTintTests.exe')) @tintObjects '/link' 'User32.lib'
        if ($LASTEXITCODE -ne 0) { throw 'Map tint test build failed.' }
    }
    if (Test-Path -LiteralPath (Join-Path $nativeRoot 'tests/JobProbeTests.cpp')) {
        $jobObjects=@('buffer','hook','trampoline','hde64') | ForEach-Object { Join-Path $objRoot ($_+'.obj') }
        & $compiler @compileGame '/nologo' '/std:c++17' '/EHsc' '/O2' '/MT' '/utf-8' (Join-Path $nativeRoot 'tests/JobProbeTests.cpp') ('/Fo'+(Join-Path $objRoot 'JobProbeTests.obj')) ('/Fe'+(Join-Path $binRoot 'JobProbeTests.exe')) @jobObjects '/link' 'User32.lib'
        if ($LASTEXITCODE -ne 0) { throw 'Job probe test build failed.' }
    }
    & $compiler @compileGame '/nologo' '/std:c++17' '/EHsc' '/O2' '/MT' '/utf-8' (Join-Path $nativeRoot 'tests/LocalizationTests.cpp') ('/Fo'+(Join-Path $objRoot 'LocalizationTests.obj')) ('/Fe'+(Join-Path $binRoot 'LocalizationTests.exe')) '/link' 'Shell32.lib' 'Ole32.lib'
    if($LASTEXITCODE -ne 0){throw 'Localization test build failed'}
    $filterTest=if($Game -eq 'ats'){'ATSCoreTests'}else{'OfferFilterTests'}
    & $compiler @compileGame '/nologo' '/std:c++17' '/EHsc' '/O2' '/MT' '/utf-8' (Join-Path $nativeRoot ('tests/'+$filterTest+'.cpp')) ('/Fo'+(Join-Path $objRoot 'OfferFilterTests.obj')) ('/Fe'+(Join-Path $binRoot 'OfferFilterTests.exe'))
    if ($LASTEXITCODE -ne 0) { throw 'Offer filter test build failed.' }
    Get-FileHash -LiteralPath (Join-Path $binRoot 'AchievementAssistant.dll') -Algorithm SHA256
} finally {
    $env:INCLUDE = $savedInclude
    $env:LIB = $savedLib
}
