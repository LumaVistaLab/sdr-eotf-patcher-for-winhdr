# SDR EOTF Patcher for Windows HDR

语言：简体中文 | [English](README.md)

为 Windows HDR / 杜比视界输出中的 SDR 内容选择解码曲线，支持 Gamma 2.2、Gamma 2.4、Rec.709 和原始 sRGB 路径。

程序按显示器渲染上下文隔离，只替换所选 HDR 输出使用的 SDR 着色器。其他 SDR 显示器保持原有处理路径。系统 DLL 文件、驱动和 ICC 配置不作修改。

发布说明：[English / 简体中文](RELEASE_NOTES.md)。

## 兼容条件

- Windows x64，目标显示器已开启 HDR / 杜比视界。
- 当前版本仅支持与内置隔离配置完全匹配的 `dwmcore.dll`。文件摘要或挂钩入口字节不匹配时不启用。
- 最多支持 8 个活动输出。各输出必须能按包含桌面原点的显示矩形唯一识别；归属不明确或上下文冲突时保持原始路径。

当前支持的模块版本为 `10.0.26100.8875`，SHA256 为：

```text
825726F26BD6B5A02B49F6145963B54D537D3D1239055DDA28D1FB0B3E864EF3
```

Windows 更新后，应重新检查模块兼容性。仅修改版本号、文件摘要或偏移值不能完成适配。

## 使用

1. 解压 `SdrEotfPatcher-portable.zip`。
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

程序不发送自动通知，也不写常驻运行日志。重复打开说明会替换已有窗口；已打开时，显示配置变化会在枚举稳定后刷新内容，手动关闭后不自动弹出。

## 显示器变化与恢复

连接、断开显示器或切换显示模式时，程序先暂停转换，等待显示拓扑稳定，再核对屏幕归属。目标暂时不可用时保留所选曲线，等待该目标恢复。显示器名称优先读取 EDID 名称，缺失时读取 PnP 友好名称。

切换到 sRGB 或正常退出时恢复原始着色器路径和挂钩入口，不重启 DWM。恢复尚未确认时，程序继续驻留，可再次选择“退出并恢复”。

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

五组检查覆盖源着色器只读枚举和损坏输入拒绝、四条曲线的 WARP GPU 回读、模拟多屏隔离、真实 x64 包装函数与本进程挂钩恢复、托盘文字居中，以及原生说明窗口生命周期。WARP 调试设备检查需要已安装 Direct3D 调试层。这些检查不在系统 DWM 中安装挂钩。

构建生成的解包文件位于 `build/package/`，交付文件为 `dist/SdrEotfPatcher-portable.zip` 和相邻的 `.sha256` 文件。打包采用固定文件清单，包含主程序、一个匹配的辅助 DLL、文件名记录、恢复脚本、中英文说明、双语发布说明和许可声明；不包含测试程序、诊断报告、符号缓存和链接产物。

构建后可运行只读检查：

```powershell
.\build\package\SdrEotfPatcher.exe --inspect "$PWD\build\inspection.txt"
```

## 项目结构

| 目录或文件 | 内容 |
| --- | --- |
| `README.md` | 英文项目说明 |
| `README_zh-CN.md` | 简体中文项目说明 |
| `RELEASE_NOTES.md` | 中英双语发布说明 |
| `LICENSE` | GNU GPL v3 许可全文 |
| `build.ps1` | 构建、检查和打包入口 |
| `Restore-Display.ps1` | 重新应用 Windows 保存的显示配置 |
| `.gitignore` | 构建产物和缓存的版本控制忽略规则 |
| `src/main.cpp`、`src/info_window.h`、`src/tray_icon.cpp` | 托盘界面、原生说明窗口和图标 |
| `src/core.h`、`src/core.cpp`、`src/dxbc_checksum.cpp` | 公共类型、系统模块读取、着色器枚举和 DXBC 校验 |
| `src/isolation.h`、`src/isolation_host.cpp` | 隔离协议、显示器枚举、守护进程和辅助模块管理 |
| `src/isolation_agent.cpp`、`src/isolation_thunks.asm` | DWM 内的上下文识别、着色器变体和挂钩包装 |
| `src/isolation_policy.cpp`、`src/isolation_profile.h` | 输出匹配、曲线转换规则和精确模块配置 |
| `src/app.manifest` | 应用权限和 Windows 兼容性声明 |
| `src/third_party/` | 第三方源码、版权和许可声明 |
| `tests/` | 独立进程检查程序 |
| `tools/` | 公开符号下载、符号枚举、磁盘反汇编和配置生成工具 |
| `build/` | 中间文件、缓存和待打包文件，不纳入版本控制 |
| `dist/` | 交付压缩包及 SHA256 校验文件，不纳入版本控制 |

`tools/` 不参与普通构建。研究工具从仓库根目录运行，使用 `build/` 存放中间数据；反汇编脚本另需 Python、Capstone 和 pefile。配置生成脚本只接受其内置摘要对应的磁盘模块，其他版本需要重新分析入口、参数和内存布局。

## 许可与第三方组件

本项目采用 GNU General Public License v3（GNU GPL v3），全文见 `LICENSE`。第三方版权和许可声明随源码与程序包提供：

| 组件 | 用途 | 声明文件 |
| --- | --- | --- |
| dwm_eotf_rs | sRGB 着色器标识和常数参考 | `dwm_eotf_rs-LICENSE` |
| GPUOpen ShaderUtils DXBCChecksum | DXBC 校验；derived from the RSA Data Security, Inc. MD5 Message Digest Algorithm | `THIRD_PARTY_NOTICES` |
| MinHook / Hacker Disassembler Engine | 内存挂钩和指令解码 | `MinHook-LICENSE.txt` |

源码中的对应声明位于 `src/third_party/`；MinHook 的原始许可文件为 `src/third_party/minhook/LICENSE.txt`。
