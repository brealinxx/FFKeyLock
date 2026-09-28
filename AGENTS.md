# FFKeyLock 维护指引

本文件适用于 Astra 及其他维护本仓库的开发代理。开始修改前先阅读本文件，再阅读涉及模块的实现；以当前代码和用户最新要求为准。本文描述的是 2026-09-28 的 v0.6.1 工作区，后续改变架构、构建或配置格式时同步更新本文。

## 1. 产品目标与不可退化的体验

FFKeyLock 是原生 Windows 游戏键盘保护工具，使用 C++20、Unicode Win32 和系统绘图 API。现代化界面必须保留原生程序启动快、常驻开销低、托盘操作方便、键盘导航可用的特点。不要为外观引入浏览器、托管运行环境、后台服务、驱动或游戏进程注入。

用户的核心场景：猫咪会趴在键盘 F 区，进入指定游戏时自动应用该游戏的锁键配置。现有“防误触预设”锁定 F1–F12、Print Screen、Scroll Lock、Pause / Break，允许逐键放行。不要因重新应用预设之外的普通刷新而覆盖用户例外设置。

- 游戏获得前台时应用其配置，切出后撤销游戏专属保护；独立的全局 Windows 键策略另行计算。
- 输入法保护和锁键保护是独立开关；只启用锁键的游戏不能被强制切换输入法。
- 游戏聊天期间可以释放输入法控制，但仍须保持该游戏的锁键策略。
- 手动暂停和紧急解除优先于自动检测；切换游戏不能偷偷恢复保护。
- 保留单实例、托盘、开机启动、便携配置、主题与 DPI 支持。

## 2. 开始工作时

1. 查看 `git status --short` 和相关文件的差异。工作区可能包含上一轮尚未提交的完整改动，不能假定所有差异都是自己产生的；不要重置、清理或覆盖它们。
2. 开发前先切换到 `dev` 分支。切换前完整保留已有改动；分支落后或分叉时先检查历史，再合入当前主线，不能重置覆盖。完成验证后按用户授权同步分支；分支同步不等于授权创建发布 tag 或发布 Release。
3. 阅读 [README.zh-CN.md](README.zh-CN.md)、[CHANGELOG.md](CHANGELOG.md) 和 [tests/VERIFICATION.md](tests/VERIFICATION.md)，了解功能与已知验证边界。
4. 先读 [AppState.h](FFKeyLock/AppState.h)，再按下表定位入口和调用方。优先用 `rg` 搜索符号，确认文件是否实际参与当前界面。
5. 修改前明确受影响的是配置草稿、已保存配置、当前游戏会话还是输入线程快照，不要混用这些状态。
6. 只做与任务相关的改动。需要新增源文件时，同时维护主项目 `.vcxproj` 和 `.vcxproj.filters`。

## 3. 模块导航

