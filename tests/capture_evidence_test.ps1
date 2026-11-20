$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../tools/perf/capture_evidence.ps1')
function Check($Value,[string]$Message){if(!$Value){throw $Message}}
$tempBase=[IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$leaf='wonder-capture-evidence-'+[guid]::NewGuid().ToString('N')
$root=[IO.Path]::GetFullPath((Join-Path $tempBase $leaf))
try {
    $source=Join-Path $root 'frames';$output=Join-Path $root 'evidence'
    New-Item -ItemType Directory -Path $source,$output | Out-Null
    $since=[DateTime]::UtcNow.AddMinutes(-1)
    $old=Join-Path $source 'old.ppm'
    [IO.File]::WriteAllBytes($old,(New-Object byte[] 50))
    (Get-Item -LiteralPath $old).LastWriteTimeUtc=$since.AddMinutes(-1)
    foreach($entry in @(@('one.ppm',100),@('two.ppm',200),@('other.raw',500))) {
        [IO.File]::WriteAllBytes((Join-Path $source $entry[0]),(New-Object byte[] $entry[1]))
    }
    $usage=Get-WonderCaptureUsage $source $since
    Check ($usage.count -eq 2 -and $usage.bytes -eq 300) 'Budget counts only fresh PPM evidence'
    $stream=[IO.File]::OpenWrite((Join-Path $source 'one.ppm'))
    try{$stream.SetLength(110)}finally{$stream.Dispose()}
    $usage=Get-WonderCaptureUsage $source $since
    Check ($usage.bytes -eq 310) 'Budget sees capture growth'
    Move-WonderCaptureEvidence $usage.frames $output
    Check ((Get-WonderCaptureUsage $source $since).count -eq 0) 'Fresh captures are not left duplicated'
    Check ((Test-Path -LiteralPath $old) -and (Test-Path -LiteralPath (Join-Path $source 'other.raw'))) 'Older and unrelated evidence preserved'
    Check ((Get-Item -LiteralPath (Join-Path $output 'one.ppm')).Length -eq 110) 'Exact captured bytes retained'
    $duplicate=Join-Path $source 'one.ppm'
    [IO.File]::WriteAllBytes($duplicate,(New-Object byte[] 99))
    $rejected=$false
    try{Move-WonderCaptureEvidence @((Get-Item -LiteralPath $duplicate)) $output}catch{$rejected=$true}
    Check ($rejected -and (Get-Item -LiteralPath $duplicate).Length -eq 99 -and
        (Get-Item -LiteralPath (Join-Path $output 'one.ppm')).Length -eq 110) 'Existing output rejects replacement and retains source'
    $empty=Get-WonderCaptureUsage (Join-Path $root 'missing') $since
    Check ($empty.count -eq 0 -and $empty.bytes -eq 0) 'Missing capture directory has zero usage'
    Write-Output 'Capture evidence: fresh-byte accounting, growth, verified transfer and preservation passed'
} finally {
    # This exact test-owned directory is the only recursive cleanup target.
    $resolved=[IO.Path]::GetFullPath($root)
    if($resolved -ne (Join-Path $tempBase $leaf) -or
       !$resolved.StartsWith($tempBase.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing cleanup outside the test-owned temporary directory'
    }
    if(Test-Path -LiteralPath $resolved){Remove-Item -LiteralPath $resolved -Recurse -Force}
}
