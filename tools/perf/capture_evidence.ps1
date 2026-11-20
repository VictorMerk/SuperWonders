# Helpers for task-owned captures. No game, process or reservation operations.
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
