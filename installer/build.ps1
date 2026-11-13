param([Parameter(Mandatory=$true)][string]$Compiler, [switch]$TestBuild)
$ErrorActionPreference='Stop'
$config=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'release.json') -Raw | ConvertFrom-Json
$arguments=@(('/DProductVersion='+$config.version))
if($TestBuild){$arguments+='/DAA_TEST'}
& $Compiler @arguments (Join-Path $PSScriptRoot 'ETS2Assistant.iss')
if($LASTEXITCODE -ne 0){throw 'Installer compilation failed'}
