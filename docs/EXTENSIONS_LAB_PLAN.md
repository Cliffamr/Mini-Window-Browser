# WebView2 浏览器扩展：独立长期实验路线（设计讨论草案）

状态：2026-10-24，**仅分支及设计方案建立；目前不代表浏览器已支持扩展，也不应发布名为“扩展测试版”的无功能程序。**
独立分支：`experiment/webview2-extensions`
正式基线：`master` / v1.4.5
目标独立预发布：`v1.4.6-extensions-lab`（待可用、可验证的最小管理功能上线后才创建，并在同一标签上更新 ZIP，绝不标记 Latest）

## 定位与范围

“插件”首期专指微软 WebView2 官方支持的 Chromium **浏览器扩展（unpacked extensions）**，不是允许第三方 Win32 DLL 注入、动态加载 C++ 插件或修改引擎。不要混淆两种类型。

WebView2 已具有 ICoreWebView2EnvironmentOptions6::put_AreBrowserExtensionsEnabled、ICoreWebView2Profile7::{AddBrowserExtension,GetBrowserExtensions}、ICoreWebView2BrowserExtension 等正式 API。默认禁用扩展，启用需要在创建环境前配置，并使用可管理的 profile。安装后的扩展启用状态会持久化。微软明确提示：WebView2 不自带完整浏览器 toolbar；依赖图标、徽章、popup 的扩展即使加载，也可能缺少 UI 入口。

首期不支持在线商店一键安装、CRX 自动导入、自动更新、扩展市场、不可信 DLL。优先 MV3；对兼容性作个别测试而非声称支持所有 Chrome 扩展。

## 首要安全约束：实验宿主不应以管理员身份运行

小窗 v1.4.5 现有 EXE 的发行构建带 `requireAdministrator`（为了与管理员权限游戏的全局热键兼容）。而微软 2026 年 WebView2 安全指南明确推荐 WebView2 宿主进程以**标准用户权限**运行；需要提升权限的工作建议移到单独的高权限辅助进程。这对将要加载第三方扩展的实验版尤其重要。

- **任何不可信第三方扩展实装之前，必须先完成权限降级评估。** 建议实验版单独编译为 `asInvoker`，并显著提示与管理员权限游戏的热键可能受到影响；或将高权限热键职责隔离到受限辅助进程。不能以“只在测试分支”替代权限隔离。
- 首期只能使用自编写、源码完全可审查的无敏感权限 MV3 测试扩展验证基础接口；未解决权限模型前不做任意扩展安装分发。
- 禁止将扩展内容、网页消息或扩展提供的脚本任意转交 Win32 高权限执行接口。

官方文档：https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/security

## 实验版必须与正式版隔离

- 独立 ZIP 名称、EXE 明显 `扩展实验版` 标识，使用独立可辨识的窗口标题/关于信息。
- 即使用户把实验版解压至正式版目录，也必须防止试验数据覆盖正式 `config.ini`，不能仅靠用户看说明：代码应使用独立 `config.extensions-lab.ini` 及独立 WebView2 User Data Folder，例如 `XiaoChuang.ExtensionsLab.WebView2`。
- 实验版的扩展源文件放在稳定的实验专用目录 `extensions-lab/`，安装前显示扩展名称/版本/声明权限和来源；只有用户明确确认才调用 AddBrowserExtension。
- 目录路径稳定可访问；官方 API 不会复制用户提供的扩展目录，因此安装时应先将经过检验的扩展文件复制到实验版自管文件夹。避免路径穿越、符号链接逃逸、文件覆盖、非法层级、重名/ID 冲突、压缩包炸弹（首期仅文件夹导入）。
- 退出/卸载实验版不得误删正式版缓存、Cookie、配置、日志。
- 同时运行实验版与正式版的互斥量、窗口类名与配置路径要核对，不应出现实验版唤起正式版的混乱。
- 共享 WebView2 的原有单视图/多标签语义不改变；每次切换标签会导航，部分扩展的后台状态可能不符合 Chrome 表现。

## MVP 功能边界

