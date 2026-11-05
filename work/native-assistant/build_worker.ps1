$ErrorActionPreference='Stop'
$nativeRoot=$PSScriptRoot
$previousCache=$env:PYINSTALLER_CONFIG_DIR
$coreRoot=Join-Path $nativeRoot '../achievement-core'
if(-not (Test-Path -LiteralPath (Join-Path $coreRoot 'ets2_achievements'))){$coreRoot=Join-Path $nativeRoot 'core'}
$coreRoot=(Resolve-Path -LiteralPath $coreRoot).Path
try {
    $env:PYINSTALLER_CONFIG_DIR=Join-Path $nativeRoot 'pyinstaller-cache'
    $dataArgs=@()
    Get-ChildItem -LiteralPath (Join-Path $coreRoot 'ets2_achievements') -Recurse -Filter '*.json' | ForEach-Object { $dataArgs += @('--add-data', ($_.FullName + ';ets2_achievements/' + ([IO.Path]::GetRelativePath((Join-Path $coreRoot 'ets2_achievements'),$_.DirectoryName)))) }
    python -m PyInstaller --noconfirm --noupx --windowed --onedir --name AADataWorker --distpath (Join-Path $nativeRoot 'bin') --workpath (Join-Path $nativeRoot 'pybuild') --specpath $nativeRoot --paths $coreRoot @dataArgs (Join-Path $nativeRoot 'worker/data_worker.py')
    if($LASTEXITCODE -ne 0){throw 'Worker build failed'}
    New-Item -ItemType Directory -Force -Path (Join-Path $nativeRoot 'bin/AchievementAssistant') | Out-Null
    Copy-Item -Path (Join-Path $nativeRoot 'bin/AADataWorker/*') -Destination (Join-Path $nativeRoot 'bin/AchievementAssistant') -Recurse -Force
} finally {$env:PYINSTALLER_CONFIG_DIR=$previousCache}