| 文件或目录 | 职责与修改入口 |
|---|---|
| [FFKeyLock.cpp](FFKeyLock/FFKeyLock.cpp) | 程序启动、单实例、配置路径、DPI、消息循环；支持 `--config` 和 `--background`。 |
| [AppState.h](FFKeyLock/AppState.h)、[AppState.cpp](FFKeyLock/AppState.cpp) | `GameProfile`、主线程共享状态、消息和定时器定义。新增持久化属性从这里开始。 |
| [GameProtection.cpp](FFKeyLock/GameProtection.cpp) | 前台检测、游戏身份匹配、进入/离开游戏、聊天状态、暂停/恢复、配置规范化及防误触预设。`ApplyForegroundGame` 是状态切换的集中入口。 |
| [WindowsKeyGuard.cpp](FFKeyLock/WindowsKeyGuard.cpp) | 名称沿用历史：现在是通用锁键引擎，负责独立输入线程、低级键盘钩子、策略快照、紧急解除和本地按键测试。 |
| [KeyPolicy.h](FFKeyLock/KeyPolicy.h) | 无界面依赖的 `KeyPressTracker`，处理同一次按键的按下、重复、松开配对。 |
| [Config.cpp](FFKeyLock/Config.cpp) | INI 读取、校验、原子保存、旧版迁移、导入导出及本地数据清理。 |
| [InputLanguage.cpp](FFKeyLock/InputLanguage.cpp) | 原生输入法切换与恢复；不能向已经失效的游戏窗口恢复布局。 |
| [MainWindow.cpp](FFKeyLock/MainWindow.cpp) | 原生主窗口、命令/消息分发、设置；菜单和运行中程序选择器已独立至 UI 模块。避免重新把业务和编辑器实现堆入此文件。 |
| [UI/Main/MainContentView.cpp](FFKeyLock/UI/Main/MainContentView.cpp) | 当前主界面：游戏列表、搜索、选择、草稿保存/放弃、复制粘贴配置。 |
| [UI/Profiles/ProfileEditor.cpp](FFKeyLock/UI/Profiles/ProfileEditor.cpp) | 嵌入式配置草稿编辑器：键盘、预设、输入法选项、脏状态与测试反馈。 |
| [UI/Controls/ScrollView.cpp](FFKeyLock/UI/Controls/ScrollView.cpp) | 圆角外层、`FFKeyLockScrollViewport` 内缩固定视口、`FFKeyLockScrollContent` 内容子窗口、原生滚动条、焦点和滚轮处理；滚动只移动窗口并完整重绘，不重排控件。 |
| [UI/Library/GameLibraryView.cpp](FFKeyLock/UI/Library/GameLibraryView.cpp) | 原生两行列表、空白区、独立滚动条、图标缓存；主界面与运行中程序选择器共用。 |
| [UI/Menu](FFKeyLock/UI/Menu) | `AppMenus` 构建菜单，`MenuBar` 提供客户区入口与 Alt / F10 导航，`PopupMenu` 装饰原生弹出菜单；活动菜单更新延后替换。 |
| [UI/Windows/RunningProgramPicker.cpp](FFKeyLock/UI/Windows/RunningProgramPicker.cpp) | 运行中程序枚举与选择。 |
| [ThemeManager.cpp](FFKeyLock/ThemeManager.cpp)、[Platform](FFKeyLock/Platform) | 配色、字体、高对比度、标题栏、DPI 与 GDI 资源管理。 |
| [UI/Rendering](FFKeyLock/UI/Rendering) | `Surface` 完整绘制容器背景；`NativeControls` 统一控件绘制，组合框列表使用主题滚动条子窗口，保留原生选择、键盘与滑块输入。具体约定见 [UI/README.md](FFKeyLock/UI/README.md)。 |
| [TrayIcon.cpp](FFKeyLock/TrayIcon.cpp)、[OverlayNotificationManager.cpp](FFKeyLock/OverlayNotificationManager.cpp) | 托盘与提示；醒目浮层启用时避免再弹重复通知。 |
| [tests/RegressionTests.cpp](tests/RegressionTests.cpp)、[tests/UIRenderingTests.cpp](tests/UIRenderingTests.cpp) | 状态、按键配对、配置、隐藏编辑器回归；独立可见窗口的像素一致性与交互验证。 |
| [installer/FFKeyLock.iss](installer/FFKeyLock.iss)、[.github/workflows](.github/workflows) | Inno Setup 安装包、构建与发布流水线。 |

历史 `ContentPanel`、`RoundedButton`、`ToggleSwitch`、`RoundedMenu`、`ProtectedProgramListView` 已移至 `archive/legacy-ui`，不参与应用或测试编译。`UI/ProtectedPrograms/ProtectedProgramCommands` 保留剪贴板与目录操作。

## 4. 锁键与线程约束

这是最容易造成输入丢失、卡键或游戏体验退化的部分，改动必须配套有针对性的回归验证。

