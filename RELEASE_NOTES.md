# Release Notes / 发布说明

## Applicable binary / 适用的二进制

Target binary / 目标二进制: `dwmcore.dll` `10.0.26100.9949` (Windows x64).

SHA-256:

```text
A3C7441A4B53A302F836A3B9B04EFA343E320E2D0CFFCE730601F89689FB7B48
```

Only this exact binary is supported. The file digest and hook entry bytes must match the built-in isolation profile.

仅适用于上述精确二进制文件；文件摘要和挂钩入口字节必须与内置隔离配置匹配。

## Current functionality / 当前功能

This repository-root file and its exact copy in `dev/` describe the current build without version-change history. Published copies in `dist/` also include the owner-selected version and its actual changes.

仓库根目录文件及其在 `dev/` 中的完全相同副本仅描述当前构建，不记录版本变化。`dist/` 中的发布副本另包含所有者指定的版本号及该版本的实际变化。

### Features / 功能

- Isolate SDR shader conversion by display rendering context on the selected Windows HDR / Dolby Vision output. Other SDR displays keep their original path.

  按显示器渲染上下文隔离，仅转换所选 Windows HDR / 杜比视界输出中的 SDR 着色器，其他 SDR 显示器保持原有路径。

- Support Gamma 2.2, Gamma 2.4, Rec.709, and original sRGB decoding. Gamma 2.2 is the default; the HDR target can be selected from the tray menu.

  支持 Gamma 2.2、Gamma 2.4、Rec.709 和原始 sRGB 解码。默认使用 Gamma 2.2，可通过托盘菜单选择目标 HDR 显示器。

- Pause conversion during display changes, check ownership again after topology settles, and retain the selected curve while waiting for the target to return.

  显示配置变化时暂停转换，待拓扑稳定后重新核对归属；目标暂时不可用时保留所选曲线，等待该目标恢复。

- Show the active curve in the tray after an actual shader hit. Use a single native information box that refreshes for display changes. Automatic notifications and persistent runtime logs are disabled.

  曲线实际命中后在托盘显示对应模式。原生说明窗口保持单实例，并随显示配置变化刷新；关闭自动通知和常驻运行日志。

### Recovery and maintenance / 恢复与维护

- Preserve verified output ownership when switching between conversion curves. Keep the prior cached variant available during shader preparation and report the confirmed active curve throughout the switch, avoiding an intermediate sRGB path or tray icon.

  在转换曲线之间切换时保留已确认的输出归属；着色器准备期间继续使用已有变体，并持续显示已确认生效的曲线，避免中间短暂回到 sRGB 路径或图标。

- Retry failed tray updates, recreate missing notification icons, and resync after display changes. Periodically refresh the icon so it recovers if Explorer loses an accepted update during HDR display reconnection.

  托盘更新失败时自动重试，图标丢失时重新添加，并在显示配置变化后重新同步。定期刷新图标，避免 HDR 显示器重连时 Explorer 丢失已接受的更新，导致曲线生效后仍显示 sRGB。

- Request composition after curve changes and restoration using a temporary window, with bounded retries and no focus, DWM restart, or display-mode change. The window opts out of Shell fullscreen detection before display to avoid fullscreen notification suppression and taskbar restacking. Its minimum nonzero opacity may cause a very slight one-frame brightness change.

  切换曲线和恢复后通过临时窗口请求合成，重试次数有限，不抢焦点、不重启 DWM、不切换显示模式；窗口在显示前退出 Shell 的全屏判定，避免触发全屏免打扰和任务栏层级切换，其最低非零不透明度可能带来一帧极轻微的亮度变化。

- Restore the original shader path and hook entry bytes when switching to sRGB or exiting, without restarting DWM. Keep the application running if restoration has not been confirmed.

  切换到 sRGB 或退出时恢复原始着色器路径和挂钩入口，不重启 DWM；尚未确认恢复时继续保留程序运行。

