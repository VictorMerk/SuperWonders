# Collect a small, local report without copying game data, saves, or shader binaries.
param(
    [Parameter(Mandatory = $true)][string]$GameRoot,
    [string]$OutputDirectory = ""
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $GameRoot).Path
$data = Join-Path $root 'data'
if (-not (Test-Path -LiteralPath $data -PathType Container)) { throw 'GameRoot must contain the data folder.' }
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $root ('diagnostics\report-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
}
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Choose a new output directory to preserve existing reports.' }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
foreach ($name in @('nx.log', 'nx.log.prev', 'pc-settings.ini')) {
    $source = Join-Path $data $name
    if (Test-Path -LiteralPath $source -PathType Leaf) { Copy-Item -LiteralPath $source -Destination $OutputDirectory }
}
# Include only the two newest bounded timing reports, never shaders/game data.
Get-ChildItem -LiteralPath $data -Filter 'performance-*.csv' -File |
    Where-Object { $_.Length -le 32MB } | Sort-Object LastWriteTime -Descending | Select-Object -First 2 |
    ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $OutputDirectory }
if (Test-Path -LiteralPath (Join-Path $root 'VERSION')) {
    Copy-Item -LiteralPath (Join-Path $root 'VERSION') -Destination $OutputDirectory
}
$report = [ordered]@{
    collected_at = (Get-Date).ToString('o')
    gpu = @(Get-CimInstance Win32_VideoController | Select-Object Name, DriverVersion)
    cpu = @(Get-CimInstance Win32_Processor | Select-Object Name, NumberOfCores, NumberOfLogicalProcessors)
    memory_gb = [math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB, 1)
    caches = @(Get-ChildItem -LiteralPath $data -File | Where-Object {
        $_.Name -match '^(pipelines_|pipeline_keys_|shader_prepare_)'
    } | Select-Object Name, Length)
    mods = @(if (Test-Path -LiteralPath (Join-Path $data 'mods')) {
        Get-ChildItem -LiteralPath (Join-Path $data 'mods') -Directory | Select-Object -ExpandProperty Name
    })
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'system.json') -Encoding UTF8
'Logs can contain local file paths. Review this directory before sharing it.' |
    Set-Content -LiteralPath (Join-Path $OutputDirectory 'README.txt') -Encoding UTF8
Write-Output (Resolve-Path -LiteralPath $OutputDirectory).Path
