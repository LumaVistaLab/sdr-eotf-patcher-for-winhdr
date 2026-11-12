# SDR EOTF Patcher for Windows HDR

Language: English | [简体中文](README_zh-CN.md)

Select the decoding curve for SDR content on Windows HDR / Dolby Vision outputs: Gamma 2.2, Gamma 2.4, Rec.709, or the original sRGB path.

The patcher isolates processing by display rendering context and replaces only the SDR shaders used by the selected HDR output. Other SDR displays keep their original processing path. System DLL files, drivers, and ICC configurations remain unchanged.

Release notes: [English / 简体中文](RELEASE_NOTES.md).

## Compatibility

- Windows x64 with HDR / Dolby Vision enabled on the target display.
- The installed `dwmcore.dll` must exactly match the built-in isolation profile. A file digest or hook entry mismatch prevents activation.
- Up to 8 active outputs are supported. Each output must be uniquely identifiable by its display rectangle, including its desktop origin. Ambiguous ownership or conflicting contexts keep the original path.

The supported module version is `10.0.26100.8875`, with SHA256:

```text
825726F26BD6B5A02B49F6145963B54D537D3D1239055DDA28D1FB0B3E864EF3
```

Check module compatibility again after Windows updates. Changing only the version number, digest, or offsets is insufficient to support another module.

## Usage

1. Extract `SdrEotfPatcher-portable.zip`.
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

The tray icon shows the active curve in green after an actual shader hit. The original path and waiting states use a gray `sRGB` icon. Hover over the icon for the current state or an error. “状态与说明” (Status and information) shows the current display list and feature descriptions in a native Windows message box, using system sizing rules and the information sound.

The application sends no automatic notifications and writes no persistent runtime logs. Opening the information box again replaces the existing one. While open, it refreshes after display configuration settles; closing it manually prevents display changes from reopening it.

## Display Changes and Recovery

Connecting or disconnecting a display, or changing its mode, pauses conversion until the display topology settles and output ownership is checked again. When the target is temporarily unavailable, the selected curve is retained while waiting for that target to return. Display names come from EDID when available, with the PnP friendly name as a fallback.

Switching to sRGB or exiting normally restores the original shader path and hook entry bytes without restarting DWM. If restoration has not been confirmed, the application remains running so “退出并恢复” (Exit and restore) can be tried again.

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

The five check programs cover read-only shader enumeration and rejection of damaged input, WARP GPU readback for all four curves, simulated display isolation, actual x64 wrappers and in-process hook restoration, tray-text centering, and the native information-box lifecycle. The WARP debug-device check requires the Direct3D debug layer. These checks do not install hooks in the system DWM.

Unpacked build output is placed in `build/package/`. Delivery files are `dist/SdrEotfPatcher-portable.zip` and its adjacent `.sha256` file. A fixed manifest packages the executable, one matching helper DLL, the filename record, the recovery script, both English and Simplified Chinese documentation, bilingual release notes, and license notices. Test executables, diagnostic reports, symbol caches, and linker output are excluded.

After building, a read-only inspection can be run with:

```powershell
.\build\package\SdrEotfPatcher.exe --inspect "$PWD\build\inspection.txt"
```

## Repository Layout

| Directory or file | Contents |
| --- | --- |
| `README.md` | English project documentation |
| `README_zh-CN.md` | Simplified Chinese project documentation |
| `RELEASE_NOTES.md` | Bilingual release notes |
| `LICENSE` | Full GNU GPL v3 license text |
| `build.ps1` | Build, checks, and packaging entry point |
| `Restore-Display.ps1` | Reapply the display configuration saved by Windows |
| `.gitignore` | Version-control exclusions for build output and caches |
| `src/main.cpp`, `src/info_window.h`, `src/tray_icon.cpp` | Tray interface, native information box, and icons |
| `src/core.h`, `src/core.cpp`, `src/dxbc_checksum.cpp` | Common types, system-module reading, shader enumeration, and DXBC checksums |
| `src/isolation.h`, `src/isolation_host.cpp` | Isolation protocol, display enumeration, guardian, and helper management |
| `src/isolation_agent.cpp`, `src/isolation_thunks.asm` | In-DWM context identification, shader variants, and hook wrappers |
| `src/isolation_policy.cpp`, `src/isolation_profile.h` | Output matching, curve conversion rules, and the exact-module profile |
| `src/app.manifest` | Application privilege and Windows compatibility declarations |
| `src/third_party/` | Third-party source, copyright, and license notices |
| `tests/` | Standalone check programs |
| `tools/` | Public-symbol download, symbol enumeration, on-disk disassembly, and profile-generation tools |
| `build/` | Intermediate files, caches, and package staging; excluded from version control |
| `dist/` | Delivery archive and SHA256 checksum file; excluded from version control |

`tools/` is not part of a normal build. Run research tools from the repository root; they use `build/` for intermediate data. The disassembly script also requires Python, Capstone, and pefile. The profile-generation script accepts only the on-disk module matching its built-in digest. Supporting another version requires analysis of its entry points, arguments, and memory layout.

## License and Third-Party Components

This project is licensed under the GNU General Public License v3 (GNU GPL v3). See `LICENSE` for the full text. Third-party copyright and license notices are included with the source and program package:

| Component | Purpose | Notice file |
| --- | --- | --- |
| dwm_eotf_rs | Reference for sRGB shader identifiers and constants | `dwm_eotf_rs-LICENSE` |
| GPUOpen ShaderUtils DXBCChecksum | DXBC checksums; derived from the RSA Data Security, Inc. MD5 Message Digest Algorithm | `THIRD_PARTY_NOTICES` |
| MinHook / Hacker Disassembler Engine | Runtime function hooks and instruction decoding | `MinHook-LICENSE.txt` |

Source-tree notices are under `src/third_party/`. The original MinHook license file is `src/third_party/minhook/LICENSE.txt`.
