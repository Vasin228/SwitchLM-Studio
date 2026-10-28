# deploy.ps1 - Automated SwitchLM Studio Deployer via DBI FTP
# Load local .env if available
$envFile = Join-Path $PSScriptRoot "..\.env"
if (Test-Path $envFile) {
    Get-Content $envFile | ForEach-Object {
        $line = $_.Trim()
        if ($line -and -not $line.StartsWith("#")) {
            $parts = $line.Split("=", 2)
            if ($parts.Length -eq 2) {
                [System.Environment]::SetEnvironmentVariable($parts[0].Trim(), $parts[1].Trim())
            }
        }
    }
}

param(
    [string]$SwitchIp = $env:SWITCH_IP,
    [int]$Port = 5000
)

$ErrorActionPreference = 'Continue'

if (-not $SwitchIp) {
    $SwitchIp = '192.168.1.100'
}
if ($env:SWITCH_FTP_PORT) {
    $Port = [int]$env:SWITCH_FTP_PORT
}

$baseUri = "ftp://${SwitchIp}:${Port}"
$sdout = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\sdout"))

Write-Output "=== SwitchLM Studio Deployment ==="
Write-Output "Target: $baseUri"

function Get-FtpRequest([string]$remotePath, [string]$method) {
    $uri = "${baseUri}${remotePath}"
    $req = [System.Net.FtpWebRequest]::Create($uri)
    $req.Credentials = New-Object System.Net.NetworkCredential('anonymous', 'anonymous')
    $req.UsePassive = $true
    $req.UseBinary = $true
    $req.KeepAlive = $false
    $req.Timeout = 30000
    $req.Method = $method
    return $req
}

function Make-RemoteDir([string]$remoteDir) {
    try {
        $req = Get-FtpRequest $remoteDir ([System.Net.WebRequestMethods+Ftp]::MakeDirectory)
        $resp = $req.GetResponse()
        $resp.Close()
        Write-Output "Created directory: $remoteDir"
    } catch {
        # Directory probably already exists
    }
}

function Upload-File([string]$localFile, [string]$remoteFile) {
    if (-not (Test-Path -LiteralPath $localFile)) {
        Write-Output "File not found: $localFile"
        return
    }

    $fi = Get-Item -LiteralPath $localFile

    # Check if remote file exists with exact same size to avoid redundant re-transfers
    try {
        $sizeReq = Get-FtpRequest $remoteFile ([System.Net.WebRequestMethods+Ftp]::GetFileSize)
        $sizeResp = $sizeReq.GetResponse()
        $remoteLen = $sizeResp.ContentLength
        $sizeResp.Close()
        if ($remoteLen -eq $fi.Length) {
            Write-Output ("  [UP-TO-DATE] {0} ({1} KB)" -f $fi.Name, [math]::Round($fi.Length / 1024, 1))
            return
        }
    } catch {
        # Proceed with upload if file doesn't exist yet
    }

    Write-Output ("Uploading {0} ({1} KB) -> {2} ..." -f $fi.Name, [math]::Round($fi.Length / 1024, 1), $remoteFile)

    try {
        $req = Get-FtpRequest $remoteFile ([System.Net.WebRequestMethods+Ftp]::UploadFile)
        $req.ContentLength = $fi.Length
        $fs = [System.IO.File]::OpenRead($localFile)
        $stream = $req.GetRequestStream()
        $fs.CopyTo($stream)
        $stream.Close()
        $fs.Close()
        $resp = $req.GetResponse()
        Write-Output ("  [OK] {0}" -f $resp.StatusDescription.Trim())
        $resp.Close()
    } catch {
        Write-Output ("  [ERROR] {0}: {1}" -f $remoteFile, $_.Exception.Message)
    }
}

# 1. Probe connectivity
try {
    $req = Get-FtpRequest "/switch/" ([System.Net.WebRequestMethods+Ftp]::ListDirectory)
    $resp = $req.GetResponse()
    $sr = New-Object System.IO.StreamReader($resp.GetResponseStream())
    $list = $sr.ReadToEnd()
    $sr.Close()
    $resp.Close()
    Write-Output "[OK] Connected to DBI FTP successfully."
} catch {
    Write-Output "[WARN] Root probe: $($_.Exception.Message)"
}

# 2. Create remote folders
$dirs = @(
    "/switch",
    "/switch/.overlays",
    "/switch/SwitchLM",
    "/switch/SwitchLM/wikis",
    "/switch/SwitchLM/models",
    "/switch/SwitchLM/micro",
    "/switch/SwitchLM-Studio",
    "/atmosphere",
    "/atmosphere/contents",
    "/atmosphere/contents/0100000000004C4D",
    "/atmosphere/contents/0100000000004C4D/exefs"
)

foreach ($d in $dirs) {
    Make-RemoteDir $d
}

# 3. Upload Targets
# Target 1: Studio NRO
Upload-File "$sdout\switch\SwitchLM-Studio\SwitchLM-Studio.nro" "/switch/SwitchLM-Studio/SwitchLM-Studio.nro"

# Target 2: Overlay OVL
Upload-File "$sdout\switch\.overlays\SwitchLM-Overlay.ovl" "/switch/.overlays/SwitchLM-Overlay.ovl"

# Target 3: Sysmodule
Upload-File "$sdout\atmosphere\contents\0100000000004C4D\toolbox.json" "/atmosphere/contents/0100000000004C4D/toolbox.json"
Upload-File "$sdout\atmosphere\contents\0100000000004C4D\exefs\main" "/atmosphere/contents/0100000000004C4D/exefs/main"
Upload-File "$sdout\atmosphere\contents\0100000000004C4D\exefs\main.npdm" "/atmosphere/contents/0100000000004C4D/exefs/main.npdm"

# Wiki RAG Databases
Upload-File "$sdout\switch\SwitchLM\wikis\0100F2C0115B6000.idx" "/switch/SwitchLM/wikis/0100F2C0115B6000.idx"
Upload-File "$sdout\switch\SwitchLM\wikis\010021C000B6A000.idx" "/switch/SwitchLM/wikis/010021C000B6A000.idx"
Upload-File "$sdout\switch\SwitchLM\wikis\global_switch.idx" "/switch/SwitchLM/wikis/global_switch.idx"

# GGUF Models
Upload-File "$sdout\switch\SwitchLM\models\Qwen2.5-0.5B-Instruct-Q4_K_M.gguf" "/switch/SwitchLM/models/Qwen2.5-0.5B-Instruct-Q4_K_M.gguf"

Write-Output "=== Deployment Complete ==="
