# Release Notes / 发布说明

## Current release / 当前版本

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

- Restore the original shader path and hook entry bytes when switching to sRGB or exiting, without restarting DWM. Keep the application running if restoration has not been confirmed.

  切换到 sRGB 或退出时恢复原始着色器路径和挂钩入口，不重启 DWM；尚未确认恢复时继续保留程序运行。

- Use an independent guardian process and a six-second heartbeat timeout in the helper. Inactive helper DLLs and trampoline allocations remain until DWM exits to avoid unloading code that a thread may still reference.

  使用独立守护进程和辅助模块内的六秒心跳超时机制。停用的辅助 DLL 和跳板内存保留到 DWM 自行退出，以避开卸载时的线程引用竞态。

- Provide read-only module inspection, command-line control of the running instance, and explicit live isolation and watchdog diagnostics. Live diagnostics install DWM hooks and write reports only when invoked.

  提供只读模块检查、运行实例的命令行控制，以及主动调用的实时隔离和看门狗诊断。实时诊断会安装 DWM 挂钩，仅在调用时生成报告。

- Replace legacy global-patch generation with read-only shader enumeration, and remove unused wrappers and shared fields. Original bytecode and shared shader libraries remain unchanged.

  将旧全局补丁生成代码替换为只读着色器枚举，清理未使用的包装函数和共享字段；原字节码及共享着色器库保持原样。

### Packaging and licensing / 打包与许可

- Ship a portable executable with one matching versioned helper DLL, `agent-file.txt`, and `Restore-Display.ps1`.

  便携包包含主程序、唯一匹配的版本化辅助 DLL、`agent-file.txt` 和 `Restore-Display.ps1`。

- Include English and Simplified Chinese READMEs and this bilingual release note. Package files through a fixed manifest and provide a SHA256 checksum for the archive.

  随包提供英文和简体中文 README，以及本双语发布说明；使用固定清单打包，并为压缩包提供 SHA256 校验文件。

- License the project under GNU GPL v3 and retain the third-party copyright and license notices.

  项目采用 GNU GPL v3，保留第三方版权和许可声明。

### Compatibility and limitations / 兼容条件与限制

- Require Windows x64 and the exact `dwmcore.dll` profile for version `10.0.26100.8875`. A file digest or hook entry mismatch prevents activation.

  需要 Windows x64，且 `dwmcore.dll` 必须精确匹配 `10.0.26100.8875` 的内置配置；文件摘要或挂钩入口字节不匹配时不启用。

- Support up to 8 active outputs, identified by display rectangles including desktop origins. Unknown or conflicting contexts keep the original shader.

  最多支持 8 个活动输出，使用包含桌面原点的显示矩形识别归属；归属未知或上下文冲突时使用原着色器。

- Apply conversion only to matching SDR decoding instruction groups. Native HDR/PQ/HLG receives no whole-frame Gamma or LUT processing; exclusive fullscreen, direct MPO presentation, and application-generated scRGB may bypass conversion.

  仅转换匹配的 SDR 解码指令组。原生 HDR/PQ/HLG 不进行整帧 Gamma 或 LUT 处理；独占全屏、MPO 直出和应用自行输出 scRGB 的路径可能绕过转换。

Supported module SHA256 / 支持的模块 SHA256：

```text
825726F26BD6B5A02B49F6145963B54D537D3D1239055DDA28D1FB0B3E864EF3
```

Usage, command-line options, and build instructions are in `README.md`.

使用方式、命令行参数和构建步骤见 `README_zh-CN.md`。
