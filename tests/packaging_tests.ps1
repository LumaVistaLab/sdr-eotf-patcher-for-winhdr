$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$originalLocation = Get-Location
$fixture = Join-Path $projectRoot ('build\packaging-tests-' + [Guid]::NewGuid().ToString('N'))
$encoding = New-Object Text.UTF8Encoding($false)
function Assert-Package([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Assert-Rejected([scriptblock]$Action, [string]$MessagePattern) {
    $rejected = $false
    try { & $Action | Out-Null } catch {
        $rejected = $true
        Assert-Package ($_.Exception.Message -match $MessagePattern) "Unexpected rejection: $($_.Exception.Message)"
    }
    Assert-Package $rejected 'An invalid release request was accepted.'
}
try {
    $package = Join-Path $fixture 'build\package'
    $dist = Join-Path $fixture 'dist'
    New-Item -ItemType Directory -Path $package,(Join-Path $fixture 'tools'),(Join-Path $fixture 'dev'),$dist -Force | Out-Null
    $releaseScript = Join-Path $fixture 'release.ps1'
    Copy-Item -LiteralPath (Join-Path $projectRoot 'release.ps1') -Destination $releaseScript
    Copy-Item -LiteralPath (Join-Path $projectRoot 'tools\package_manifest.ps1') -Destination (Join-Path $fixture 'tools')
    # No compilation or real publication: release.ps1 runs against synthetic files only.
    [IO.File]::WriteAllText((Join-Path $fixture 'build.ps1'), 'param([switch]$Test)', $encoding)
    $agent = 'SdrEotfAgent-20000101-00000000.dll'
    $expectedFiles = @('SdrEotfPatcher.exe', $agent, 'agent-file.txt', 'README.md', 'README_zh-CN.md', 'RELEASE_NOTES.md', 'LICENSE', 'Restore-Display.ps1', 'THIRD_PARTY_NOTICES', 'dwm_eotf_rs-LICENSE', 'MinHook-LICENSE.txt')
    foreach ($file in $expectedFiles) { [IO.File]::WriteAllText((Join-Path $package $file), "fixture: $file", $encoding) }
    [IO.File]::WriteAllText((Join-Path $package 'agent-file.txt'), $agent + "`r`n", $encoding)
    foreach ($readme in @('README.md', 'README_zh-CN.md')) {
        [IO.File]::WriteAllText((Join-Path $package $readme), "# Old title`r`n`r`nREADME body`r`n", $encoding)
    }
    $binaryDeclaration = "## Applicable binary / 适用的二进制`r`n`r`n" + 'Target binary / 目标二进制: `dwmcore.dll` `10.0.20000.1234` (Windows x64).' + "`r`n`r`nSHA-256: " + ('0123456789abcdef' * 4)
    $rootNotes = "# Release Notes / 发布说明`r`n`r`n$binaryDeclaration`r`n`r`n## Current functionality / 当前功能`r`n`r`nCurrent features.`r`n"
    foreach ($notesPath in @((Join-Path $package 'RELEASE_NOTES.md'),(Join-Path $fixture 'RELEASE_NOTES.md'),(Join-Path $fixture 'dev\RELEASE_NOTES.md'))) {
        [IO.File]::WriteAllText($notesPath, $rootNotes, $encoding)
    }
    foreach ($extra in @('diagnostics.txt', 'SdrEotfAgent-19990101-00000000.dll', 'test.exe')) {
        [IO.File]::WriteAllText((Join-Path $package $extra), 'Must not be shipped.', $encoding)
    }
    [IO.File]::WriteAllText((Join-Path $fixture 'dev\local-only.txt'), 'Must not be shipped.', $encoding)
    $historyDirectory = Join-Path $dist 'sdr-eotf-patcher-v0.1-history-test'
    New-Item -ItemType Directory -Path $historyDirectory | Out-Null
    $historyFiles = @((Join-Path $historyDirectory 'RELEASE_NOTES.md'), (Join-Path $dist 'legacy.zip'), (Join-Path $dist 'legacy.zip.sha256'))
    $historyHashes = @{}
    foreach ($file in $historyFiles) {
        [IO.File]::WriteAllText($file, 'Historical release; retain exactly.', $encoding)
        $historyHashes[$file] = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
    }
    $changesPath = Join-Path $fixture 'changes.md'
    $changes = '- Fix release folder packaging. / 修正发布目录打包。'
    [IO.File]::WriteAllText($changesPath, $changes, $encoding)
    $version = 'v0.2-package-test'
    $name = 'sdr-eotf-patcher-' + $version
    & $releaseScript -Version $version -ChangesPath $changesPath -Test | Out-Null
    $releaseDirectory = Join-Path $dist $name
    $archivePath = Join-Path $dist ($name + '.zip')
    $releaseNotes = [IO.File]::ReadAllText((Join-Path $releaseDirectory 'RELEASE_NOTES.md'))
    $expectedStart = "# Release Notes / 发布说明`r`n`r`nRelease binary version / 发布程序版本: $version`r`n`r`n$binaryDeclaration`r`n`r`n"
    Assert-Package ($releaseNotes.StartsWith($expectedStart, [StringComparison]::Ordinal)) 'Release notes must declare the release version and applicable target binary before any version changes.'
    Assert-Package ($releaseNotes.Contains("## $version") -and $releaseNotes.Contains('### Version changes / 版本变化') -and $releaseNotes.Contains($changes)) 'Release notes lost the supplied version or changes.'
    foreach ($notesPath in @((Join-Path $package 'RELEASE_NOTES.md'),(Join-Path $fixture 'RELEASE_NOTES.md'),(Join-Path $fixture 'dev\RELEASE_NOTES.md'))) {
        Assert-Package ([IO.File]::ReadAllText($notesPath) -ceq $rootNotes) 'Release packaging changed development or root notes.'
    }
    $actualFiles = @(Get-ChildItem -LiteralPath $releaseDirectory -File | Select-Object -ExpandProperty Name)
    Assert-Package (@(Compare-Object ($expectedFiles | Sort-Object) ($actualFiles | Sort-Object)).Count -eq 0) 'Release folder does not match the fixed manifest.'
    $digest = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    Assert-Package ([IO.File]::ReadAllText($archivePath + '.sha256').Trim() -ceq ($digest + '  ' + $name + '.zip')) 'SHA-256 file does not match the ZIP.'
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($archivePath)
    try {
        $entries = @($zip.Entries | Where-Object { $_.Name })
        Assert-Package ($entries.Count -eq $expectedFiles.Count) 'ZIP contains unexpected files.'
        foreach ($entry in $entries) {
            $entryPath = $entry.FullName.Replace('\', '/')
            Assert-Package ($entryPath -ceq ($name + '/' + $entry.Name)) 'ZIP lost the versioned top-level folder.'
            $reader = New-Object IO.StreamReader($entry.Open())
            try { $contents = $reader.ReadToEnd() } finally { $reader.Dispose() }
            Assert-Package ($contents -ceq [IO.File]::ReadAllText((Join-Path $releaseDirectory $entry.Name))) 'ZIP content differs from the unpacked release.'
        }
    } finally { $zip.Dispose() }
    foreach ($readme in @('README.md', 'README_zh-CN.md')) {
        Assert-Package ((Get-Content -LiteralPath (Join-Path $releaseDirectory $readme) -Encoding UTF8 -TotalCount 1) -ceq '# SDR EOTF Patcher for Windows HDR') 'Incorrect release README title.'
    }
    $publishedHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
    Assert-Rejected { & $releaseScript -Version $version -ChangesPath $changesPath } 'history will not be overwritten'
    Assert-Rejected { & $releaseScript -Version '../outside' -ChangesPath $changesPath } 'pattern'
    $emptyChanges = Join-Path $fixture 'empty.md'
    [IO.File]::WriteAllText($emptyChanges, " `r`n", $encoding)
    Assert-Rejected { & $releaseScript -Version v0.3-package-test -ChangesPath $emptyChanges } 'must not be empty'
    $orphanChecksum = Join-Path $dist 'sdr-eotf-patcher-v0.3-package-test.zip.sha256'
    [IO.File]::WriteAllText($orphanChecksum, 'Preserve this checksum too.', $encoding)
    Assert-Rejected { & $releaseScript -Version v0.3-package-test -ChangesPath $changesPath } 'history will not be overwritten'
    & $releaseScript -Version v0.4-package-test -ChangesPath $changesPath | Out-Null
    $nextNotes = [IO.File]::ReadAllText((Join-Path $dist 'sdr-eotf-patcher-v0.4-package-test\RELEASE_NOTES.md'))
    Assert-Package ($nextNotes.StartsWith("# Release Notes / 发布说明`r`n`r`nRelease binary version / 发布程序版本: v0.4-package-test`r`n", [StringComparison]::Ordinal)) 'A later release declared the wrong binary version.'
    Assert-Package ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash -ceq $publishedHash) 'Publishing another version changed the earlier ZIP.'
    foreach ($file in $historyFiles) {
        Assert-Package ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ceq $historyHashes[$file]) 'Historical release was changed.'
    }
    Assert-Package ([IO.File]::ReadAllText($orphanChecksum) -ceq 'Preserve this checksum too.') 'An existing checksum was overwritten.'
    $fixtureNotes = Join-Path $package 'RELEASE_NOTES.md'
    try {
        [IO.File]::WriteAllText($fixtureNotes, "# Release Notes`r`n`r`nNo binary declaration.", $encoding)
        Assert-Rejected { & $releaseScript -Version v0.5-package-test -ChangesPath $changesPath } 'applicable binary version'
        [IO.File]::WriteAllText($fixtureNotes, "# Release Notes`r`n`r`n## Applicable binary / 适用的二进制`r`n`r`ndwmcore.dll", $encoding)
        Assert-Rejected { & $releaseScript -Version v0.6-package-test -ChangesPath $changesPath } 'applicable binary version'
    } finally { [IO.File]::WriteAllText($fixtureNotes, $rootNotes, $encoding) }
    foreach ($invalidVersion in @('v0.5-package-test', 'v0.6-package-test')) {
        foreach ($suffix in @('', '.zip', '.zip.sha256')) {
            Assert-Package (-not (Test-Path -LiteralPath (Join-Path $dist ('sdr-eotf-patcher-' + $invalidVersion + $suffix)))) 'Invalid binary declarations produced release output.'
        }
    }
    Write-Output 'Packaging checks passed: binary declarations at the beginning, folder layout, manifest, README titles, release notes, SHA-256, history preservation, and invalid requests.'
    Write-Output "Isolated fixture: $fixture"
} finally {
    Set-Location -LiteralPath $originalLocation.Path
}
