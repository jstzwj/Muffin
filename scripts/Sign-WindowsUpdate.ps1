param(
    [Parameter(Mandatory = $true)][string]$Installer,
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][ValidateSet('x64', 'arm64')][string]$Architecture,
    [string]$PublicKey = "$PSScriptRoot/../resources/update-public-key.txt"
)

$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
    throw 'Expected a stable X.Y.Z version.'
}
$installerPath = (Resolve-Path -LiteralPath $Installer).Path
$expectedName = "Muffin-Setup-$Version-windows-$Architecture.exe"
if ([IO.Path]::GetFileName($installerPath) -cne $expectedName) {
    throw "Installer must be named $expectedName"
}
if (-not $env:MUFFIN_UPDATE_SIGNING_KEY) {
    throw 'MUFFIN_UPDATE_SIGNING_KEY is required. Never publish an unsigned in-app update.'
}
$key = [Security.Cryptography.CngKey]::Import(
    [Convert]::FromBase64String($env:MUFFIN_UPDATE_SIGNING_KEY),
    [Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob)
$signer = [Security.Cryptography.ECDsaCng]::new($key)
try {
    $actualPublic = ([BitConverter]::ToString($key.Export([Security.Cryptography.CngKeyBlobFormat]::EccPublicBlob))).Replace('-', '').ToLowerInvariant()
    if ($actualPublic -cne ([IO.File]::ReadAllText((Resolve-Path -LiteralPath $PublicKey).Path).Trim())) {
        throw 'Signing key does not match the public key embedded in Muffin.'
    }
    $payload = [ordered]@{
        schema = 1
        version = $Version
        architecture = $Architecture
        fileName = $expectedName
        size = (Get-Item -LiteralPath $installerPath).Length
        sha256 = (Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash.ToLowerInvariant()
    } | ConvertTo-Json -Compress
    $bytes = [Text.Encoding]::UTF8.GetBytes($payload)
    $signature = $signer.SignData($bytes, [Security.Cryptography.HashAlgorithmName]::SHA256)
    $envelope = [ordered]@{
        payload = [Convert]::ToBase64String($bytes)
        signature = [Convert]::ToBase64String($signature)
    } | ConvertTo-Json -Compress
    $manifestPath = [IO.Path]::ChangeExtension($installerPath, '.update.json')
    [IO.File]::WriteAllText($manifestPath, $envelope, [Text.UTF8Encoding]::new($false))
    Write-Output "Signed update manifest: $manifestPath"
} finally {
    $signer.Dispose()
    $key.Dispose()
}
