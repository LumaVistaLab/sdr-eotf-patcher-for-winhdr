# SDR EOTF Patcher for Windows HDR

语言：简体中文 | [English](README.md)

为 Windows HDR / 杜比视界输出中的 SDR 内容选择解码曲线，支持 Gamma 2.2、Gamma 2.4、Rec.709 和原始 sRGB 路径。

程序按显示器渲染上下文隔离，只替换所选 HDR 输出使用的 SDR 着色器。其他 SDR 显示器保持原有处理路径。系统 DLL 文件、驱动和 ICC 配置不作修改。

发布说明：[English / 简体中文](RELEASE_NOTES.md)。

## 兼容条件

- Windows x64，目标显示器已开启 HDR / 杜比视界。
- 当前版本仅支持与内置隔离配置完全匹配的 `dwmcore.dll`。文件摘要或挂钩入口字节不匹配时不启用。
- 最多支持 8 个活动输出。各输出必须能按包含桌面原点的显示矩形唯一识别；归属不明确或上下文冲突时保持原始路径。

当前支持的模块版本为 `10.0.26100.9949`，SHA256 为：

```text
A3C7441A4B53A302F836A3B9B04EFA343E320E2D0CFFCE730601F89689FB7B48
```

Windows 更新后，应重新检查模块兼容性。仅修改版本号、文件摘要或偏移值不能完成适配。

## 使用

1. 解压发布 ZIP（`sdr-eotf-patcher-<版本号>.zip`）并打开对应子文件夹；本地开发版本直接打开 `dev/`。
2. 保持 `SdrEotfPatcher.exe`、匹配的 `SdrEotfAgent-*.dll` 和 `agent-file.txt` 位于同一目录。
3. 运行主程序并接受管理员权限请求，随后通过通知区域图标操作。
4. 在菜单中选择曲线和目标 HDR 显示器。
5. 使用“退出并恢复”结束程序。

默认曲线为 Gamma 2.2，目标为按 Windows 显示设备编号排序后的第一个 HDR 输出。选择仅在本次运行期间保留。SDR 显示器标为“SDR 屏幕不适用”，不能选作目标；HDR 显示器标为“仅处理 SDR 内容”。

| 菜单选项 | SDR 解码方式 | 托盘标签 |
| --- | --- | --- |
| Gamma 2.2 | `L = V^2.2` | `2.2` |
| Gamma 2.4 | `L = V^2.4` | `2.4` |
| Rec.709 | BT.709 分段传递函数的逆变换 | `709` |
| sRGB | 使用 Windows 原始解码路径 | `sRGB` |

其中 `V` 为归一化 SDR 输入，`L` 为线性值。Rec.709 在 `V < 0.081` 时使用 `L = V / 4.5`，其余部分使用 `L = ((V + 0.099) / 1.099)^(1 / 0.45)`。曲线选择不改变色域、白点或视频范围。

曲线实际命中后，托盘显示绿色模式图标；原始路径或等待期间显示灰色 `sRGB`。悬停提示用于查看当前状态或问题。“状态与说明”显示当前显示器列表和功能说明，使用 Windows 原生信息框、系统尺寸规则和信息提示音。

在同一目标屏上切换 Gamma 2.2、Gamma 2.4 和 Rec.709 时保留已确认的输出归属；新着色器变体准备期间继续保留旧曲线，新曲线命中后托盘直接切换到对应模式。

切换曲线时请求桌面重新合成。恢复到 sRGB 或退出时，程序尝试在曾被转换的 HDR 输出上短暂保持最后一幅画面，在其下方恢复原始挂钩，覆盖期间补一次合成，随后整体移除覆盖。期间视频可能短暂停住。覆盖保留极小透明度，让下方窗口继续参与合成，因此保持期间可能略微混入实时画面。画面仅保存在本机 GPU 内存中，使用静态 FP16 scRGB DirectComposition 表面，不保存图片文件。覆盖窗口退出 Shell 的全屏判定，不抢焦点，也不切换显示模式。

