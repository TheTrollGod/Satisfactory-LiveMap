<#
.SYNOPSIS
  Install or update Live Map on Windows: a dedicated server or the game itself.

.DESCRIPTION
  Run it from the game or server install folder (the one containing FactoryGame\):

    irm https://raw.githubusercontent.com/TheTrollGod/Satisfactory-LiveMap/main/scripts/install.ps1 | iex

  It picks the matching release zip by looking at what's installed:
    FactoryServer.exe present  -> LiveMap-WindowsServer
    FactoryGame*.exe present   -> LiveMap-Windows (Steam and Epic)

  With options, download the script first:
    .\install.ps1 -Path 'C:\SatisfactoryServer' -Version v0.1.0-build.13
    .\install.ps1 -Uninstall

  Stop the game or server before installing or updating.
#>
param(
  [string]$Path = (Get-Location).Path,
  [ValidateSet('', 'windows-server', 'windows')][string]$Target = '',
  [string]$Version = 'latest',
  [string]$Zip = '',
  [switch]$Uninstall
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # Invoke-WebRequest is very slow with the progress bar
$Repo = 'TheTrollGod/Satisfactory-LiveMap'

function Info($m) { Write-Host "==> $m" }
function Warn($m) { Write-Warning $m }

# ---- find the install ----
if (-not (Test-Path (Join-Path $Path 'FactoryGame'))) {
  $candidates = @(
    'C:\Program Files (x86)\Steam\steamapps\common\SatisfactoryDedicatedServer',
    'C:\Program Files (x86)\Steam\steamapps\common\Satisfactory',
    'C:\Program Files\Epic Games\Satisfactory',
    'C:\SatisfactoryServer'
  )
  $found = $candidates | Where-Object { Test-Path (Join-Path $_ 'FactoryGame') } | Select-Object -First 1
  if (-not $found) { throw "Couldn't find Satisfactory. Run this from the install folder (the one containing FactoryGame\), or pass -Path." }
  $Path = $found
}
$Mods = Join-Path $Path 'FactoryGame\Mods'
$Dest = Join-Path $Mods 'LiveMap'

# ---- work out which build it needs ----
if (-not $Target) {
  if (Test-Path (Join-Path $Path 'FactoryServer.exe')) { $Target = 'windows-server' }
  elseif (Get-ChildItem $Path -Filter 'FactoryGame*.exe' -ErrorAction SilentlyContinue) { $Target = 'windows' }
  else { throw "Couldn't tell what's installed in $Path; pass -Target windows-server or windows" }
}
$AssetName = if ($Target -eq 'windows-server') { 'LiveMap-WindowsServer' } else { 'LiveMap-Windows' }
$Kind = if ($Target -eq 'windows-server') { 'Windows dedicated server' } else { 'game client' }
Info "Install: $Path ($Kind)"

if ($Uninstall) {
  if (Test-Path $Dest) { Remove-Item $Dest -Recurse -Force; Info "Removed $Dest" } else { Info "Live Map isn't installed." }
  return
}

$Tmp = Join-Path ([IO.Path]::GetTempPath()) ("livemap-" + [guid]::NewGuid())
New-Item -ItemType Directory $Tmp | Out-Null
try {
  if (-not $Zip) {
    $api = if ($Version -eq 'latest') { "https://api.github.com/repos/$Repo/releases/latest" } else { "https://api.github.com/repos/$Repo/releases/tags/$Version" }
    $release = Invoke-RestMethod $api -Headers @{ 'User-Agent' = 'livemap-installer' }
    $asset = $release.assets | Where-Object { $_.name -match "^$AssetName-\d.*\.zip$" } | Select-Object -First 1
    if (-not $asset) { throw "Release '$($release.tag_name)' has no $AssetName zip" }
    Info "Downloading $($asset.name)"
    $Zip = Join-Path $Tmp 'livemap.zip'
    Invoke-WebRequest $asset.browser_download_url -OutFile $Zip -Headers @{ 'User-Agent' = 'livemap-installer' }
  }
  if (-not (Test-Path $Zip)) { throw "No such file: $Zip" }

  $Extract = Join-Path $Tmp 'mod'
  Expand-Archive $Zip -DestinationPath $Extract
  $uplugin = Get-ChildItem $Extract -Filter 'LiveMap.uplugin' -Recurse -Depth 1 | Select-Object -First 1
  if (-not $uplugin) { throw "That zip doesn't contain LiveMap.uplugin" }
  $Src = $uplugin.Directory.FullName
  if ((Test-Path (Join-Path $Src 'Binaries')) -and -not (Test-Path (Join-Path $Src 'Binaries\Win64'))) {
    Warn "This zip has no Win64 binaries, but this looks like a $Kind (expected $AssetName)"
  }

  New-Item -ItemType Directory -Force $Mods | Out-Null
  if (Test-Path $Dest) { Remove-Item $Dest -Recurse -Force }
  Copy-Item $Src $Dest -Recurse
  Info "Installed Live Map to $Dest"

  if (-not (Test-Path (Join-Path $Mods 'SML'))) {
    Warn "SML isn't installed here ($Mods\SML is missing). Live Map won't load without it. Install it with Satisfactory Mod Manager: https://smm.ficsit.app"
  }
}
finally {
  Remove-Item $Tmp -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ""
Info "Done. Start the world, allow TCP port 8086 through the firewall, then browse to http://<host>:8086/"