- Attempt a short hold of the previous HDR image before restoring sRGB or exiting, compose once underneath after hook restoration, then uncover the full output. A slightly transparent static DirectComposition surface keeps underlying windows participating in composition, with a small residual blend of the live picture. Use a local FP16 scRGB copy, completed on the GPU before display; no image files or display-mode changes are involved. Offline tests cover ordering, copy bounds and error cleanup; visible HDR behavior remains subject to display validation.

  恢复到 sRGB 或退出前尝试短暂保持旧 HDR 画面，确认挂钩恢复后在覆盖期间补一次合成，随后整体移除覆盖。静态 DirectComposition 表面保留极小透明度，让下方窗口继续参与合成，期间可能略微混入实时画面。仅在本机 GPU 内存中复制 FP16 scRGB，确认 GPU 复制完成后才显示，不保存图片文件、不改变显示模式。离线测试覆盖时序、复制范围与失败清理，实际 HDR 观感仍待显示器验证。

- Reject pointer-only startup frames and protected, rotated, unsupported-format or wrong-size captures. Remove a cover on errors, display changes or a one-second wait timeout. When no cover is available, request three spaced composition refreshes; this fallback can update regions at different times and is not a DWM frame-level presentation acknowledgement.

  拒绝启动阶段只有鼠标更新的帧，以及受保护、旋转、格式不支持或尺寸不符的画面。发生错误、显示配置变化或等待超过一秒时移除覆盖。覆盖不可用时回退为三次间隔合成补刷；回退可能分区域先后更新，不构成 DWM 的逐帧呈现确认。

- Use an independent guardian process and a six-second heartbeat timeout in the helper. Inactive helper DLLs and trampoline allocations remain until DWM exits to avoid unloading code that a thread may still reference.

  使用独立守护进程和辅助模块内的六秒心跳超时机制。停用的辅助 DLL 和跳板内存保留到 DWM 自行退出，以避开卸载时的线程引用竞态。

- Provide read-only module inspection, command-line control of the running instance, and explicit live isolation and watchdog diagnostics. Live diagnostics install DWM hooks and write reports only when invoked.

  提供只读模块检查、运行实例的命令行控制，以及主动调用的实时隔离和看门狗诊断。实时诊断会安装 DWM 挂钩，仅在调用时生成报告。

- Enumerate source shaders read-only. Original bytecode and shared shader libraries remain unchanged.

  以只读方式枚举源着色器；原字节码及共享着色器库保持原样。

### Packaging and licensing / 打包与许可

- Keep unpacked development files in `dev/`, including the executable, its matching versioned helper DLL, `agent-file.txt`, and `Restore-Display.ps1`. Development builds create no portable archive.

  开发文件以解包形式保存在 `dev/`，包含主程序、匹配的版本化辅助 DLL、`agent-file.txt` 和 `Restore-Display.ps1`；开发构建不生成便携压缩包。

- Include English and Simplified Chinese READMEs and this bilingual note. Owner-requested releases use a fixed manifest, a versioned `sdr-eotf-patcher-<version>` folder, a ZIP containing that folder, and an adjacent SHA-256 file. Preserve historical releases by default.

  提供英文和简体中文 README，以及本双语说明。按所有者指令发布时，使用固定清单生成带版本号的 `sdr-eotf-patcher-<版本号>` 文件夹、包含该文件夹的 ZIP 和相邻的 SHA-256 校验文件；默认保留历史发布。

- License the project under GNU GPL v3 and retain the third-party copyright and license notices.

  项目采用 GNU GPL v3，保留第三方版权和许可声明。

### Compatibility and limitations / 兼容条件与限制

- Require Windows x64 and the exact `dwmcore.dll` profile for version `10.0.26100.9949`. A file digest or hook entry mismatch prevents activation.

  需要 Windows x64，且 `dwmcore.dll` 必须精确匹配 `10.0.26100.9949` 的内置配置；文件摘要或挂钩入口字节不匹配时不启用。

- Support up to 8 active outputs, identified by display rectangles including desktop origins. Unknown or conflicting contexts keep the original shader.

  最多支持 8 个活动输出，使用包含桌面原点的显示矩形识别归属；归属未知或上下文冲突时使用原着色器。

- Apply conversion only to matching SDR decoding instruction groups. Native HDR/PQ/HLG receives no whole-frame Gamma or LUT processing; exclusive fullscreen, direct MPO presentation, and application-generated scRGB may bypass conversion.

  仅转换匹配的 SDR 解码指令组。原生 HDR/PQ/HLG 不进行整帧 Gamma 或 LUT 处理；独占全屏、MPO 直出和应用自行输出 scRGB 的路径可能绕过转换。

Usage, command-line options, and build instructions are in `README.md`.

使用方式、命令行参数和构建步骤见 `README_zh-CN.md`。
