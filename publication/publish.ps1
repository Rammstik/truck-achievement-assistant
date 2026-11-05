param([Parameter(Mandatory=$true)][string]$GitHubCLI)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$version=(Get-Content -LiteralPath (Join-Path $root 'installer/release.json') -Raw | ConvertFrom-Json).version
$repo='Rammstik/truck-achievement-assistant'
$sourceRepo='Rammstik/truck-achievement-assistant-source'
function Invoke-Gh {
    & $GitHubCLI @args
    if($LASTEXITCODE -ne 0){throw 'GitHub command failed'}
}
$sourceState=Invoke-Gh api ('repos/'+$sourceRepo) | ConvertFrom-Json
$publicState=Invoke-Gh api ('repos/'+$repo) | ConvertFrom-Json
if(-not $sourceState.private -or $publicState.private){throw 'Repository visibility does not match the agreed publication policy'}
$publicRoot=Join-Path $PSScriptRoot 'public'
$allow=@('README.md','.gitignore','RELEASE_NOTES_RU.md')
$actual=@(Get-ChildItem -LiteralPath $publicRoot -File | ForEach-Object Name)
if(@($actual | Where-Object {$_ -notin $allow}).Count -gt 0){throw 'Unexpected file in public documentation'}
$downloadRoot=Join-Path $root 'outputs/public'
$assets=@((Join-Path $downloadRoot "ETS2-Achievement-Assistant-Setup-$version.exe"),(Join-Path $downloadRoot "ETS2-Achievement-Assistant-Portable-$version.zip"),(Join-Path $downloadRoot 'SHA256SUMS.txt'))
foreach($asset in $assets){if(-not (Test-Path -LiteralPath $asset -PathType Leaf)){throw "Missing release asset: $asset"}}
# A separate working copy prevents product source from entering the public repository.
$checkout=Join-Path $PSScriptRoot 'public-repo'
if(-not (Test-Path -LiteralPath (Join-Path $checkout '.git'))){
    New-Item -ItemType Directory -Force -Path $checkout | Out-Null
    & git -C $checkout init -b main
    & git -C $checkout remote add origin ('https://github.com/'+$repo+'.git')
}
& git -C $checkout config user.name Rammstik
& git -C $checkout config user.email '103355750+Rammstik@users.noreply.github.com'
& git -C $checkout config core.autocrlf false
foreach($name in $allow){Copy-Item -LiteralPath (Join-Path $publicRoot $name) -Destination (Join-Path $checkout $name) -Force}
& git -C $checkout add -- @allow
& git -C $checkout diff --cached --quiet
if($LASTEXITCODE -eq 1){
    & git -C $checkout commit -m "Publish ETS2 $version installation guide"
    if($LASTEXITCODE -ne 0){throw 'Public documentation commit failed'}
}
$tracked=@(& git -C $checkout ls-files)
if(@($tracked | Where-Object {$_ -notin $allow}).Count -gt 0){throw 'Public repository includes files outside the documentation allow-list'}
& git -C $checkout push -u origin main
if($LASTEXITCODE -ne 0){throw 'Public documentation push failed'}
Invoke-Gh release create "ets2-v$version" @assets --repo $repo --target main --prerelease --title "ETS2 Achievement Assistant $version — ETS2 1.62.1.0" --notes-file (Join-Path $publicRoot 'RELEASE_NOTES_RU.md')
# Validate actual published visibility and asset checksums using server-side digests.
$release=Invoke-Gh api ("repos/$repo/releases/tags/ets2-v$version") | ConvertFrom-Json
if($release.draft -or -not $release.prerelease -or $release.assets.Count -ne $assets.Count){throw 'Unexpected published release state'}
foreach($asset in $assets){
    $remote=$release.assets | Where-Object name -eq ([IO.Path]::GetFileName($asset))
    $hash=(Get-FileHash -LiteralPath $asset -Algorithm SHA256).Hash.ToLowerInvariant()
    if(-not $remote -or $remote.digest -ne ('sha256:'+$hash)){throw "Uploaded asset checksum mismatch: $asset"}
}
$release | Select-Object html_url,prerelease,@{n='assets';e={$_.assets | Select-Object name,size,digest}}
