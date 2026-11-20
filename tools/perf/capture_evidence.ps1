# Helpers for task-owned captures. No game, process or reservation operations.
function Publish-WonderCaptureRequest {
    param([Parameter(Mandatory=$true)][string]$DataRoot,
          [ValidateRange(1,120)][int]$Count=1,
          [ValidateRange(1,600)][int]$Every=1)
    $directory=(Resolve-Path -LiteralPath $DataRoot -ErrorAction Stop).Path
    if(!(Test-Path -LiteralPath $directory -PathType Container)){throw 'Capture request needs an existing data directory.'}
    $target=Join-Path $directory 'dump_frames'
    $temporary=Join-Path $directory ('dump_frames.pending-'+[guid]::NewGuid().ToString('N'))
    $stream=$null;$created=$false
    try {
        $bytes=[Text.Encoding]::ASCII.GetBytes("$Count $Every`n")
        $stream=[IO.File]::Open($temporary,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        $created=$true
        $stream.Write($bytes,0,$bytes.Length);$stream.Flush($true);$stream.Dispose();$stream=$null
        # Publish complete bytes in the same directory. Move never replaces an
        # existing request: a second producer must not discard pending work.
        # Direct truncate/write can expose empty bytes, selecting native defaults.
        [IO.File]::Move($temporary,$target)
    } finally {
        if($stream){$stream.Dispose()}
        if($created -and [IO.File]::Exists($temporary)){[IO.File]::Delete($temporary)}
    }
}

function Assert-WonderEmptyCaptureDirectory([string]$Directory) {
    if(!(Test-Path -LiteralPath $Directory -ErrorAction Stop)){return}
    if(!(Test-Path -LiteralPath $Directory -PathType Container -ErrorAction Stop)) {
        throw 'The capture path must be a directory.'
    }
    if(@(Get-ChildItem -LiteralPath $Directory -File -Filter '*.ppm' -ErrorAction Stop).Count) {
        throw 'Fixture captures already exist. Preserve them and use an empty task-owned capture folder.'
    }
}

function Get-WonderCaptureCompletionReason([string]$Reason,[long]$Bytes,[long]$Budget) {
    if($Reason -eq 'exited' -and $Bytes -ge $Budget){return 'capture budget reached'}
    return $Reason
}

function Get-WonderCaptureUsage([string]$Directory,[DateTime]$Since) {
    $frames=@(Get-ChildItem -LiteralPath $Directory -File -Filter '*.ppm' -ErrorAction SilentlyContinue |
        Where-Object {$_.LastWriteTimeUtc -ge $Since.ToUniversalTime()})
    [long]$bytes=0
    foreach($frame in $frames){$bytes+=$frame.Length}
    [pscustomobject]@{frames=$frames;count=$frames.Count;bytes=$bytes}
}

function Move-WonderCaptureEvidence([object[]]$Frames,[string]$Destination) {
    $directory=(Resolve-Path -LiteralPath $Destination -ErrorAction Stop).Path
    foreach($frame in $Frames) {
        $source=Get-Item -LiteralPath $frame.FullName -ErrorAction Stop
        if($source.PSIsContainer -or ($source.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Capture evidence must be an ordinary file.'
        }
        $target=Join-Path $directory $source.Name
        if(Test-Path -LiteralPath $target){throw 'Existing capture evidence must not be replaced.'}
        # Check every copy before removing only this exact source file. The
        # fixture can cross volumes through a junction, even with matching drive
        # letters. Never recurse through fixture links or remove older captures.
        $before=(Get-FileHash -LiteralPath $source.FullName -Algorithm SHA256 -ErrorAction Stop).Hash
        Copy-Item -LiteralPath $source.FullName -Destination $target -ErrorAction Stop
        $after=(Get-FileHash -LiteralPath $target -Algorithm SHA256 -ErrorAction Stop).Hash
        $unchanged=(Get-FileHash -LiteralPath $source.FullName -Algorithm SHA256 -ErrorAction Stop).Hash
        if($before -ne $after -or $before -ne $unchanged){throw 'Capture copy changed; source retained.'}
        Remove-Item -LiteralPath $source.FullName -ErrorAction Stop
    }
}
