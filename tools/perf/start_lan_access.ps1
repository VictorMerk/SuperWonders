param(
    [string]$AllowedClient,
    [string]$PublicKeyPath=(Join-Path $PSScriptRoot 'session_key.pub'),
    [ValidateRange(10,120)][int]$Minutes=90,
    [ValidateRange(1024,65535)][int]$Port=27847,
    [string]$SessionDirectory,
    [switch]$Worker,
    [switch]$Stop
)
$ErrorActionPreference='Stop'

function New-SshServiceFileAcl([Security.Principal.SecurityIdentifier]$OwnerSid=[Security.Principal.SecurityIdentifier]::new('S-1-5-18')) {
    $acl=New-Object Security.AccessControl.FileSecurity
    $acl.SetOwner($OwnerSid)
    $acl.SetAccessRuleProtection($true,$false)
    foreach($sid in @($OwnerSid.Value,'S-1-5-18','S-1-5-32-544') | Select-Object -Unique) {
        $identity=[Security.Principal.SecurityIdentifier]::new($sid)
        $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($identity,[Security.AccessControl.FileSystemRights]::FullControl,[Security.AccessControl.AccessControlType]::Allow))
    }
    return $acl
}
function Require-Administrator {
    $identity=[Security.Principal.WindowsIdentity]::GetCurrent()
    if (!(New-Object Security.Principal.WindowsPrincipal($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run this script as administrator. It creates a temporary firewall rule and scheduled task.'
    }
}
function Session-Path([string]$path) {
    $base=[IO.Path]::GetFullPath((Join-Path $env:ProgramData 'SuperWonders-LAN-QA'))
    $full=[IO.Path]::GetFullPath($path)
    if ([IO.Path]::GetDirectoryName($full) -ne $base -or [IO.Path]::GetFileName($full) -notmatch '^[a-f0-9]{32}$') {
        throw 'Invalid temporary session directory.'
    }
    return $full
}
function Clean-Session($info) {
    Remove-NetFirewallRule -Name $info.rule -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName $info.task -Confirm:$false -ErrorAction SilentlyContinue
}

Require-Administrator
if ($Stop) {
    $directory=Session-Path $SessionDirectory
    $info=Get-Content -LiteralPath (Join-Path $directory 'session.json') -Raw | ConvertFrom-Json
    Stop-ScheduledTask -TaskName $info.task -ErrorAction SilentlyContinue
    Clean-Session $info
    Write-Host 'Temporary LAN access stopped.'
    exit
}
if ($Worker) {
    # A private, one-shot task runs the standalone server as SYSTEM, which
    # Windows OpenSSH needs for key-based user logon. No existing SSH service,
    # system sshd_config or normal authorized_keys file is changed.
    $directory=Session-Path $SessionDirectory
    $info=Get-Content -LiteralPath (Join-Path $directory 'session.json') -Raw | ConvertFrom-Json
    # ssh-keygen applies its own creator ACL. Directory inheritance alone is
    # insufficient when the daemon later runs as SYSTEM. Replace the file ACL,
    # including ownership, in the daemon's security context before loading keys.
    foreach($name in @('host_key','host_key.pub','authorized_keys','sshd_config','helper.ps1','session.json')) {
        $file=Join-Path $directory $name
        [IO.File]::SetAccessControl($file,(New-SshServiceFileAcl))
        $actual=Get-Acl -LiteralPath $file
        if ($actual.GetOwner([Security.Principal.SecurityIdentifier]).Value -ne 'S-1-5-18' -or !$actual.AreAccessRulesProtected) { throw ('Could not secure SSH service file: '+$name) }
        foreach($rule in $actual.GetAccessRules($true,$true,[Security.Principal.SecurityIdentifier])) {
            if($rule.IdentityReference.Value -notin @('S-1-5-18','S-1-5-32-544')) { throw ('Unexpected permission on SSH service file: '+$name) }
        }
    }
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public sealed class LanSshJob : IDisposable {
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] static extern IntPtr CreateJobObject(IntPtr a,string n);
 [DllImport("kernel32.dll")] static extern bool SetInformationJobObject(IntPtr j,int c,IntPtr p,uint n);
 [DllImport("kernel32.dll")] static extern bool AssignProcessToJobObject(IntPtr j,IntPtr p);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
 [StructLayout(LayoutKind.Sequential)] struct Basic { public long a,b; public uint flags; public UIntPtr lo,hi; public uint active; public UIntPtr affinity; public uint priority,scheduling; }
 [StructLayout(LayoutKind.Sequential)] struct Io { public ulong a,b,c,d,e,f; }
 [StructLayout(LayoutKind.Sequential)] struct Limits { public Basic basic; public Io io; public UIntPtr process,job,peakProcess,peakJob; }
 IntPtr handle;
 public LanSshJob() {
  handle=CreateJobObject(IntPtr.Zero,null);
  var limit=new Limits(); limit.basic.flags=0x2000; // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
  int size=Marshal.SizeOf(typeof(Limits)); IntPtr p=Marshal.AllocHGlobal(size);
  try { Marshal.StructureToPtr(limit,p,false); if(handle==IntPtr.Zero || !SetInformationJobObject(handle,9,p,(uint)size)) throw new Exception("Could not create an expiring SSH process job"); }
  finally { Marshal.FreeHGlobal(p); }
 }
 public void Assign(IntPtr process) { if(!AssignProcessToJobObject(handle,process)) throw new Exception("Could not contain SSH processes; access was not opened"); }
 public void Dispose() { if(handle!=IntPtr.Zero) { CloseHandle(handle); handle=IntPtr.Zero; } }
}
'@
    $job=$null; $server=$null
    try {
        $job=New-Object LanSshJob
        $arguments='-D -f "'+(Join-Path $directory 'sshd_config')+'" -E "'+(Join-Path $directory 'sshd.log')+'"'
        $server=Start-Process -FilePath $info.sshd -ArgumentList $arguments -WindowStyle Hidden -PassThru
        $job.Assign($server.Handle)
        Start-Sleep -Seconds 2
        $server.Refresh()
        if ($server.HasExited) { throw 'OpenSSH exited. See sshd.log in the session directory.' }
        New-NetFirewallRule -Name $info.rule -DisplayName 'SuperWonders temporary LAN test SSH' -Direction Inbound -Action Allow -Protocol TCP -LocalPort $info.port -LocalAddress $info.address -RemoteAddress $info.client -Profile $info.profile -Program $info.sshd | Out-Null
        Set-Content -LiteralPath (Join-Path $directory 'ready.txt') -Value 'ready' -Encoding Ascii
        while ((Get-Date).ToUniversalTime() -lt ([DateTime]$info.expires_utc).ToUniversalTime()) {
            Start-Sleep -Seconds 1
            $owner=Get-Process -Id $info.owner_pid -ErrorAction SilentlyContinue
            if (!$owner -or $owner.StartTime.ToUniversalTime().ToString('o') -ne $info.owner_started_utc) { break }
            $server.Refresh()
            if ($server.HasExited) { break }
        }
    } catch {
        $_ | Out-String | Set-Content -LiteralPath (Join-Path $directory 'error.txt')
    } finally {
        if ($job) { $job.Dispose() }
        if ($server) { $server.Refresh(); if (!$server.HasExited) { $server.Kill() } }
        Clean-Session $info
    }
    exit
}

$ip=$null
if (![Net.IPAddress]::TryParse($AllowedClient,[ref]$ip) -or $ip.AddressFamily -ne [Net.Sockets.AddressFamily]::InterNetwork) { throw 'AllowedClient must be the other PC''s private IPv4 address.' }
$bytes=$ip.GetAddressBytes()
if (!($bytes[0] -eq 10 -or ($bytes[0] -eq 172 -and $bytes[1] -ge 16 -and $bytes[1] -le 31) -or ($bytes[0] -eq 192 -and $bytes[1] -eq 168))) { throw 'Only private LAN client addresses are accepted.' }
$public=(Get-Content -LiteralPath $PublicKeyPath -Raw).Trim()
if ($public -notmatch '^ssh-ed25519 [A-Za-z0-9+/=]+(?: [^\r\n]*)?$') { throw 'Expected one Ed25519 public key. Never transfer a private key.' }
$route=Find-NetRoute -RemoteIPAddress $AllowedClient | Where-Object { $_.IPAddress } | Select-Object -First 1
if (!$route) { throw 'No route to the authorized client PC was found.' }
$network=Get-NetConnectionProfile -InterfaceIndex $route.InterfaceIndex | Select-Object -First 1
if (!$network) { throw 'No active network profile found for the route to the other PC.' }
$address=$route.IPAddress
if (!$address) { throw 'No local IPv4 address found.' }
$local=Get-NetIPAddress -InterfaceIndex $route.InterfaceIndex -IPAddress $address -AddressFamily IPv4 | Select-Object -First 1
if (!$local -or $local.PrefixLength -gt 30) { throw 'This helper requires a directly connected LAN subnet.' }
$localBytes=([Net.IPAddress]::Parse($address)).GetAddressBytes()
if (!($localBytes[0] -eq 10 -or ($localBytes[0] -eq 172 -and $localBytes[1] -ge 16 -and $localBytes[1] -le 31) -or ($localBytes[0] -eq 192 -and $localBytes[1] -eq 168))) { throw 'Only a private LAN interface may host the temporary server.' }
for($i=0;$i -lt 4;$i++) {
    $bits=[Math]::Min(8,[Math]::Max(0,[int]$local.PrefixLength-8*$i))
    $mask=if($bits -eq 0){0}else{(255 -shl (8-$bits)) -band 255}
    if (($bytes[$i] -band $mask) -ne ($localBytes[$i] -band $mask)) { throw 'Both PCs must be on the same directly connected private LAN subnet.' }
}
$profile=if($network.NetworkCategory -eq 'DomainAuthenticated'){'Domain'}else{$network.NetworkCategory.ToString()}
$sshd=Join-Path $env:WINDIR 'System32/OpenSSH/sshd.exe'
if (!(Test-Path -LiteralPath $sshd)) {
    Write-Host 'Installing Microsoft''s Windows OpenSSH Server optional component. Its files remain installed; this helper does not enable the normal SSH service.'
    $defaultRuleExisted=!!(Get-NetFirewallRule -Name 'OpenSSH-Server-In-TCP' -ErrorAction SilentlyContinue)
    Add-WindowsCapability -Online -Name 'OpenSSH.Server~~~~0.0.1.0' | Out-Null
    if (!$defaultRuleExisted) { Remove-NetFirewallRule -Name 'OpenSSH-Server-In-TCP' -ErrorAction SilentlyContinue }
    if (!(Test-Path -LiteralPath $sshd)) { throw 'OpenSSH installation did not finish. No temporary access was opened.' }
}
$sessionId=[Guid]::NewGuid().ToString('N')
$directory=Join-Path (Join-Path $env:ProgramData 'SuperWonders-LAN-QA') $sessionId
New-Item -ItemType Directory -Path $directory -Force | Out-Null
# Localized Windows group names are avoided through well-known SIDs.
& icacls.exe $directory /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)(F)' '*S-1-5-32-544:(OI)(CI)(F)' | Out-Null
if ($LASTEXITCODE) { throw 'Could not secure the temporary session directory.' }
Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $directory 'helper.ps1')
$hostKey=Join-Path $directory 'host_key'
$keygen=Join-Path (Split-Path $sshd) 'ssh-keygen.exe'
$process=Start-Process -FilePath $keygen -ArgumentList ('-q -t ed25519 -f "'+$hostKey+'" -N ""') -WindowStyle Hidden -Wait -PassThru
if ($process.ExitCode -ne 0) { throw 'Could not generate the temporary SSH host key.' }
Set-Content -LiteralPath (Join-Path $directory 'authorized_keys') -Value ('from="'+$AllowedClient+'",no-agent-forwarding,no-port-forwarding,no-X11-forwarding '+$public) -Encoding Ascii
$unix=$directory.Replace('\','/')
$login=$env:USERNAME.ToLowerInvariant()
$config=@"
Port $Port
ListenAddress $address
HostKey "$unix/host_key"
PidFile "$unix/sshd.pid"
AuthorizedKeysFile "$unix/authorized_keys"
AllowUsers $login
PubkeyAuthentication yes
PasswordAuthentication no
ChallengeResponseAuthentication no
PermitEmptyPasswords no
AllowTcpForwarding no
AllowAgentForwarding no
X11Forwarding no
PermitTunnel no
PermitUserEnvironment no
MaxAuthTries 3
LoginGraceTime 30
ClientAliveInterval 30
ClientAliveCountMax 2
LogLevel VERBOSE
Subsystem sftp "$((Join-Path (Split-Path $sshd) 'sftp-server.exe').Replace('\','/'))"
"@
$configPath=Join-Path $directory 'sshd_config'
Set-Content -LiteralPath $configPath -Value $config -Encoding Ascii
& $sshd -t -f $configPath
if ($LASTEXITCODE) { throw 'OpenSSH rejected its temporary configuration. No access was opened.' }
$info=[ordered]@{address=$address;client=$AllowedClient;profile=$profile;port=$Port;login=$login;sshd=$sshd;owner_pid=$PID;owner_started_utc=(Get-Process -Id $PID).StartTime.ToUniversalTime().ToString('o');expires_utc=(Get-Date).ToUniversalTime().AddMinutes($Minutes).ToString('o');rule=('SuperWonders-LAN-QA-'+$sessionId);task=('SuperWonders-LAN-QA-'+$sessionId)}
$info | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $directory 'session.json') -Encoding Ascii
$helperLiteral=(Join-Path $directory 'helper.ps1').Replace("'","''")
$directoryLiteral=$directory.Replace("'","''")
$outputLiteral=(Join-Path $directory 'worker-output.log').Replace("'","''")
# Capture the complete invocation, including failures before the worker's own
# try block (such as C# compilation or an execution-policy error).
$invocation='$ErrorActionPreference=''Stop''; try { & '''+$helperLiteral+''' -Worker -SessionDirectory '''+$directoryLiteral+''' *> '''+$outputLiteral+''' } catch { $_ | Out-String | Add-Content -LiteralPath '''+$outputLiteral+'''; exit 1 }'
$encodedInvocation=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($invocation))
$action=New-ScheduledTaskAction -Execute (Join-Path $env:WINDIR 'System32/WindowsPowerShell/v1.0/powershell.exe') -Argument ('-NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand '+$encodedInvocation) -WorkingDirectory $directory
$principal=New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
$settings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes ($Minutes+2)) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
try {
    Register-ScheduledTask -TaskName $info.task -Action $action -Principal $principal -Settings $settings | Out-Null
    Start-ScheduledTask -TaskName $info.task
    Write-Host 'Starting the temporary SSH worker...'
    $until=(Get-Date).AddSeconds(90)
    $started=Get-Date
    while (!(Test-Path -LiteralPath (Join-Path $directory 'ready.txt'))) {
        if (Test-Path -LiteralPath (Join-Path $directory 'error.txt')) { throw (Get-Content -LiteralPath (Join-Path $directory 'error.txt') -Raw) }
        $state=Get-ScheduledTask -TaskName $info.task
        if ($state.State -ne 'Running' -and (Get-Date) -gt $started.AddSeconds(5)) { throw 'The SSH worker stopped before becoming ready.' }
        if ((Get-Date) -gt $until) { throw 'The temporary SSH worker did not become ready.' }
        Start-Sleep -Milliseconds 500
    }
} catch {
    $failure=$_ | Out-String
    $taskInfo=Get-ScheduledTaskInfo -TaskName $info.task -ErrorAction SilentlyContinue
    Stop-ScheduledTask -TaskName $info.task -ErrorAction SilentlyContinue
    Clean-Session $info
    $diagnostics=''
    if ($taskInfo) { $diagnostics+='Task result: '+$taskInfo.LastTaskResult+"`n" }
    foreach($file in @('error.txt','worker-output.log','sshd.log')) {
        $logPath=Join-Path $directory $file
        if(Test-Path -LiteralPath $logPath) { $diagnostics+=$file+":`n"+((Get-Content -LiteralPath $logPath -Tail 25) -join "`n")+"`n" }
    }
    throw ($failure+"`n"+$diagnostics+"`nSession diagnostics: "+$directory)
}
Write-Host "READY: $login@$address port $Port; expires in $Minutes minutes."
Write-Host 'Send the READY line and this public host fingerprint to Victor (no password or private key):'
& $keygen -l -f ($hostKey+'.pub')
Write-Host 'Stop access early with the STOP-LAN-ACCESS.cmd created beside this script.'
$stopCommand='@echo off'+"`r`n"+'powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Start-Process powershell.exe -Verb RunAs -ArgumentList ''-NoProfile -ExecutionPolicy Bypass -File ""'+(Join-Path $directory 'helper.ps1')+'"" -Stop -SessionDirectory ""'+$directory+'""''"'+"`r`n"
Set-Content -LiteralPath (Join-Path $PSScriptRoot 'STOP-LAN-ACCESS.cmd') -Value $stopCommand -Encoding Ascii
Write-Host 'Leave this window open. Closing it or pressing Ctrl+C stops temporary access.'
try {
    while ((Get-Date).ToUniversalTime() -lt ([DateTime]$info.expires_utc).ToUniversalTime()) {
        Start-Sleep -Seconds 1
        $task=Get-ScheduledTask -TaskName $info.task -ErrorAction SilentlyContinue
        if (!$task -or $task.State -ne 'Running') { break }
    }
} finally {
    Stop-ScheduledTask -TaskName $info.task -ErrorAction SilentlyContinue
    Clean-Session $info
}