覆盖要求输出未旋转，并能获得格式和尺寸完全匹配、确有桌面更新的 FP16 画面，不接受启动阶段只有鼠标更新的帧。完整复制画面并确认 GPU 完成后才显示覆盖。受保护、无法访问、格式不支持或尺寸不匹配时，使用常规合成补刷回退，可能分区域先后更新。恢复发生错误、显示配置变化或等待超过一秒时移除覆盖。离线检查验证时序、复制范围与失败清理；实际 HDR 色彩、窗口呈现和切换观感仍待显示器验证。

程序不发送自动通知，也不写常驻运行日志。重复打开说明会替换已有窗口；已打开时，显示配置变化会在枚举稳定后刷新内容，手动关闭后不自动弹出。

## 显示器变化与恢复

连接、断开显示器或切换显示模式时，程序先暂停转换，等待显示拓扑稳定，再核对屏幕归属。目标暂时不可用时保留所选曲线，等待该目标恢复。显示器名称优先读取 EDID 名称，缺失时读取 PnP 友好名称。

切换到 sRGB 或正常退出时恢复原始着色器路径和挂钩入口，不重启 DWM。恢复尚未确认时，程序继续驻留，可再次选择“退出并恢复”。

覆盖可用时，先准备并显示旧画面，再停用转换，确认挂钩恢复后才移除覆盖。没有有效覆盖时，守护进程会间隔请求三次合成刷新，随后报告 sRGB 状态或退出。再次选择 sRGB 也会请求回退补刷。合成刷新请求本身不构成 DWM 的逐帧呈现确认。

独立守护进程负责恢复；DWM 内的辅助模块使用六秒心跳超时机制，在心跳中断后停用转换并恢复入口。停用的辅助 DLL 和跳板内存保留到 DWM 自行退出，以避开卸载时的线程引用竞态。

`Restore-Display.ps1` 用于重新应用 Windows 保存的显示配置，可在 PowerShell 中手动运行：

```powershell
.\Restore-Display.ps1
```

补丁入口的恢复由程序退出流程和辅助模块负责。

## 处理范围

辅助模块依据显示器上下文决定是否替换着色器。只有所有活动输出均完成归属核对后，所选 HDR 输出才允许使用曲线变体。非目标输出、SDR 输出、归属未知或冲突的上下文均返回原着色器。

转换仅针对已链接 DXBC 执行字节码中完整匹配的 sRGB 解码指令组。原字节码和共享着色器库保持原样，各曲线变体由原着色器持有并随其释放。原生 HDR/PQ/HLG 内容不套用整帧 Gamma 或 LUT 后处理。

独占全屏、MPO 直出或应用自行输出 scRGB 等路径可能绕过这一阶段，因而不会命中转换。

## 命令行

以下控制命令操作已经运行的托盘实例，需要与该实例处于相同权限级别。

| 参数 | 作用 |
| --- | --- |
| `--gamma22` | 切换到 Gamma 2.2 |
| `--gamma24` | 切换到 Gamma 2.4 |
| `--bt709` | 切换到 Rec.709 |
| `--srgb` | 恢复原始 sRGB 路径 |
| `--exit` | 退出并恢复 |
| `--status REPORT` | 将当前状态写入指定文件，格式为 `status=数字` |

`--status` 的退出码为状态值：`0` 初始化、`1` 处理中、`2` 已应用、`3` 原始路径、`4` 等待 HDR 目标、`5` 失败、`6` 已停止、`7` 输出归属不明确、`8` 等待稳定拓扑。没有运行实例时返回 `2`，不生成报告；读取结果时应使用本次调用生成的文件。

`--srgb-start` 以原始路径启动托盘。`--inspect REPORT` 只读取系统模块和显示器配置，将检查结果写入指定文件，无需管理员权限，也不加载辅助模块。

实时诊断命令需要管理员权限，并应先退出托盘实例：

