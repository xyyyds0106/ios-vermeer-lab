param(
    [ValidateRange(1,86400)][int]$Seconds = 600,
    [string]$Port,
    [string]$LogPath = (Join-Path $PSScriptRoot '..\..\device-audit\live-ios-004.log'),
    [ValidateSet('help','status','ping','replay','sample','frame','input')][string]$Command = 'status',
    [switch]$ObserveFrames,
    [switch]$ListOnly
)
$ErrorActionPreference = 'Stop'

function Find-DebugPort {
    $usbRoot = 'Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\USB'
    $id = Join-Path $usbRoot 'VID_1209&PID_0001'
    if (-not (Test-Path -LiteralPath $id)) { return }
    foreach ($instance in Get-ChildItem -LiteralPath $id) {
        # Match our synthetic serial, never choose an unrelated serial adapter.
        if ($instance.PSChildName -ne 'K70DBG004') { continue }
        $params = Join-Path $instance.PSPath 'Device Parameters'
        $found = (Get-ItemProperty -LiteralPath $params -Name PortName -ErrorAction SilentlyContinue).PortName
        if ($found -match '^COM[0-9]+$') { $found }
    }
}
if ($ListOnly) { Find-DebugPort; return }
if ($Port -and $Port -notmatch '^COM[0-9]+$') { throw 'Invalid COM port name' }
$deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
$serial = $null
$stream = $null
$total = 0L
try {
    while ([DateTime]::UtcNow -lt $deadline) {
        if (-not $Port) { $Port = @(Find-DebugPort | Sort-Object -Unique) | Select-Object -First 1 }
        if ($Port) { break }
        Start-Sleep -Milliseconds 500
    }
    if (-not $Port) { throw 'K70DBG004 did not enumerate as a Windows COM port before timeout' }
    $serial = [System.IO.Ports.SerialPort]::new($Port,115200,[System.IO.Ports.Parity]::None,8,[System.IO.Ports.StopBits]::One)
    $serial.DtrEnable = $true
    $serial.RtsEnable = $true
    $serial.ReadTimeout = 500
    $serial.WriteTimeout = 2000
    $serial.Open()
    $resolvedLog = [System.IO.Path]::GetFullPath($LogPath)
    $null = [System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($resolvedLog))
    # Preserve earlier captures; timestamp names on conflict, then bounded files.
    if ([System.IO.File]::Exists($resolvedLog)) {
        $resolvedLog = $resolvedLog + '.' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
    }
    $stream = [System.IO.File]::Open($resolvedLog,[System.IO.FileMode]::CreateNew,[System.IO.FileAccess]::Write,[System.IO.FileShare]::Read)
    Write-Output "Connected $Port; capture $resolvedLog"
    $serial.Write($Command + "`n")
    $buffer = [byte[]]::new(16384)
    $part = 0
    $nextFrameQuery = [DateTime]::UtcNow
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($ObserveFrames -and [DateTime]::UtcNow -ge $nextFrameQuery) {
            $serial.Write("frame`ninput`n")
            $nextFrameQuery = [DateTime]::UtcNow.AddSeconds(10)
        }
        try { $n = $serial.Read($buffer,0,$buffer.Length) }
        catch [System.TimeoutException] { continue }
        if ($n -le 0) { continue }
        if ($stream.Length + $n -gt 32MB) {
            $stream.Dispose()
            $part++
            $stream = [System.IO.File]::Open($resolvedLog + ".part$part",[System.IO.FileMode]::CreateNew,[System.IO.FileAccess]::Write,[System.IO.FileShare]::Read)
        }
        $stream.Write($buffer,0,$n)
        $stream.Flush()
        $total += $n
        # Keep terminal output small; logs are read from the shared file.
    }
    Write-Output "Saved $total bytes"
} finally {
    if ($stream) { $stream.Dispose() }
    if ($serial) { if ($serial.IsOpen) { $serial.Close() }; $serial.Dispose() }
}
