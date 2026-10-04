# void Image Viewer（触控 + 多语言）

[![stable](https://img.shields.io/badge/status-stable-brightgreen.svg)](https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/releases)
[![release](https://img.shields.io/github/v/release/purrfecto114-lgtm/voidImageViewer_PLUS.svg?display_name=tag)](https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/releases)
[![license](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

[English](README.md) | **简体中文**

> [voidtools/voidImageViewer](https://github.com/voidtools/voidImageViewer) 的稳定分支，带**触控优化**、**屏幕缩放控件**、**完整深色界面**与**双语安装包 + 界面语言切换**。欢迎到 [issue 跟踪器](https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/issues) 反馈问题。

一个轻量级 Windows 看图软件（BMP、GIF、ICO、PNG、JPG、TIF、WEBP、JPEG-XR、HEIF、AVIF、DDS、QOI、EMF、WMF——含 GIF/WEBP 动图；JPEG-XR（Win7+）与 DDS（Win8.1+）走 Windows 自带的 WIC 编解码器，HEIF/AVIF 走系统商店的图像扩展（装了即支持），QOI 为内置解码），以尽可能快的速度打开并显示图片。

[下载](#下载) · [最新动态](#最新动态) · [触控与缩放](#触控与缩放控件) · [画布、背景与深色模式](#画布背景与深色模式) · [最近文件](#最近文件) · [语言](#语言) · [构建](#从源码构建)

下载
--------
稳定版二进制（安装包 + zip，x86/x64/arm64，附 SHA-256 校验和）：

https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/releases

二进制未签名（开源签名账户在路线图上）——运行前请对照发布页的 `sha256.txt` 校验每个下载文件。
每个发布资产都带有构建来源证明（build-provenance attestation）：`gh attestation verify <file> --repo purrfecto114-lgtm/voidImageViewer_PLUS`
可查出每个文件由哪次工作流运行、哪个提交构建而来（签名是另一轮的事——attestation 证明的是来源，不是发布者身份）。

最新动态
--------
**1.1.16 — 实机响应第四轮（当前稳定版）：**

- 稳定版在 build 113 重建。设置窗口在任何尺寸下都有求必应——内容行在固定页脚下滚动，命中矩形裁剪到绘制早已裁剪的同一视口带（过去被隐藏的行会吃掉“应用/取消/确定”的点击，还会透过注册表静默勾选看不见的复选框）；滚动停在行格点上，任何可达位置都不会把行切半截在标题带下；滚动条换上 12dip 宽的条带（不再被窗口缩放边带遮蔽），滑块用看得见的色调绘制，轨道按压按 Windows 惯例朝点击方向翻页。
- 你勾选的格式，直接关联——接管守卫把 jpg 家族的出厂属主拼错成了 `jpgfile`（Windows 实际出厂 `jpegfile`），于是每台原装机器都被读成“外来查看器”，勾选框静默弹回；表已修正，且每一次显式选择（单击、全部选中、安装向导的勾选）都直接接管，无论先前属主是谁。UserChoice 锁戴上挂锁标记并在网格下方配两行说明；锁定提示框完整拼出扩展名并深链到 Windows 默认应用页里本程序自己的那一行。
- 卸载不留残——失败存档临时文件双家清扫，真正为空的 OpenWithProgids 键删除，指向本程序已移除 ProgID 的 UserChoice 删除，扩展名自身的键在裸键或只是 HKLM 已有答案的冗余影子时一并删除，FileExts 父键与 RegisteredApplications 键同样遵守裸键规则，旧版自启动值也随卸载退役。你的设置得到该有的那一问——保留还是删除；默认保留，静默卸载携带保留词。
- 安装负载减掉四分之一兆字节——`Changes.txt` 留在仓库与发行说明里（升级仍会清掉旧版安装落盘的副本），应用列表显示实测体积。
- 硬件加速看机器的真话——能力探测（模块、导出、对象、caps，不建设备、不置粘滞失败）把守设置开关（不可用时灰显并说明）、菜单单选与安装器勾选；运行时 GDI 回退仍是安全网。快捷键页学会自我解释——常驻引导行（空闲流程与捕获态），按钮对选择负责，空的按键列表点击即开始添加，页面打开即显示命令的首个绑定。
- 缩放读数显示整数——快速连点落在几何阶梯无法精确命中的缩放对上（实测链 121、131、219）；显示值在两点容差内取整到最近的十倍数，百分之二十以下不取整，渲染位置本身从不动。

**1.1.15 — 上一稳定版** —— 缓存族上限、诚实的脸（双“打开方式”登记、UserChoice 询问）、每一帧都递送（多帧 HEIF/AVIF）、双向帧计数器、可定张数的缓存环与预加载链、窄窗口工具栏分页。完整叙事见 `Changes.txt`。

**1.1.16 弧线 —— 十二个候选版：** 第四次融合响应（僵尸类树倒下）、第五报告裁决、并行清扫、实机响应轮（毒探针退役、Windows 注册、硬件加速选项）、布局迁移（升级不再带出悬浮球）、实机响应第二轮（标题跟上图片、安装器提权诚实）、默认程序轮（一名到底、十九格式安装器）、文档轮、CodeQL 加固（五处 libwebp 加宽）、工具轮（套件自审）、审计响应（中继白名单与八项内存修复）、审查裁决（完成事件、进程号临时存档、安全求和）。完整叙事见 `Changes.txt`。

**1.1.15 弧线 —— 十九个候选版：** 完整叙事见 `Changes.txt`。

触控与缩放控件
--------

| 手势 / 控件 | 动作 |
| --- | --- |
| 双指捏合 | 放大 / 缩小（以手指中心为锚点） |
| 双指拖动 | 平移 / 滚动图像（带惯性） |
| 双指轻点 | 重置缩放 |
| 双击（触控） | 切换 1:1 / 最佳适应 |
| 工具栏缩放按钮 | 放大 / 缩小 |
| 浮动缩放条 | 两种模式共用一条七格控件行——上一张 / 播放-暂停 / 下一张 / 缩小 / 百分比 / 放大（底部居中；全屏时闲置淡出） |

手势需要 Windows 7+ 与触控硬件。单指输入保持与鼠标兼容，配置的点击动作不受影响。通过 **视图 → 显示悬浮控制条** 切换浮动控件。捏合可以继续缩到窗口适应之下，最小约为适应/16（对应 16× 缩放上限）——设置中的 `允许缩小` 含义不变。

画布、背景与深色模式
--------

- **窗口 / 全屏背景色** —— 设置 → 查看，或视图菜单取色器；图像周围的衬边与空窗口画布。
- **透明背景衬底** —— 视图 → 透明背景：跟随窗口背景色、黑、白、自定义色或棋盘格。带 alpha 的图像（PNG/GIF/WEBP）在加载时合成在其上。它*不是*图像周围的画布——那个颜色是上面的窗口背景色。
- **深色界面规则** —— 浅色界面永远显示你选的精确颜色。深色界面下，浅色衬底保留色相但压入暗部区间（默认白映射到深色画布），自定义衬底同规则，没有任何颜色从深色边框里刺出来。Win11 标题栏着色跟随衬底。
- **深色界面是完整的** —— 对话框、设置页与菜单栏全部跟随主题（老系统上主题 API 不够的地方自绘补齐）；浅色对话框保持浅色。
- 深色模式本体：设置 → 常规 → 主题——自动（跟随 Windows）、浅色或深色。主题切换会重绘整个窗口并无条件重申深色边框（延迟复查可治愈系统侧的浅色重绘），打开的对话框实时换肤。

最近文件
--------
文件 → 最近 保存最近打开的十条路径（大小写不敏感去重；缺失文件在下一次打开时淘汰；清除 即清空）。列表在所有路径上都封顶十条，写入在打开路径上防抖。

语言
--------
内置英语与简体中文。

- 安装包在第一页选择语言。
- **设置 → 常规 → 语言** 即时切换 自动 / English / 简体中文（无需重启）。
- 以 `language=auto|english|chinese` 存于 `voidImageViewer.ini`；静默安装可传 `/language english|chinese|auto`。

从源码构建
--------
纯 C + Win32 API，Visual Studio：

1. 打开 `vs2019/voidImageViewer.sln`（VS2022+，v143 工具集）或 `vs2026/voidImageViewer.sln`（v145 工具集）。两者共享同一份文件列表（`voidImageViewer.files.props`）。VS2019 可用 `/p:PlatformToolset=v142`（CI 不覆盖）。
2. 构建 `voidImageViewer` 项目（x64、Win32 或 ARM64）。
3. 可选安装包：NSIS 3，经 `nsis\build_installer.ps1`（自动探测 VS 版本；源码以 `/utf-8` 编译）。

zig 交叉构建无需 Visual Studio：`sh build-zig/build.sh`（104 个翻译单元，`-Os` 下 x64 约 480 KB）；`sh build-zig/build-arm64.sh` 为 aarch64 编译同一棵树。ARM64 发布产物本身是 MSVC v143 构建（`Platform=ARM64`）——zig 腿是证明树仍能为该目标翻译的编译门。VS 链接器内嵌 `res/voidImageViewer.Manifest`（per-monitor v2）；zig 构建不内嵌清单，而是在 exe 旁写出 `viv.exe.manifest`，`os_init` 的运行时声明覆盖连被剥离的副本——两个文件请放在一起，需要单文件二进制时请走 VS 构建。

源码布局：一个核心（`src/viv.c`——启动、命令行、拆卸）加十八个领域模块（`src/viv_<domain>.c/.h`）、解码器模块（`src/webp.c`、`src/qoi.c`、`src/wic.c`）与共享上下文头（`src/viv_state.h`）；见 `docs/architecture/viv-split-spec.md`。

GitHub Actions 编译每次推送（固定 `windows-2022`/v143 + `windows-2025`/v145 双腿）；tag 推送运行测试、端到端校验 SHA-256 并发布资产。

![Void Image Viewer Image View](https://www.voidtools.com/voidImageViewer.Image.View10.gif)

致谢
--------
上游：**voidtools / David Carpenter** —— [voidImageViewer](https://github.com/voidtools/voidImageViewer)，MIT。

本分支：原始分支、中文本地化与现代界面重写由 **hesphoros**（2026，见 git 历史），由现任维护者按同一 MIT 条款延续。完整的作者记录在仓库历史中；`THIRD_PARTY_NOTICES.md` 覆盖内置组件（含 Google 的 libwebp）。

另见
--------
上游项目：https://github.com/voidtools/voidImageViewer