| 参数 | 作用 |
| --- | --- |
| `--diagnose-isolation REPORT` | 安装隔离入口，关闭着色器替换，观察上下文归属 |
| `--verify-live REPORT` | 短时启用 Gamma 2.2 并检查隔离范围及恢复 |
| `--verify-gamma24 REPORT` | 短时启用 Gamma 2.4 并检查隔离范围及恢复 |
| `--verify-bt709 REPORT` | 短时启用 Rec.709 并检查隔离范围及恢复 |
| `--verify-watchdog REPORT` | 短时启用 Gamma 2.2，停止心跳并检查自动恢复 |

这些命令会在当前 DWM 中安装挂钩，结束时恢复入口，并按指定路径生成报告。`REPORT` 是输出文件路径，其父目录须已存在。

## 构建与检查

需要 Visual Studio C++ x64 工具链、Windows SDK 和 PowerShell。构建脚本优先查找 Visual Studio 2019 Community，随后使用 `vswhere` 查找可用的 C++ 工具链。

在仓库根目录执行：

```powershell
.\build.ps1
```

编译并运行全部检查：

```powershell
.\build.ps1 -Test
```

八组检查覆盖源着色器只读枚举和损坏输入拒绝、四条曲线的 WARP GPU 回读、模拟多屏隔离、真实 x64 包装函数与本进程挂钩恢复、托盘文字居中、原生说明窗口生命周期、显示器重连或 Shell 更新失败后的托盘恢复、有限次合成刷新，以及恢复覆盖层的时序、画面验证与异常清理。WARP 调试设备检查需要已安装 Direct3D 调试层。这些检查不在系统 DWM 中安装挂钩。

只运行模块只读检查和纯 CPU 恢复时序测试时，使用 `.\build.ps1 -OfflineTest`。它编译全部检查，但只执行 `core_tests` 和 `restoration_transition_tests`，不打开测试窗口、不捕获屏幕、不创建设备进行渲染，也不注入 DWM。

每次成功构建都会将最新主程序、匹配的辅助模块和配套文件同步到 `dev/`，用于本地测试。构建前先退出已有开发实例。旧辅助 DLL 可能仍被 DWM 加载，因此予以保留。`dev/RELEASE_NOTES.md` 与仓库根目录文件完全一致，仅描述当前功能，不记录版本变化。开发构建不生成 ZIP 便携包，也不写入 `dist/`。

每次完成仓库修改后，交付测试前都必须同步 `dev/`。程序源码或构建输入变更时重新构建；仅文档或配套文件变更时同步对应文件，并核对开发产物为最新内容。根目录与开发版的 `RELEASE_NOTES.md` 都必须在开头、功能说明之前声明适用的目标二进制文件名、文件版本、架构和 SHA-256。

`build/package/` 是本次构建的干净暂存目录。固定文件清单包含主程序、一个匹配的辅助 DLL、文件名记录、恢复脚本、中英文说明、发布说明和许可声明；发布产物不包含测试程序、诊断报告、符号缓存和链接产物。

发布时机与版本号由仓库所有者决定。只有收到发布指令后，才整理该版本的实际变化，保存为 UTF-8 Markdown 文件，并使用指定版本号执行下列命令（此处版本号仅为示例）：

```powershell
.\release.ps1 -Version v1.0-stable -ChangesPath .\build\release-changes.md -Test
```

发布脚本重新构建，在发布副本的 `RELEASE_NOTES.md` 开头声明发布程序版本，以及适用的目标二进制文件名、文件版本、架构和 SHA-256，随后列出提供的版本变化。目标二进制声明取自本次构建的说明；缺少文件版本或摘要时拒绝打包。脚本创建：

```text
dist/
  sdr-eotf-patcher-v1.0-stable/
  sdr-eotf-patcher-v1.0-stable.zip
  sdr-eotf-patcher-v1.0-stable.zip.sha256
```

ZIP 包含完整的同名子文件夹；两份 README 的标题均为 `SDR EOTF Patcher for Windows HDR`。校验文件记录 ZIP 的 SHA-256 摘要及文件名。所有历史发布产物均予保留；同版本目录、ZIP 或校验文件中任何一项已存在时，拒绝复用该版本号。在所有者要求首次发布之前，`dist/` 保持为空，不存放开发文件或开发便携包。发布打包不改动根目录和开发目录的发布说明。发布输入仅取自本次干净暂存目录，不使用 `dev/` 或历史 `dist/` 内容。

