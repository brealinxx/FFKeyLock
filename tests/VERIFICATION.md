# v0.6.0 验证记录

日期：2026-09-21。

## 发布前 UI 修复验证

- Release x64 / Win32 应用及两类测试工程均构建通过；两种架构各通过 **153 项功能回归、108 项可见 UI 检查**。
- 新增测试先在修复前复现失败：每行子菜单检测出 2 个箭头；鼠标移动后、处理队列重绘前，滚动条像素与完整主题绘制不一致。
- 修复后检查深色 → 浅色 → 深色三轮：游戏列表及编辑器滚动条的移动、移出、`SetScrollPos` 和 `SetScrollInfo` 更新都立即呈现主题画面；下拉列表非客户区悬停检查通过。菜单三个子菜单项在普通及通过原生方向键导航进入的高亮状态均只有一个箭头，并验证高亮背景确实出现。
- 此轮图像位于 `tests/artifacts/ui-30192`（x64）和 `tests/artifacts/ui-39176`（Win32）。测试窗口使用自身数据，未修改日常配置、移动实体鼠标或向其他程序发送输入。
- 两种架构保留 96/120 DPI 隐藏编辑器验证；跨屏 DPI、高对比度及实际游戏边界仍见下文。单帧/消息边界检查不等于所有显示环境的帧率保证。
- 中英文 README 使用 `Assets/intro_cn.png` 与 `Assets/intro_en.png`；安装脚本与 ZIP 工作流同步包含图片。安装包未在本机重新构建。

## 本轮 UI 架构重构验证

- MSVC v145 / Windows SDK：Release x64 与 Win32 的应用、回归测试和可见 UI 测试工程均构建通过。
- 两种架构各通过 **153 项功能/隐藏编辑器回归断言**及 **71 项可见 UI 检查**。最后一轮可见窗口图像分别保存在 `tests/artifacts/ui-39276`（x64）和 `tests/artifacts/ui-13672`（Win32）。
- 可见测试使用真实窗口客户区与非客户区像素，不使用 `WM_PRINTCLIENT` 代替屏幕结果。比较滚动、快速细粒度滚轮、展开/收起、主题切换、过滤/空列表、窄窗口/恢复尺寸、弹出菜单关闭后的画面与完整重绘是否一致。比较前等待原生鼠标悬停消息稳定，避免把不同 hover 状态当作绘制残留。
- 检查菜单和游戏列表空白区的暗色背景；长下拉列表保持紧凑高度，滚动条在打开和滚动后均采用主题颜色。新增检查曾发现该原生非客户区仍为白色，修复后两种架构均通过。
- 检查原生复选框产生草稿、深浅主题切换保留草稿、菜单打开时刷新菜单树、Alt/F10、方向键和 Esc 返回焦点。保留隐藏编辑器的 96/120 DPI 验证。
- 已检查深色、浅色、英文窄窗口、弹出菜单和下拉列表的实际截图。未以此宣称跨显示器 DPI、系统高对比度、完整人工交互或真实游戏已验收。
- 测试只使用独立数据和自身窗口，不启用键盘钩子、不改日常配置或系统设置；测试进程已退出。输入引擎行为及配置格式保持原有实现。
- 项目与 filters 的活动 UI 源文件清单校验通过；历史 UI 已移至 `archive/legacy-ui` 并退出编译。`git diff --check` 按 Windows CRLF 规则检查通过。
- 本轮未进行性能基准或新的空闲资源采样；仍使用原生 Win32/GDI，无新增浏览器、托管运行时、后台服务或常驻绘制定时器。下文资源数据仅为历史采样。

## 上一轮局部 UI 调整（历史记录）

- Release x64 与 Win32 应用和测试工程均构建通过；两种架构分别通过 153 项回归断言（此前为 89 项）。
- 新增隐藏原生编辑器测试，分别运行于 120 DPI 与 96 DPI 窗口环境：滚动不改变键位坐标、尺寸和字体；上下边界、半格滚轮累积、展开／收起后滚动位置收敛；无需滚动时键盘宽度不跳变；Tab 顺序与布局一致。
- 验证深浅主题切换保留未保存修改与 F2 放行例外，并检查复选框、下拉框、编辑器滚动条的暗色背景像素。
- 使用独立隐藏窗口生成并检查深色、浅色、滚动后及英文窄窗口的原生控件渲染图。未显示或操作用户正在运行的调试实例；渲染图不代表真实窗口连续滚动、跨屏 DPI 或高对比度已验收。
- `git diff --check` 按 Windows CRLF 行尾规则检查通过（WSL 下使用 `git -c core.whitespace=blank-at-eol,blank-at-eof,space-before-tab,cr-at-eol diff --check`）。
- 未修改锁键引擎、配置格式、用户系统设置或开机启动；未重新进行资源采样、真实游戏或实体键盘验收。ARM64 与安装包仍沿用下文的未验证边界。

