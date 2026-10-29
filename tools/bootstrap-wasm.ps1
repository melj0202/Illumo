[CmdletBinding()]
param([string]$Destination = (Join-Path $PSScriptRoot '../build-wasm-tools'))

$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitOperatingSystem -or $env:OS -ne 'Windows_NT') {
  throw 'These pinned toolchains require Windows x64.'
}
$packages = @(
  @{
    Name = 'wasmtime-v48.0.7-x86_64-windows-c-api'
    Archive = 'wasmtime-v48.0.7-x86_64-windows-c-api.zip'
    Url = 'https://github.com/bytecodealliance/wasmtime/releases/download/v48.0.7/wasmtime-v48.0.7-x86_64-windows-c-api.zip'
    Hash = 'd7f39ef35d9b4ab147a67dec2b26dca34bd2901f0b54efd81b6b51c491d9ca17'
  },
  @{
    Name = 'wasmtime-v48.0.7-x86_64-windows'
    Archive = 'wasmtime-v48.0.7-x86_64-windows.zip'
    Url = 'https://github.com/bytecodealliance/wasmtime/releases/download/v48.0.7/wasmtime-v48.0.7-x86_64-windows.zip'
    Hash = '23b43be8720d8fed20caee24d78104c9093717b90f2b4b0bdd171b6dd7f224af'
  },
  @{
    Name = 'wasi-sdk-34.0-x86_64-windows'
    Archive = 'wasi-sdk-34.0-x86_64-windows.tar.gz'
    Url = 'https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-34/wasi-sdk-34.0-x86_64-windows.tar.gz'
    Hash = 'cccb5c323a9b34f0349a9b09e8804a0a7632c68c3310f4b5f437ed57d7e71d8f'
  }
)
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$resolvedDestination = (Resolve-Path -LiteralPath $Destination).Path
foreach ($package in $packages) {
  $archivePath = Join-Path $resolvedDestination $package.Archive
  if (-not (Test-Path -LiteralPath $archivePath)) {
    $partialPath = "$archivePath.partial"
    Invoke-WebRequest -Uri $package.Url -OutFile $partialPath
    if ((Get-FileHash -LiteralPath $partialPath -Algorithm SHA256).Hash -ne $package.Hash) {
      throw "SHA256 mismatch for $($package.Archive); refusing extraction."
    }
    Move-Item -LiteralPath $partialPath -Destination $archivePath
  }
  if ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash -ne $package.Hash) {
    throw "SHA256 mismatch for $archivePath; refusing extraction."
  }
  # Always extract from the verified archive, including when a prior run was interrupted.
  & tar -xf $archivePath -C $resolvedDestination
  if ($LASTEXITCODE -ne 0) { throw "Extraction failed for $archivePath" }
  Write-Output "Verified $($package.Name)"
}
Write-Output "Toolchains are local to $resolvedDestination; no PATH or global installation was changed."