运行 `.\tests\packaging_tests.ps1` 可在 `build/` 下的隔离测试目录中检查发布流程，不会在仓库的 `dist/` 中创建真实发布。

构建后可运行只读检查：

```powershell
.\dev\SdrEotfPatcher.exe --inspect "$PWD\build\inspection.txt"
```

## 项目结构

| 目录或文件 | 内容 |
| --- | --- |
| `README.md` | 英文项目说明 |
| `README_zh-CN.md` | 简体中文项目说明 |
| `RELEASE_NOTES.md` | 当前功能的中英双语说明，不记录版本变化 |
| `LICENSE` | GNU GPL v3 许可全文 |
| `build.ps1` | 构建、检查和开发产物同步入口 |
| `release.ps1` | 使用显式版本号和版本变化说明进行发布打包 |
| `AGENTS.md` | 仓库开发与发布约定 |
| `Restore-Display.ps1` | 重新应用 Windows 保存的显示配置 |
| `.gitignore` | 构建产物和缓存的版本控制忽略规则 |
| `src/main.cpp`、`src/info_window.h`、`src/tray_icon.cpp` | 托盘界面、原生说明窗口和图标 |
| `src/core.h`、`src/core.cpp`、`src/dxbc_checksum.cpp` | 公共类型、系统模块读取、着色器枚举和 DXBC 校验 |
| `src/isolation.h`、`src/isolation_host.cpp` | 隔离协议、显示器枚举、守护进程和辅助模块管理 |
| `src/display_refresh.h`、`src/display_refresh.cpp` | 切换曲线和恢复后的有限次整屏合成刷新 |
| `src/restoration_cover.h`、`src/restoration_cover.cpp`、`src/restoration_transition.cpp` | 使用公开接口保持 HDR 画面的恢复覆盖、校验及切换时序 |
| `src/isolation_agent.cpp`、`src/isolation_thunks.asm` | DWM 内的上下文识别、着色器变体和挂钩包装 |
| `src/isolation_policy.cpp`、`src/isolation_profile.h` | 输出匹配、曲线转换规则和精确模块配置 |
| `src/app.manifest` | 应用权限和 Windows 兼容性声明 |
| `src/third_party/` | 第三方源码、版权和许可声明 |
| `tests/` | 独立进程检查程序 |
| `tools/` | 共享打包清单、公开符号下载、符号枚举、磁盘反汇编和配置生成工具 |
| `build/` | 中间文件、缓存和待打包文件，不纳入版本控制 |
| `dev/` | 解包形式的开发版本，不纳入版本控制 |
| `dist/` | 历史发布目录、ZIP 压缩包及 SHA-256 校验文件，不纳入版本控制 |

`tools/package_manifest.ps1` 为开发构建和发布提供固定文件清单，其余研究工具不参与普通构建。研究工具从仓库根目录运行，使用 `build/` 存放中间数据；反汇编脚本另需 Python、Capstone 和 pefile。配置生成脚本只接受其内置摘要对应的磁盘模块，其他版本需要重新分析入口、参数和内存布局。

## 许可与第三方组件

本项目采用 GNU General Public License v3（GNU GPL v3），全文见 `LICENSE`。第三方版权和许可声明随源码与程序包提供：

| 组件 | 用途 | 声明文件 |
| --- | --- | --- |
| dwm_eotf_rs | sRGB 着色器标识和常数参考 | `dwm_eotf_rs-LICENSE` |
| GPUOpen ShaderUtils DXBCChecksum | DXBC 校验；derived from the RSA Data Security, Inc. MD5 Message Digest Algorithm | `THIRD_PARTY_NOTICES` |
| MinHook / Hacker Disassembler Engine | 内存挂钩和指令解码 | `MinHook-LICENSE.txt` |

源码中的对应声明位于 `src/third_party/`；MinHook 的原始许可文件为 `src/third_party/minhook/LICENSE.txt`。
