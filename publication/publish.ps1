param([Parameter(Mandatory=$true)][string]$GitHubCLI, [switch]$ValidateOnly, [switch]$Prerelease)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$version=(Get-Content -LiteralPath (Join-Path $root 'installer/release.json') -Raw | ConvertFrom-Json).version
$repo='Rammstik/truck-achievement-assistant'
function Invoke-Gh {
    & $GitHubCLI @args
    if($LASTEXITCODE -ne 0){throw 'GitHub command failed'}
}
function Invoke-Git {
    & git -c ('safe.directory='+$root.Replace('\','/')) -C $root @args
    if($LASTEXITCODE -ne 0){throw 'Git command failed'}
}
$state=Invoke-Gh api ('repos/'+$repo) | ConvertFrom-Json
if($state.private){throw 'The release repository must be public'}
$remote=Invoke-Git remote get-url origin
if($LASTEXITCODE -ne 0 -or $remote -ne ('https://github.com/'+$repo+'.git')){throw 'Unexpected origin repository'}
$tracked=@(Invoke-Git ls-files)
if($LASTEXITCODE -ne 0){throw 'Could not inspect tracked files'}
$unexpected=@($tracked | Where-Object {
    $_ -notmatch '^(\.gitattributes|\.gitignore|\.github/(?:FUNDING\.yml|rammstik-donate\.png)$|README(?:\.en)?\.md|RELEASE_NOTES_RU\.md|requirements-build\.txt|installer/|publication/|work/(native-assistant|achievement-core|achievement-lab)/)' -or
    $_ -match '(^|/)(player-data|sessions|staging|test-area|test-results|public-repo|bin|bin-ats|build|build-ats|pybuild|__pycache__)/|\.(exe|dll|zip|log|pyc|obj|pdb)$'
})
if($unexpected.Count -gt 0){$unexpected; throw 'Unexpected file in the public source tree'}
if(@(Invoke-Git status --porcelain).Count -gt 0){throw 'Commit source changes before publishing a release'}
$commit=Invoke-Git rev-parse HEAD
$downloadRoot=Join-Path $root 'outputs/public'
$assets=@((Join-Path $downloadRoot "ETS2-Achievement-Assistant-Setup-$version.exe"),(Join-Path $downloadRoot "ETS2-Achievement-Assistant-Portable-$version.zip"),(Join-Path $downloadRoot 'SHA256SUMS.txt'))
foreach($asset in $assets){if(-not (Test-Path -LiteralPath $asset -PathType Leaf)){throw "Missing release asset: $asset"}}
if($ValidateOnly){Write-Output "Validated public repository, committed source and release assets for $version"; return}
Invoke-Git push -u origin main
# A new release tag points to the exact source commit. Existing releases are never overwritten.
$releaseFlags=@()
if($Prerelease){$releaseFlags+='--prerelease'}
Invoke-Gh release create "ets2-v$version" @assets --repo $repo --target $commit @releaseFlags --title "ETS2 Achievement Assistant $version" --notes-file (Join-Path $PSScriptRoot 'public/RELEASE_NOTES_RU.md')
$release=Invoke-Gh api ("repos/$repo/releases/tags/ets2-v$version") | ConvertFrom-Json
if($release.draft -or $release.prerelease -ne [bool]$Prerelease -or $release.assets.Count -ne $assets.Count){throw 'Unexpected published release state'}
foreach($asset in $assets){
    $remoteAsset=$release.assets | Where-Object name -eq ([IO.Path]::GetFileName($asset))
    $hash=(Get-FileHash -LiteralPath $asset -Algorithm SHA256).Hash.ToLowerInvariant()
    if(-not $remoteAsset -or $remoteAsset.digest -ne ('sha256:'+$hash)){throw "Uploaded asset checksum mismatch: $asset"}
}
$release | Select-Object html_url,prerelease,@{n='assets';e={$_.assets | Select-Object name,size,digest}}
