param(
    [ValidateSet('Host', 'PS5', 'Native', 'NativeCPU', 'RuntimeHost', 'RuntimePS5')][string]$Target = 'Host',
    [string]$Sdk = ''
)
$ErrorActionPreference = 'Stop'
$taskWorkspace = Split-Path -Parent $PSScriptRoot
$taskMount = ($taskWorkspace -replace '\\', '/') + ':/ws'
if ($Target -notin @('Host', 'RuntimeHost')) {
    if (-not $Sdk) {
        $Sdk = Join-Path $taskWorkspace 'Castation/native-ps5/.deps/native/ps5-payload-sdk'
    }
    $taskSdkResolved = (Resolve-Path -LiteralPath $Sdk).Path
    $taskWorkspacePrefix = $taskWorkspace.TrimEnd('\') + '\'
    if (-not $taskSdkResolved.StartsWith($taskWorkspacePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The SDK must be inside the mounted workspace.'
    }
    $taskSdkContainer = '/ws/' + ($taskSdkResolved.Substring($taskWorkspacePrefix.Length) -replace '\\', '/')
    if ($Target -eq 'RuntimePS5') {
        & docker run --rm -v $taskMount -w /ws/Xbox360PS5 -e "PS5_PAYLOAD_SDK=$taskSdkContainer" castation-buildenv bash tools/build-runtime.sh ps5
    } elseif ($Target -eq 'NativeCPU') {
        & docker run --rm -v $taskMount -w /ws/Xbox360PS5 -e "PS5_PAYLOAD_SDK=$taskSdkContainer" castation-buildenv bash tools/build-native-cpu-probe.sh
    } elseif ($Target -eq 'Native') {
        & docker run --rm -v $taskMount -w /ws/Xbox360PS5 -e "PS5_PAYLOAD_SDK=$taskSdkContainer" castation-buildenv bash tools/build-native-probe.sh
    } else {
        & docker run --rm -v $taskMount -w /ws/Xbox360PS5 -e "PS5_PAYLOAD_SDK=$taskSdkContainer" castation-buildenv bash tools/build.sh ps5
    }
} elseif ($Target -eq 'RuntimeHost') {
    & docker run --rm -v $taskMount -w /ws/Xbox360PS5 castation-buildenv bash tools/build-runtime.sh host
} else {
    & docker run --rm -v $taskMount -w /ws/Xbox360PS5 castation-buildenv bash tools/build.sh host
}
if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
