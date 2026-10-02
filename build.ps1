param(
    [ValidateSet('Host', 'PS5')][string]$Target = 'Host',
    [string]$Sdk = ''
)
$ErrorActionPreference = 'Stop'
$taskWorkspace = Split-Path -Parent $PSScriptRoot
$taskMount = ($taskWorkspace -replace '\\', '/') + ':/ws'
if ($Target -eq 'PS5') {
    if (-not $Sdk) {
        $Sdk = Join-Path $taskWorkspace 'Castation/native-ps5/.deps/native/ps5-payload-sdk'
    }
    $taskSdkResolved = (Resolve-Path -LiteralPath $Sdk).Path
    $taskWorkspacePrefix = $taskWorkspace.TrimEnd('\') + '\'
    if (-not $taskSdkResolved.StartsWith($taskWorkspacePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The SDK must be inside the mounted workspace.'
    }
    $taskSdkContainer = '/ws/' + ($taskSdkResolved.Substring($taskWorkspacePrefix.Length) -replace '\\', '/')
    & docker run --rm -v $taskMount -w /ws/Xbox360PS5 -e "PS5_PAYLOAD_SDK=$taskSdkContainer" castation-buildenv bash tools/build.sh ps5
} else {
    & docker run --rm -v $taskMount -w /ws/Xbox360PS5 castation-buildenv bash tools/build.sh host
}
if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
