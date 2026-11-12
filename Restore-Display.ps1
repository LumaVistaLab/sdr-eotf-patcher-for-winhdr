<#
.SYNOPSIS
重新应用 Windows 保存的显示配置。
.DESCRIPTION
调用 SetDisplayConfig，使用系统数据库中的显示配置重新初始化活动拓扑。
补丁挂钩的恢复由主程序退出流程和辅助模块负责。
.EXAMPLE
.\Restore-Display.ps1
#>
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class SdrEotfDisplayRecovery {
    [DllImport("user32.dll")]
    public static extern int SetDisplayConfig(uint paths, IntPtr pathArray, uint modes, IntPtr modeArray, uint flags);
}
'@
$recoveryResult = [SdrEotfDisplayRecovery]::SetDisplayConfig(0, [IntPtr]::Zero, 0, [IntPtr]::Zero, 0x8F)
if ($recoveryResult -ne 0) { throw "Windows display recovery failed: $recoveryResult" }
Write-Output 'Windows saved display configuration has been reapplied.'