- UI 线程维护应用状态，`RefreshKeyboardPolicy()` 生成策略快照，通过线程消息交给输入线程。输入线程独占其策略和按键跟踪状态；不要在钩子里直接访问主线程的容器或编辑器草稿。
- `WH_KEYBOARD_LL` 回调只做固定规模的键位查找、状态判断和消息投递。不能在里面读写配置、写日志、绘图、查询进程路径、等待锁或分配动态内存。
- 保留对 `LLKHF_INJECTED` 的提前过滤。本地测试依赖实体输入，不能用 `SendInput` 成功与否证明真实锁键效果。
- 同一次按压的拦截决定必须保持到松开：已放行的按下需要放行松开，已拦截的按下不能漏出孤立松开。长按重复、切窗、暂停和配置切换都不能破坏配对。
- 钩子安装时已经按住的键要保留配对语义。Pause、`VK_CANCEL`（Break）和可能只有松开事件的 Print Screen 必须按现有特殊路径处理。
- 紧急组合默认是 `Ctrl+Alt+Backspace`，可配置的替代主键是 End/Home。Ctrl、Alt 和当前紧急主键不能进入普通屏蔽集合。
- 紧急解除会先在输入线程锁存暂停，再通知 UI；保留锁存与确认机制，防止队列里的旧策略立即重新锁键。
- 聊天事件携带游戏会话编号，主线程必须丢弃过期会话的事件。不要把上一个游戏的 Enter/T/Y 等事件应用到下一个游戏。
- 前台检测以窗口事件为主，定时器兜底。不要改为高频遍历所有进程；不要在按键回调或绘制路径中提取程序图标。
- `KeyboardGuardHealthy()` 只能反映安装/线程状态，不能保证 Windows 从未移除钩子，更不能保证所有游戏或反作弊环境兼容。保留重新连接入口，准确描述限制。

## 5. 游戏身份与配置安全

- `g_gameExeNames` 名字有历史包袱：元素可以是规范化完整路径，也可以是旧版文件名身份，不能一律当作 basename。
- 新增游戏以完整路径区分同名程序。匹配先查完整路径，再走旧版名称兼容路径；旧记录有已知路径时仍需核对路径。使用已有 `MatchGameIdentity` 等辅助函数，不要另写宽松匹配。
- `NormalizeGameProfile()` 统一过滤非法/保留键、去重、限制超时；预设、编辑保存、导入等路径应保持一致约束。
- 全局 Windows 键开关是默认策略；不要遍历改写所有游戏配置。区分 `inheritWindowsKey` 和该游戏自己的 `lockWindowsKey`。
- 常规配置在 `%APPDATA%\FFKeyLock\config.ini`。`--config <路径>` 或程序旁的 `portable.ini` 使用独立配置；便携清理不能删除已安装版本的数据。
- 当前格式为版本 2：`[Settings]`、`[Library]`、`[Game.N]`；最多 4096 个游戏。保留 Unicode、完整路径和大配置库支持。
- 保存先写同目录临时文件并刷新，再替换目标文件。不要恢复为多次原地写入；失败必须返回错误并显示未保存状态，不能向用户宣称已落盘。
- 旧版迁移保留 `.v1.bak`；损坏或不支持的配置不能在加载时被覆盖，后续保存按现有逻辑先保留 `.invalid.bak`，备份失败就停止覆盖。
- 空游戏库是有效用户选择，不能每次启动都补回默认游戏。
- 导入先完整解析校验再合并；保存失败回滚内存集合。导入游戏库不能覆盖当前全局设置；相同身份的替换保留现有交互确认。

新增配置属性时同时检查：模型默认值、规范化、读取、保存、旧版缺省语义、导入导出、复制粘贴、编辑器脏状态、运行策略和回归用例。

## 6. 界面修改原则