1. 设置 → **实验性浏览器扩展**：默认关闭，启用需要重启；明确警示兼容性/权限与白屏风险。
2. **从本地文件夹安装**：选择包含 `manifest.json` 的目录；检查 MV3、必要文件、权限字段和文件夹安全；展示摘要与用户确认，再复制到受控目录调用官方 API。导入成功后显示真实扩展 ID、名称、版本、状态。
3. **扩展列表**：查询当前 WebView2 profile，显示名称、版本（本地 manifest）、标识、启停状态、来源；失败不能伪装为空列表。
4. **启用／禁用／卸载**：使用官方 API，操作结果反馈明确；卸载时按顺序移除已安装扩展和本地源文件，源文件清理失败应保留日志且不影响正常浏览。
5. **日志与故障定位**：记录扩展操作成功/失败、HRESULT、扩展计数、WebView2 进程异常、探测与重建，默认不写 URL、搜索词、网页标题、Cookie、密钥等；日志的全局保留策略沿用 v1.4.5。
6. **安全模式**：启动时强制禁用所有扩展（不删除扩展数据），便于判断“白屏是扩展导致还是浏览器自身导致”。即使 WebView 初始化失败，也应允许用户进入安全模式。

### 非目标

- 不承诺扩展 toolbar/action/popup 自动正常工作；没有宿主 UI 的浏览器特性要单独评估。
- 不以 Chromium 内核作为“完整 Chrome 扩展兼容性”的保证；`chrome.*` 接口要按个别扩展验证。
- 不允许在 `master` 默认启用扩展；不修改正式 v1.4.5 的稳定行为。
- 不自动从 Chrome/Edge 已安装目录扫描或导入扩展，避免用户数据权限及版权/信任问题。

## 分阶段实现与测试

**阶段 A：权限、安全、API 和隔离验证**
- 实验版默认 **asInvoker** 或由非提权 WebView2 子进程承载，先验证既有全局热键退化范围；绝不默认把未知扩展载入管理员权限浏览器宿主。
- 启用 `AreBrowserExtensionsEnabled` 的最低 SDK/Runtime 兼容检查和友好错误。
- 独立配置与 User Data Folder、互斥量/窗口标题、防止污染正式版。
- 单个自制无网络权限 MV3 扩展验证注入 CSS/DOM 标记，重启后持久化；卸载后不残留。

**阶段 B：可用的扩展管理器 + 首次独立预发布**
- 本地文件夹安装、列表、开关、卸载、权限摘要、错误提示和活动日志。
- 核心测试、Windows x64 CI，发行包只放实验 EXE/运行库、手册、许可证及原有三个 TXT；用户扩展和个人配置不打包。
- 首次单独发布 `v1.4.6-extensions-lab`，`--prerelease`，独立标题，不设置 Latest；后续原位覆盖同一测试标签的资产并公开当前 SHA256。

**阶段 C：长期回归**
- 无扩展/单扩展/多扩展；典型页面、视频、登录、跨标签；启动关闭 50–100 次；渲染进程退出、UI 无响应、WebView 重建、沙箱隔离、杀进程恢复、扩展升级和残留。
- 性能对照记录：导航完成时间、启动耗时、内存、CPU、视频播放和挖孔鼠标跟随；同一机器比较 v1.4.5 与实验版“关闭扩展”以及“启用扩展”。
- 一个扩展造成白屏时，复测禁用扩展能否恢复；独立日志应包括扩展计数/版本和异常关联，不披露敏感浏览信息。
- 对每个第三方扩展建立“成功加载 / 核心功能可用 / 部分可用 / 不可用”的明确兼容性结论，不只看能否安装。

**阶段 D：谨慎扩大能力**
- 调研 extension action 的菜单/设置入口和 MV3 权限/更新策略；只有 WebView2 允许并验证的 UI 才实现。
- 扩展安全与兼容达到稳定标准，再另行讨论是否合并到未来正式版，**不因长时间测试自动晋升稳定版**。

## 验收关卡

- 旧正式版与实验版配置、用户数据、扩展、进程实例完全隔离。
- 扩展安装/启停/卸载/重启持久化通过；不可信目录/manifest 错误有清晰反馈。
- 一键安全模式可在扩展引发异常时恢复启动及浏览。
- 现有 WebView2 白屏处理、窗口置顶、鼠标穿透、全局热键、预设、日志和升级不出现可重现严重回归。
- CI、用户实测与性能对照分开记录；只把已完成并实测的能力写入发行说明。

## 官方参考

- https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2environmentoptions6
- https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2profile7
- https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/browser-features
- https://learn.microsoft.com/en-us/microsoft-edge/extensions-chromium/developer-guide/migrate-your-extension-from-manifest-v2-to-v3