## 上一轮功能验证

- MSVC v145 / Windows SDK，Release x64 与 Win32 构建通过。
- x64 与 Win32 回归测试分别通过 89 项断言。
- 检查两种架构的发布程序依赖：仅 Windows 系统 DLL，无 VC++ 动态运行库、浏览器或托管运行环境依赖。
- `git diff --check` 通过。
- 使用独立便携配置打开过原生界面，确认游戏列表、配置编辑器、按钮、复选框及系统无障碍树正常出现；观察到未保存修改状态。
- 所有验证使用工作区内的测试配置；测试进程已结束。

回归覆盖：按下／松开配对、重复长按、切换策略时保持配对、安装前已按住的键、Pause 和 Print Screen 特殊序列、防误触预设及例外键、同名程序路径区分、前台游戏切换、禁用／恢复保护、暂停不被检测覆盖、游戏内聊天仍锁键、独立输入法控制、Unicode 配置、300 游戏的大配置库、旧版迁移和备份、空列表、导入失败回滚、保存失败、无效配置保留。

## 早期版本资源采样（不代表本轮 UI 性能）

在 x64 发布程序、独立空游戏库、关闭通知、托盘后台状态下，启动稳定后采样 20.01 秒：

| 指标 | 结果 |
|---|---:|
| 工作集 | 13.49 MiB |
| 私有内存 | 2.35 MiB |
| CPU 时间增量 | 0 ms（系统计量粒度内） |
| 句柄数 | 160 |

这是单次本机空闲采样，不是与旧版本对比，也不代表有游戏运行、持续键盘输入或通知动画时的资源占用。采样之后的最终修正仅涉及输入事件边界和本地测试提示。

## 验证边界

- 本机未安装 ARM64 v145 工具，ARM64 构建未完成；项目和 CI 的 ARM64 配置保留。
- 本机未安装 Inno Setup，未生成或验证新安装包；安装脚本版本已更新。
- 早期功能验证曾按用户 Esc 停止；本轮按新的 UI 重构请求运行了独立可见测试窗口。真实跨屏 DPI、系统高对比度与完整人工交互仍未验收，不能由隐藏窗口或像素对比代替。
- 实体键盘的 F 区、PrtSc、Scroll Lock、Pause / Break、紧急快捷键、游戏及反作弊兼容性尚未实测。单元测试和窗口内按键测试不能替代真实游戏验收。
- CI 与 Release workflow 已增加回归测试步骤，但本轮未向远端推送、执行云端流水线或发布版本。

## 复现

```powershell
MSBuild.exe FFKeyLock.slnx /p:Configuration=Release /p:Platform=x64 /m
MSBuild.exe FFKeyLock.slnx /p:Configuration=Release /p:Platform=Win32 /m
MSBuild.exe tests/FFKeyLock.Tests.vcxproj /p:Configuration=Release /p:Platform=x64 /m
tests/artifacts/x64/FFKeyLock.Tests.exe tests/artifacts
MSBuild.exe tests/FFKeyLock.Tests.vcxproj /p:Configuration=Release /p:Platform=Win32 /m
tests/artifacts/Win32/FFKeyLock.Tests.exe tests/artifacts
```

可见 UI 测试需要交互式 Windows 桌面，会短暂显示自己的窗口（Esc 中止）；不要直接放到无交互桌面的 CI 执行：

```powershell
MSBuild.exe tests/FFKeyLock.UI.Tests.vcxproj /p:Configuration=Release /p:Platform=x64 /m
tests/artifacts/x64/FFKeyLock.UI.Tests.exe tests/artifacts
MSBuild.exe tests/FFKeyLock.UI.Tests.vcxproj /p:Configuration=Release /p:Platform=Win32 /m
tests/artifacts/Win32/FFKeyLock.UI.Tests.exe tests/artifacts
```

建议真实游戏验收时依次检查：应用防误触预设 → 放行一个游戏常用 F 键 → 保存并重启 → 游戏前台锁定 → 聊天时保持锁定 → Alt+Tab 释放 → 返回恢复 → 长按中切窗 → 暂停跨游戏保持 → 紧急解除后手动恢复。