- 沿用原生窗口和控件，保留 Tab 导航、焦点指示、复选框语义、系统无障碍信息和标准菜单行为。不要仅用自绘图形替代可访问的交互控件。
- 列表选中的游戏是编辑对象，前台运行的游戏是保护对象；两者不同，不要因为用户浏览列表就切换实际保护。
- 编辑草稿不自动落盘。切换游戏、删除或其他会丢失草稿的操作要保留保存/放弃/取消处理；保存失败不能清掉脏标记。
- 主题切换和状态刷新不能丢失草稿。搜索或通知刷新不能无条件重建列表、反复提取图标或扰动焦点。
- 应用客户区统一使用 `UI/Rendering`；禁止仅抑制背景擦除而不完整绘制背景，或恢复旧的 `WS_EX_COMPOSITED` 与滚动像素复制路径。
- 使用统一主题/DPI 工具，处理深浅主题、系统高对比度、跨屏缩放和小窗口滚动；不要新增固定物理像素布局。
- 本地按键测试只在指定测试窗口前台生效，切走或修改配置时停止。保留总开关关闭、已暂停、钩子不可用时的明确提示。
- 原生资源（字体、画刷、位图、图标、DC、钩子、事件、线程等）保持清晰所有权与释放路径，优先使用已有 RAII 工具。

## 7. 构建与回归命令

在仓库根目录用 PowerShell 执行。项目使用 MSVC **v145**、Windows SDK 10.0、C++20；Release 使用静态 C/C++ 运行库。主解决方案支持 x64、Win32、ARM64，测试工程支持 Release x64/Win32。

如果当前终端找不到 MSBuild，可用 Visual Studio Installer 的 `vswhere` 定位，不要依赖某台电脑的安装盘符：

```powershell
$vswherePath = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuildPath = & $vswherePath -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuildPath) { throw '未找到 MSBuild，请检查 Visual Studio C++ 构建环境。' }

& $msbuildPath FFKeyLock.slnx /p:Configuration=Release /p:Platform=x64 /m
& $msbuildPath FFKeyLock.slnx /p:Configuration=Release /p:Platform=Win32 /m

& $msbuildPath tests/FFKeyLock.Tests.vcxproj /p:Configuration=Release /p:Platform=x64 /m
& .\tests\artifacts\x64\FFKeyLock.Tests.exe "$PWD\tests\artifacts"
& $msbuildPath tests/FFKeyLock.Tests.vcxproj /p:Configuration=Release /p:Platform=Win32 /m
& .\tests\artifacts\Win32\FFKeyLock.Tests.exe "$PWD\tests\artifacts"
```

逐条检查退出码；构建失败时不要运行残留的旧测试程序并当作新改动通过。测试程序必须带测试目录参数，会在其中创建 `run-<PID>` 隔离数据。`tests/artifacts` 是生成目录，不提交二进制。

应用产物分别是 `x64/Release/FFKeyLock.exe`、`Release/FFKeyLock.exe` 和 `ARM64/Release/FFKeyLock.exe`。安装 ARM64 C++ 工具后可额外执行：

```powershell
& $msbuildPath FFKeyLock.slnx /p:Configuration=Release /p:Platform=ARM64 /m
```

可见 UI 验证使用独立测试窗口与数据，不安装键盘钩子。需要交互式 Windows 桌面，会短暂显示窗口；按 Esc 可以中止。不要在无桌面的 CI 中直接运行或将其替代真实跨屏 DPI 验收：

```powershell
& $msbuildPath tests/FFKeyLock.UI.Tests.vcxproj /p:Configuration=Release /p:Platform=x64 /m
& .\tests\artifacts\x64\FFKeyLock.UI.Tests.exe "$PWD\tests\artifacts"
& $msbuildPath tests/FFKeyLock.UI.Tests.vcxproj /p:Configuration=Release /p:Platform=Win32 /m
& .\tests\artifacts\Win32\FFKeyLock.UI.Tests.exe "$PWD\tests\artifacts"
```

安装包需要 Inno Setup；已构建对应架构后执行 `ISCC.exe /DAppArchitecture=x64 installer/FFKeyLock.iss`，其他架构参数是 `x86`、`arm64`。检查脚本中的产物路径与输出位置，不要把未构建的架构打包。

发布版本校验与便携包生成（ZIP 默认携带空游戏库的 `portable.ini`）：

```powershell
powershell -File scripts/Test-ReleaseVersion.ps1 -Tag v0.6.1
powershell -File tests/ReleaseTooling.Tests.ps1
powershell -File scripts/New-PortablePackage.ps1 -Architecture x64
```

