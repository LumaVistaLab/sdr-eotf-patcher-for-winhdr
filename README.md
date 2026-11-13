# SDR EOTF Patcher for Windows HDR

Language: English | [简体中文](README_zh-CN.md)

Select the decoding curve for SDR content on Windows HDR / Dolby Vision outputs: Gamma 2.2, Gamma 2.4, Rec.709, or the original sRGB path.

The patcher isolates processing by display rendering context and replaces only the SDR shaders used by the selected HDR output. Other SDR displays keep their original processing path. System DLL files, drivers, and ICC configurations remain unchanged.

Release notes: [English / 简体中文](RELEASE_NOTES.md).

## Compatibility

- Windows x64 with HDR / Dolby Vision enabled on the target display.
- The installed `dwmcore.dll` must exactly match the built-in isolation profile. A file digest or hook entry mismatch prevents activation.
- Up to 8 active outputs are supported. Each output must be uniquely identifiable by its display rectangle, including its desktop origin. Ambiguous ownership or conflicting contexts keep the original path.

The supported module version is `10.0.26100.9949`, with SHA256:

```text
A3C7441A4B53A302F836A3B9B04EFA343E320E2D0CFFCE730601F89689FB7B48
```

Check module compatibility again after Windows updates. Changing only the version number, digest, or offsets is insufficient to support another module.

## Usage

1. Extract the release ZIP (`sdr-eotf-patcher-<version>.zip`) and open its matching folder, or open `dev/` for a local development build.
2. Keep `SdrEotfPatcher.exe`, its matching `SdrEotfAgent-*.dll`, and `agent-file.txt` in the same directory.
3. Run the executable and accept the administrator permission request, then use its notification-area icon.
4. Select a curve and the target HDR display from the menu.
5. Choose “退出并恢复” (Exit and restore) to quit.

The default curve is Gamma 2.2. The initial target is the first HDR output in Windows display-device number order. Selections apply to the current run only. SDR displays are labeled “SDR 屏幕不适用” (Not applicable to SDR displays) and cannot be selected; HDR displays are labeled “仅处理 SDR 内容” (SDR content only).

| Menu option | SDR decoding | Tray label |
| --- | --- | --- |
| Gamma 2.2 | `L = V^2.2` | `2.2` |
| Gamma 2.4 | `L = V^2.4` | `2.4` |
| Rec.709 | Inverse of the piecewise BT.709 transfer function | `709` |
| sRGB | Original Windows decoding path | `sRGB` |

Here, `V` is the normalized SDR input and `L` is the linear value. Rec.709 uses `L = V / 4.5` when `V < 0.081`; otherwise, it uses `L = ((V + 0.099) / 1.099)^(1 / 0.45)`. Curve selection does not change the color gamut, white point, or video range.

The tray icon shows the active curve in green after an actual shader hit. While an HDR output is available, the original path and waiting states use the gray `sRGB` icon. When all active outputs are in SDR mode, a separate `SDR` icon uses the same gray color scheme. Hover over the icon for the current state or an error. “状态与说明” (Status and information) shows the current display list and feature descriptions in a native Windows message box, using system sizing rules and the information sound.

Switching between Gamma 2.2, Gamma 2.4, and Rec.709 on the same target preserves the verified output mapping. The previous curve stays available while the new shader variant is prepared, and the tray changes directly to the new mode after a confirmed hit. State-change events update the tray without waiting for periodic polling; a hit during composition refresh is reported in the same worker iteration.

Curve changes request a desktop composition refresh. When restoring sRGB or exiting, the application attempts to hold the last HDR picture over each previously converted output, restore the original hooks underneath it, compose once under the cover, then uncover the whole output together. Video may briefly appear frozen during this transition. A small amount of the live picture remains visible through the cover to keep underlying windows participating in composition; this can produce a slight blend during the hold. The image stays in local GPU memory and uses a static FP16 scRGB DirectComposition surface; no image file is saved. Covers opt out of Shell fullscreen detection and do not change focus or display modes.

The cover requires an unrotated output and an updated, matching FP16 desktop image; pointer-only startup frames are not accepted. All pixels must be copied and GPU completion confirmed before showing the cover. Protected, inaccessible, unsupported or mismatched captures use the ordinary composition-refresh fallback, which can update regions at different times. The cover is removed on recovery errors, display changes or a one-second wait timeout. Offline checks verify transition ordering, copy bounds and failure cleanup; actual HDR color fidelity, window presentation and the visible transition still require display validation.

The application sends no automatic notifications and writes no persistent runtime logs. Opening the information box again replaces the existing one. While open, it refreshes after display configuration settles; closing it manually prevents display changes from reopening it.

## Display Changes and Recovery

Connecting or disconnecting a display, or changing its mode, pauses conversion until the display topology settles and output ownership is checked again. When the target is temporarily unavailable, the selected curve is retained while waiting for that target to return. Display names come from EDID when available, with the PnP friendly name as a fallback.

Switching to sRGB or exiting normally restores the original shader path and hook entry bytes without restarting DWM. If restoration has not been confirmed, the application remains running so “退出并恢复” (Exit and restore) can be tried again.

