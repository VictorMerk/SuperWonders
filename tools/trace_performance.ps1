# Run a normal game session with local timing diagnostics. No assets or saves are copied.
param([Parameter(Mandatory=$true)][string]$GameRoot)
$ErrorActionPreference='Stop'
$root=(Resolve-Path -LiteralPath $GameRoot).Path
$exe=Join-Path $root 'wonder.exe'
$data=Join-Path $root 'data'
if (!(Test-Path -LiteralPath $exe -PathType Leaf) -or !(Test-Path -LiteralPath $data -PathType Container)) {
    throw 'GameRoot must contain wonder.exe and its prepared data folder.'
}
if (Get-Process wonder,wonder-launcher,mkw -ErrorAction SilentlyContinue) {
    throw 'Close the running game before starting a measured session.'
}
$trace=Join-Path $data ('performance-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff')+'.csv')
$previous=$env:NX_FRAMETRACE
$previousData=$env:NX_DATA_ROOT
try {
    $env:NX_FRAMETRACE=$trace
    $env:NX_DATA_ROOT=$data
    Write-Output 'Play through the stutter, then quit normally. The trace stays on this PC.'
    & $exe
} finally {
    $env:NX_FRAMETRACE=$previous
    $env:NX_DATA_ROOT=$previousData
}
Write-Output $trace