版本校验覆盖版本头、manifest、安装脚本默认值和中英文日志；`-Artifacts` 可额外校验 EXE/安装包的文件和产品版本。CI 覆盖 `dev`，Release workflow 在打包前检查 tag 与产物版本。安装脚本通过 `installer/InitializeLanguage.iss` 仅初始化不存在的配置文件，升级保留已有文件并跳过首次语言选择页。

发布工具测试还包括 `tests/InstallerLanguage.Tests.ps1`（需 Inno Setup 的 `ISCC.exe`）和 `tests/PortablePackage.Tests.ps1 -Archive <ZIP 路径>`；前者只运行隔离测试安装器，不安装主程序。实体输入与真实升级步骤见 [实机验收清单](tests/MANUAL-RELEASE-CHECKLIST.md)。

文本统一使用 LF（`.gitattributes`）；Debug、Release、ARM64、测试产物和 `dist/` 不纳入版本控制。

SDK 访问被沙箱阻止、缺少 ARM64 工具链或没有 Inno Setup，都属于环境问题；如实报告并完成可执行的验证，不要为了掩盖环境错误随意降低工具集或移除架构。

## 8. 验证范围与安全的本机运行

代码改动按影响范围验证：配置/状态/键盘引擎优先补充真实边界用例并运行回归，界面改动还需检查布局与交互。纯文档改动检查事实、路径和差异即可。最后运行 `git diff --check`。

需要启动程序验证时，使用工作区内独立的 `--config` 路径，避免改动日常使用配置。注意程序是单实例：已运行实例会影响测试启动，不要擅自结束不属于本次验证的进程。后台启动测试进程时使用隐藏窗口选项，需验收界面才显示；测试后只清理本次创建的进程和数据。遵守用户对电脑操作的停止指令。

重点回归顺序：

1. 防误触预设 → 放行一个常用 F 键 → 保存并重启，确认例外保留。
2. 指定游戏前台锁定 → 聊天时仍锁定 → Alt+Tab 释放 → 返回恢复。
3. 长按中切窗/换配置，验证按下松开配对；分别检查 PrtSc、Scroll Lock、Pause/Break。
4. 切换两个同名但不同路径的程序，确认应用各自配置。
5. 暂停后切换游戏仍暂停；紧急解除后只有明确恢复才重新启用。
6. 只开锁键不改变输入法；关闭单个游戏或总开关后正确撤销保护。
7. 损坏配置、保存失败、导入失败、空列表和旧版迁移不丢数据。
8. Tab 导航、未保存草稿、主题、高对比度、不同 DPI 和窄窗口布局。

截至本文日期，v0.6.1 的 x64 和 Win32 发布构建、各 153 项回归断言已通过；相同 UI 源码的滚动面板与下拉列表修复已通过各 331 项可见 UI 检查（含 96／120 DPI 隐藏编辑器验证、四角背景、滑块几何/悬停/原生拖动、下拉列表箭头/轨道/捕获及键盘滚动）。详细记录在 [tests/VERIFICATION.md](tests/VERIFICATION.md)；这只是历史结果，不代表后续修改自动通过。实体键盘/真实游戏/反作弊、完整界面交互与跨屏 DPI、ARM64 和真实安装升级仍有未验证项，必须区分“自动测试通过”和“实机验收通过”。不要把一次空闲资源采样当作性能保证。

## 9. 交付与文档维护

- 功能行为变化同步中英文 README；发布版本变化同步 `Version.h`、应用 manifest、安装脚本默认版本及中英文 CHANGELOG。资源文件从版本头读取版本，先确认引用关系。
- 仅记录实际执行的验证，说明未执行项和原因；不要宣称云端 CI 或真实游戏测试通过，除非有对应结果。
- 不因一次代码修改自动发布、推送或调整用户的开机启动/系统设置；按用户授权范围交付。
- 最终用简洁中文说明改了什么、验证结果和剩余限制，提供重要文件或程序路径。若调整了本文所述入口、格式或命令，同步修订本文件，方便下一次直接接手。