When a cover is available, it is prepared and shown before disabling conversion and is removed only after hook restoration has been confirmed. Without an active cover, the worker requests three spaced composition refreshes before reporting sRGB or exiting. Selecting sRGB again also requests the fallback refresh. These requests do not constitute a frame-level presentation acknowledgement from DWM.

An independent guardian process handles recovery. The helper inside DWM uses a six-second heartbeat timeout to disable conversion and restore entry bytes if heartbeats stop. The inactive helper DLL and trampoline allocations remain until DWM exits to avoid unloading code that a thread may still reference.

`Restore-Display.ps1` reapplies the display configuration saved by Windows. Run it manually in PowerShell when needed:

```powershell
.\Restore-Display.ps1
```

The application exit flow and helper handle restoration of the patch hooks.

## Processing Scope

The helper uses the display context to decide whether to replace a shader. Curve variants are allowed on the selected HDR output only after all active outputs have been mapped. Non-target outputs, SDR outputs, and unknown or conflicting contexts return the original shader.

Conversion applies only to complete sRGB decoding instruction groups matched in the executable bytecode of linked DXBC shaders. Original bytecode and shared shader libraries remain unchanged. Each curve variant is owned by the original shader and released with it. Native HDR/PQ/HLG content receives no whole-frame Gamma or LUT post-processing.

Exclusive fullscreen, direct MPO presentation, and applications that output scRGB themselves may bypass this stage and therefore receive no conversion.

## Command Line

The following commands control an existing tray instance and require the same privilege level as that instance.

| Argument | Action |
| --- | --- |
| `--gamma22` | Switch to Gamma 2.2 |
| `--gamma24` | Switch to Gamma 2.4 |
| `--bt709` | Switch to Rec.709 |
| `--srgb` | Restore the original sRGB path |
| `--exit` | Exit and restore |
| `--status REPORT` | Write the current state to a file as `status=number` |

The exit code of `--status` is the state value: `0` initializing, `1` working, `2` applied, `3` original path, `4` waiting for the HDR target, `5` failed, `6` stopped, `7` ambiguous output ownership, or `8` waiting for stable topology. If no instance is running, it returns `2` without creating a report. Read the file generated by the current invocation.

`--srgb-start` starts the tray on the original path. `--inspect REPORT` reads only the system module and display configuration and writes the result to the specified file. It requires no administrator permission and does not load the helper.

Live diagnostics require administrator permission. Exit the tray instance before running them:

| Argument | Action |
| --- | --- |
| `--diagnose-isolation REPORT` | Install isolation hooks with shader replacement disabled and observe context ownership |
| `--verify-live REPORT` | Briefly enable Gamma 2.2 and check output isolation and restoration |
| `--verify-gamma24 REPORT` | Briefly enable Gamma 2.4 and check output isolation and restoration |
| `--verify-bt709 REPORT` | Briefly enable Rec.709 and check output isolation and restoration |
| `--verify-watchdog REPORT` | Briefly enable Gamma 2.2, stop heartbeats, and check automatic restoration |

These commands install hooks in the current DWM, restore entry bytes when finished, and write a report to the specified path. `REPORT` is an output file path whose parent directory must already exist.

## Build and Checks

Build requirements are the Visual Studio C++ x64 toolchain, Windows SDK, and PowerShell. The build script checks for Visual Studio 2019 Community first, then uses `vswhere` to find an available C++ toolchain.

From the repository root, run:

```powershell
.\build.ps1
```

To compile and run all checks:

```powershell
.\build.ps1 -Test
```

The eight check programs cover read-only shader enumeration and rejection of damaged input, WARP GPU readback for all four curves, simulated display isolation, actual x64 wrappers and in-process hook restoration, tray-text centering, the native information-box lifecycle, tray recovery after display reconnection or failed shell updates, state-change notifications without polling, same-iteration publication of composition-confirmed curves, bounded composition refreshes, and restoration-cover ordering, capture validation and exception cleanup. The WARP debug-device check requires the Direct3D debug layer. These checks do not install hooks in the system DWM.

For a build with read-only module checks and CPU-only transition tests, use `.\build.ps1 -OfflineTest`. It compiles all checks but runs only `core_tests` and `restoration_transition_tests`, without opening test windows, capturing a display, creating a rendering device, or injecting DWM.

Every successful build synchronizes the latest executable, matching helper, and support files to `dev/` for local testing. Exit the existing development instance before building. Old helper DLLs are retained because DWM may still have them loaded. `dev/RELEASE_NOTES.md` is an exact copy of the repository-root file and describes current functionality without version-change history. Development builds create no ZIP archive and do not write to `dist/`.

Refresh `dev/` after every repository change before handing it back for testing. Rebuild changes to program source or build inputs; for documentation or support-file changes only, synchronize the corresponding files and verify that the development output is current. Both the root and development `RELEASE_NOTES.md` must start with the applicable target binary's filename, file version, architecture, and SHA-256, before the functionality descriptions.

`build/package/` is clean staging for the current build. A fixed manifest includes the executable, one matching helper DLL, the filename record, the recovery script, both English and Simplified Chinese documentation, release notes, and license notices. Test executables, diagnostic reports, symbol caches, and linker output are excluded from releases.

