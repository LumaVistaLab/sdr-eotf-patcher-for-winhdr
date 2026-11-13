# Shared manifest: never package a local installation, diagnostics, symbols or old agents.
function Get-PackageFileNames {
    param([Parameter(Mandatory = $true)][string]$PackageDirectory)

    $agentFile = (Get-Content -LiteralPath (Join-Path $PackageDirectory 'agent-file.txt') -Raw).Trim()
    if ($agentFile -notmatch '\ASdrEotfAgent-[0-9]{8}-[0-9]{8}\.dll\z') { throw 'Invalid matching helper filename.' }
    $files = @('SdrEotfPatcher.exe', $agentFile, 'agent-file.txt', 'README.md', 'README_zh-CN.md', 'RELEASE_NOTES.md', 'LICENSE', 'Restore-Display.ps1', 'THIRD_PARTY_NOTICES', 'dwm_eotf_rs-LICENSE', 'MinHook-LICENSE.txt')
    foreach ($file in $files) {
        $path = Join-Path $PackageDirectory $file
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing package file: $file" }
        if ((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Package file cannot be a link: $file" }
    }
    return $files
}
