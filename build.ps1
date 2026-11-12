param([switch]$Test)
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
$vc = 'C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vc)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vs) { $vc = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat' }
    }
}
if (-not (Test-Path -LiteralPath $vc)) { throw 'Install Visual Studio C++ tools and the Windows SDK.' }
New-Item -ItemType Directory -Path build,build\generated,dist -Force | Out-Null
# This staging directory contains generated delivery files only. Never replace a running installation.
$package = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'build\package'))
$buildRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'build')) + [IO.Path]::DirectorySeparatorChar
if (-not $package.StartsWith($buildRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Package staging must stay inside build.' }
if (Test-Path -LiteralPath $package) {
    if ((Get-Item -LiteralPath $package).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Package staging cannot be a link.' }
    Remove-Item -LiteralPath $package -Recurse -Force
}
New-Item -ItemType Directory -Path $package | Out-Null
$agentTag = Get-Date -Format 'yyyyMMdd-HHmmssff'
$agentFile = "SdrEotfAgent-$agentTag.dll"
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'build\generated\agent_filename.h'), ('#pragma once' + "`r`n" + 'inline constexpr const wchar_t* AgentFileName = L"' + $agentFile + '";' + "`r`n"))
$compile = '@echo off' + "`r`ncall `"$vc`" >nul`r`nif errorlevel 1 exit /b 1`r`n" + @'
set "CXX=cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /Ibuild\generated"
set "CORE_OBJECTS=build\core.obj build\checksum.obj build\dxbc_checksum.obj build\isolation_policy.obj build\isolation_host.obj"
set "HOOK_OBJECTS=build\isolation_thunks.obj build\buffer.obj build\hook.obj build\trampoline.obj build\hde64.obj"
%CXX% /c src\core.cpp src\dxbc_checksum.cpp src\isolation_policy.cpp src\isolation_host.cpp src\tray_icon.cpp /Fobuild\
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /O2 /MT /W3 /c src\third_party\DXBCChecksum.c /Fobuild\checksum.obj
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /W3 /c src\third_party\minhook\src\buffer.c src\third_party\minhook\src\hook.c src\third_party\minhook\src\trampoline.c src\third_party\minhook\src\hde\hde64.c /Fobuild\
if errorlevel 1 exit /b 1
ml64 /nologo /c /Fobuild\isolation_thunks.obj src\isolation_thunks.asm
if errorlevel 1 exit /b 1
%CXX% /c src\isolation_agent.cpp /Fobuild\isolation_agent.obj
if errorlevel 1 exit /b 1
link /nologo /DLL /OUT:build\package\AGENT_FILE /IMPLIB:build\isolation_agent.lib build\isolation_agent.obj build\isolation_policy.obj build\dxbc_checksum.obj build\checksum.obj %HOOK_OBJECTS% user32.lib d3d11.lib
if errorlevel 1 exit /b 1
%CXX% src\main.cpp build\tray_icon.obj %CORE_OBJECTS% /Febuild\package\SdrEotfPatcher.exe /Fobuild\main.obj /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest advapi32.lib user32.lib gdi32.lib shell32.lib bcrypt.lib
if errorlevel 1 exit /b 1
if /i not "%~1"=="test" exit /b 0
%CXX% /DEOTF_AGENT_TEST /c src\isolation_agent.cpp /Fobuild\isolation_agent_test.obj
if errorlevel 1 exit /b 1
link /nologo /DLL /OUT:build\SdrEotfAgentTest.dll build\isolation_agent_test.obj build\isolation_policy.obj build\dxbc_checksum.obj build\checksum.obj %HOOK_OBJECTS% user32.lib d3d11.lib d3dcompiler.lib
if errorlevel 1 exit /b 1
%CXX% tests\core_tests.cpp %CORE_OBJECTS% /Febuild\core_tests.exe /Fobuild\tests.obj /link advapi32.lib user32.lib bcrypt.lib d3dcompiler.lib
if errorlevel 1 exit /b 1
%CXX% tests\isolation_tests.cpp %CORE_OBJECTS% /Febuild\isolation_tests.exe /Fobuild\isolation_tests.obj /link advapi32.lib user32.lib bcrypt.lib d3d11.lib d3dcompiler.lib
if errorlevel 1 exit /b 1
%CXX% tests\agent_selftest.cpp %CORE_OBJECTS% /Febuild\agent_selftest.exe /Fobuild\agent_selftest.obj /link advapi32.lib user32.lib bcrypt.lib
if errorlevel 1 exit /b 1
%CXX% tests\info_window_tests.cpp build\tray_icon.obj %CORE_OBJECTS% /Febuild\info_window_tests.exe /Fobuild\info_window_tests.obj /link advapi32.lib user32.lib gdi32.lib shell32.lib bcrypt.lib
if errorlevel 1 exit /b 1
%CXX% tests\tray_preview.cpp build\tray_icon.obj %CORE_OBJECTS% /Febuild\tray_preview.exe /Fobuild\tray_preview.obj /link advapi32.lib user32.lib gdi32.lib bcrypt.lib
exit /b %errorlevel%
'@
$compile = $compile.Replace('AGENT_FILE', $agentFile)
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'build\compile.cmd'), $compile, [Text.Encoding]::Default)
$buildMode = if ($Test) { 'test' } else { 'release' }
& $env:ComSpec /d /c build\compile.cmd $buildMode
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)." }
if ($Test) {
    foreach ($testName in @('core_tests', 'isolation_tests', 'agent_selftest', 'tray_preview', 'info_window_tests')) {
        & (Join-Path $PSScriptRoot "build\$testName.exe")
        if ($LASTEXITCODE -ne 0) { throw "$testName failed ($LASTEXITCODE)." }
    }
}
[IO.File]::WriteAllText((Join-Path $package 'agent-file.txt'), $agentFile + "`r`n", [Text.Encoding]::ASCII)
Copy-Item -LiteralPath README.md,README_zh-CN.md,RELEASE_NOTES.md,LICENSE,Restore-Display.ps1 -Destination $package -Force
Copy-Item -LiteralPath src\third_party\THIRD_PARTY_NOTICES,src\third_party\dwm_eotf_rs-LICENSE -Destination $package -Force
Copy-Item -LiteralPath src\third_party\minhook\LICENSE.txt -Destination (Join-Path $package 'MinHook-LICENSE.txt') -Force
# Package an explicit manifest so diagnostics, tests, symbols and old agents cannot enter the archive.
$packageFiles = @('SdrEotfPatcher.exe', $agentFile, 'agent-file.txt', 'README.md', 'README_zh-CN.md', 'RELEASE_NOTES.md', 'LICENSE', 'Restore-Display.ps1', 'THIRD_PARTY_NOTICES', 'dwm_eotf_rs-LICENSE', 'MinHook-LICENSE.txt')
$packagePaths = @($packageFiles | ForEach-Object { Join-Path $package $_ })
$archive = Join-Path $PSScriptRoot 'dist\SdrEotfPatcher-portable.zip'
$stagedArchive = Join-Path $PSScriptRoot 'build\SdrEotfPatcher-portable.zip'
Compress-Archive -LiteralPath $packagePaths -DestinationPath $stagedArchive -Force
Move-Item -LiteralPath $stagedArchive -Destination $archive -Force
$digest = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText($archive + '.sha256', $digest + '  SdrEotfPatcher-portable.zip' + "`r`n", [Text.Encoding]::ASCII)
Write-Output "Package: $archive"
Write-Output "Unpacked files: $package"