Release timing and version numbers are chosen by the repository owner. Only when a release is requested, prepare a UTF-8 Markdown file describing that version's actual changes, then run the following command with the chosen version (the version below is an example):

```powershell
.\release.ps1 -Version v1.0-stable -ChangesPath .\build\release-changes.md -Test
```

The release script makes a fresh build. At the beginning of the release copy of `RELEASE_NOTES.md`, it declares the release binary version and the applicable target binary's filename, file version, architecture, and SHA-256, followed by the supplied version changes. The target binary declaration comes from the current build's notes; missing version or digest information prevents packaging. The script creates:

```text
dist/
  sdr-eotf-patcher-v1.0-stable/
  sdr-eotf-patcher-v1.0-stable.zip
  sdr-eotf-patcher-v1.0-stable.zip.sha256
```

The ZIP contains the complete named folder. Both README titles are `SDR EOTF Patcher for Windows HDR`. The checksum file records the ZIP's SHA-256 digest and filename. Existing releases are retained; an existing version's folder, ZIP, or checksum prevents reuse of that version. Before the first owner-requested release, `dist/` stays empty; development files and development archives do not belong there. The root and development release notes stay unchanged by release packaging. Release inputs come exclusively from clean staging, never from `dev/` or historical `dist/` contents.

Run `.\tests\packaging_tests.ps1` to check the release workflow in an isolated fixture under `build/`; it does not create a real release in the repository's `dist/`.

After building, a read-only inspection can be run with:

```powershell
.\dev\SdrEotfPatcher.exe --inspect "$PWD\build\inspection.txt"
```

## Repository Layout

| Directory or file | Contents |
| --- | --- |
| `README.md` | English project documentation |
| `README_zh-CN.md` | Simplified Chinese project documentation |
| `RELEASE_NOTES.md` | Current bilingual functionality notes; no version-change history |
| `LICENSE` | Full GNU GPL v3 license text |
| `build.ps1` | Build, checks, and development-output synchronization |
| `release.ps1` | Explicitly versioned release packaging with supplied changes |
| `AGENTS.md` | Repository development and release rules |
| `Restore-Display.ps1` | Reapply the display configuration saved by Windows |
| `.gitignore` | Version-control exclusions for build output and caches |
| `src/main.cpp`, `src/info_window.h`, `src/tray_icon.cpp` | Tray interface, native information box, and icons |
| `src/core.h`, `src/core.cpp`, `src/dxbc_checksum.cpp` | Common types, system-module reading, shader enumeration, and DXBC checksums |
| `src/isolation.h`, `src/isolation_host.cpp` | Isolation protocol, display enumeration, guardian, and helper management |
| `src/display_refresh.h`, `src/display_refresh.cpp` | Bounded full-desktop composition refresh after curve changes and restoration |
| `src/restoration_cover.h`, `src/restoration_cover.cpp`, `src/restoration_transition.cpp` | Public-API HDR image hold during restoration, validation and transition ordering |
| `src/isolation_agent.cpp`, `src/isolation_thunks.asm` | In-DWM context identification, shader variants, and hook wrappers |
| `src/isolation_policy.cpp`, `src/isolation_status.h`, `src/isolation_profile.h` | Output matching, curve conversion rules, confirmed-state publication, and the exact-module profile |
| `src/app.manifest` | Application privilege and Windows compatibility declarations |
| `src/third_party/` | Third-party source, copyright, and license notices |
| `tests/` | Standalone check programs |
| `tools/` | Shared package manifest, public-symbol download, symbol enumeration, on-disk disassembly, and profile-generation tools |
| `build/` | Intermediate files, caches, and package staging; excluded from version control |
| `dev/` | Unpacked development builds; excluded from version control |
| `dist/` | Historical release folders, ZIP archives, and SHA-256 files; excluded from version control |

`tools/package_manifest.ps1` supplies the fixed file list for development and release builds. The other tools are for research and are not part of a normal build. Run research tools from the repository root; they use `build/` for intermediate data. The disassembly script also requires Python, Capstone, and pefile. The profile-generation script accepts only the on-disk module matching its built-in digest. Supporting another version requires analysis of its entry points, arguments, and memory layout.

## License and Third-Party Components

This project is licensed under the GNU General Public License v3 (GNU GPL v3). See `LICENSE` for the full text. Third-party copyright and license notices are included with the source and program package:

| Component | Purpose | Notice file |
| --- | --- | --- |
| dwm_eotf_rs | Reference for sRGB shader identifiers and constants | `dwm_eotf_rs-LICENSE` |
| GPUOpen ShaderUtils DXBCChecksum | DXBC checksums; derived from the RSA Data Security, Inc. MD5 Message Digest Algorithm | `THIRD_PARTY_NOTICES` |
| MinHook / Hacker Disassembler Engine | Runtime function hooks and instruction decoding | `MinHook-LICENSE.txt` |

Source-tree notices are under `src/third_party/`. The original MinHook license file is `src/third_party/minhook/LICENSE.txt`.
