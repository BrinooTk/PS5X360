param(
    [ValidateSet('Host', 'PS5', 'Native', 'NativeCPU', 'NativeRuntime', 'RuntimeHost', 'RuntimePS5', 'VulkanHost', 'VulkanPS5', 'IntegrationHost', 'IntegrationPS5', 'NativeGame', 'RADV')][string]$Target = 'Host',
    [string]$Sdk = ''
)
$ErrorActionPreference = 'Stop'
$taskWorkspace = Split-Path -Parent $PSScriptRoot
$taskMount = ($taskWorkspace -replace '\\', '/') + ':/ws'
if ($Target -notin @('Host', 'RuntimeHost', 'VulkanHost', 'IntegrationHost', 'RADV')) {
    if (-not $Sdk) {
        $Sdk = Join-Path $taskWorkspace 'Castation/native-ps5/.deps/native/ps5-payload-sdk'
    }
    $taskSdkResolved = (Resolve-Path -LiteralPath $Sdk).Path
    $taskWorkspacePrefix = $taskWorkspace.TrimEnd('\') + '\'
    if (-not $taskSdkResolved.StartsWith($taskWorkspacePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The SDK must be inside the mounted workspace.'
    }
    $taskSdkContainer = '/ws/' + ($taskSdkResolved.Substring($taskWorkspacePrefix.Length) -replace '\\', '/')
    if ($Target -eq 'NativeGame') {
        & docker run --rm -v $taskMount -w /ws/Xbox360PS5 -e "PS5_PAYLOAD_SDK=$taskSdkContainer" castation-buildenv bash tools/build-engine-ps5.sh
    } elseif ($Target -eq 'IntegrationPS5') {
        & docker run --rm -v $taskMount -w /ws/Xbox360PS5 -e "PS5_PAYLOAD_SDK=$taskSdkContainer" castation-buildenv bash tools/build-integration.sh ps5
    } elseif ($Target -eq 'NativeRuntime') {
        & docker run --rm -v $taskMount -w /ws/Xbox360PS5 -e "PS5_PAYLOAD_SDK=$taskSdkContainer" castation-buildenv bash tools/build-native-runtime-probe.sh
        if ($LASTEXITCODE -ne 0) { throw 'Native runtime build failed' }
        & docker run --rm -v $taskMount -w /ws/Xbox360PS5 castation-buildenv python3 tools/package-native-runtime.py
    } elseif ($Target -eq 'VulkanPS5') {
        & docker run --rm -v $taskMount -w /ws/Xbox360PS5 -e "PS5_PAYLOAD_SDK=$taskSdkContainer" castation-buildenv bash tools/build-vulkan-platform.sh ps5
    } elseif ($Target -eq 'RuntimePS5') {
        & docker run --rm -v $taskMount -w /ws/Xbox360PS5 -e "PS5_PAYLOAD_SDK=$taskSdkContainer" castation-buildenv bash tools/build-runtime.sh ps5
    } elseif ($Target -eq 'NativeCPU') {
        & docker run --rm -v $taskMount -w /ws/Xbox360PS5 -e "PS5_PAYLOAD_SDK=$taskSdkContainer" castation-buildenv bash tools/build-native-cpu-probe.sh
    } elseif ($Target -eq 'Native') {
        & docker run --rm -v $taskMount -w /ws/Xbox360PS5 -e "PS5_PAYLOAD_SDK=$taskSdkContainer" castation-buildenv bash tools/build-native-probe.sh
    } else {
        & docker run --rm -v $taskMount -w /ws/Xbox360PS5 -e "PS5_PAYLOAD_SDK=$taskSdkContainer" castation-buildenv bash tools/build.sh ps5
    }
} elseif ($Target -eq 'IntegrationHost') {
    & docker build -t xbox360ps5-vulkan-buildenv -f (Join-Path $PSScriptRoot 'tooling/docker/Dockerfile.vulkan') $PSScriptRoot
    if ($LASTEXITCODE -ne 0) { throw 'Vulkan environment build failed' }
    & docker build -t xbox360ps5-xenos-buildenv -f (Join-Path $PSScriptRoot 'tooling/docker/Dockerfile.xenos') $PSScriptRoot
    if ($LASTEXITCODE -ne 0) { throw 'Xenos environment build failed' }
    & docker run --rm -v $taskMount -w /ws/Xbox360PS5 xbox360ps5-xenos-buildenv bash tools/build-integration.sh host
} elseif ($Target -eq 'RADV') {
    & docker build -t xbox360ps5-radv-buildenv -f (Join-Path $PSScriptRoot 'tooling/docker/Dockerfile.radv') $PSScriptRoot
    if ($LASTEXITCODE -ne 0) { throw 'RADV environment build failed' }
    & docker run --rm -v $taskMount -v 'xbox360ps5-radv-work:/ws/Xbox360PS5/.deps/references/PS5_Vulkan/.deps/work' -w /ws/Xbox360PS5 xbox360ps5-radv-buildenv bash tools/build-radv-platform.sh
} elseif ($Target -eq 'VulkanHost') {
    & docker build -t xbox360ps5-vulkan-buildenv -f (Join-Path $PSScriptRoot 'tooling/docker/Dockerfile.vulkan') $PSScriptRoot
    if ($LASTEXITCODE -ne 0) { throw 'Vulkan host environment build failed' }
    & docker run --rm -v $taskMount -w /ws/Xbox360PS5 xbox360ps5-vulkan-buildenv bash tools/build-vulkan-platform.sh host
} elseif ($Target -eq 'RuntimeHost') {
    & docker run --rm -v $taskMount -w /ws/Xbox360PS5 castation-buildenv bash tools/build-runtime.sh host
} else {
    & docker run --rm -v $taskMount -w /ws/Xbox360PS5 castation-buildenv bash tools/build.sh host
}
if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
