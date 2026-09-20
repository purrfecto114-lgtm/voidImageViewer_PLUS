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
稳定版二进制（安装包 + zip，x86/x64，附 SHA-256 校验和）：

https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/releases

二进制未签名（开源签名账户在路线图上）——运行前请对照发布页的 `sha256.txt` 校验每个下载文件。
每个发布资产都带有构建来源证明（build-provenance attestation）：`gh attestation verify <file> --repo purrfecto114-lgtm/voidImageViewer_PLUS`
可查出每个文件由哪次工作流运行、哪个提交构建而来（签名是另一轮的事——attestation 证明的是来源，不是发布者身份）。

最新动态
--------
**1.1.15-rc.9 — 内存与缓存轮（当前候选版）：**

- 图片缓存现在是可自定义张数的环形队列 — 设置 → 视图 → **缓存张数**（关闭 / 1–8 张）；往回翻无需重新加载。
- 预加载按自定义张数成链 — **预加载张数**（关闭 / 1–5 张）；链上的图片先进环，在你到达之前就绪。
- 缓存集合有自己的内存线（图像预算的一半），裁剪先丢最旧，动画不再为没人看的每一帧构建 mipmap 链 — 实测内存占用大幅下降。
- 修复最近打开在加载进行中被转发重开时列表自行重排的问题；重命名绑定屏幕上正在查看的文件。
- **从上次位置继续**（设置 → 常规）空白启动时重新打开上次会话的文件。

**1.1.15-rc.8 — 第七审计响应轮：** 剪贴板/身份/线程加固、双语 README 与发布链固定。详见 [Changes.txt](Changes.txt)。

**1.1.14 — 当前稳定版** —— 缓存集上限在每次填充点同时对三张持有图像计价（会溢出的预加载被静默放弃；结算点丢缓存，屏幕上的图永不丢），预加载/缓存默认值已作为守卫钉住。完整叙事：`Changes.txt`。

完整的轮次档案——更早的每个候选版本一行一条，加上超出轮次本身的教训——见 [experience.md](experience.md)（英文）；逐版本完整叙事见 [Changes.txt](Changes.txt)。

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

手势需要 Windows 7+ 与触控硬件。单指输入保持与鼠标兼容，配置的点击动作不受影响。通过 **查看 → 缩放控件** 切换浮动控件。捏合可以继续缩到窗口适应之下，最小约为适应/16（对应 16× 缩放上限）——选项中的 `允许缩小` 含义不变。

画布、背景与深色模式
--------

- **窗口 / 全屏背景色** —— 选项 → 查看，或查看菜单取色器；图像周围的衬边与空窗口画布。
- **透明背景衬底** —— 查看 → 透明背景：跟随窗口背景色、黑、白、自定义色或棋盘格。带 alpha 的图像（PNG/GIF/WEBP）在加载时合成在其上。它*不是*图像周围的画布——那个颜色是上面的窗口背景色。
- **深色界面规则** —— 浅色界面永远显示你选的精确颜色。深色界面下，浅色衬底保留色相但压入暗部区间（默认白映射到深色画布），自定义衬底同规则，没有任何颜色从深色边框里刺出来。Win11 标题栏着色跟随衬底。
- **深色界面是完整的** —— 对话框、选项页、菜单栏与导航树全部跟随主题（老系统上主题 API 不够的地方自绘补齐）；浅色对话框保持浅色。
- 深色模式本体：选项 → 常规 → 深色模式——浅色、深色或跟随 Windows。主题切换会重绘整个窗口并无条件重申深色边框（延迟复查可治愈系统侧的浅色重绘），打开的对话框实时换肤。

最近文件
--------
文件 → 最近 保存最近打开的十条路径（大小写不敏感去重；缺失文件在下一次打开时淘汰；清除 即清空）。列表在所有路径上都封顶十条，写入在打开路径上防抖。

语言
--------
内置英语与简体中文。

- 安装包在第一页选择语言。
- **选项 → 常规 → 语言** 即时切换 自动 / English / 简体中文（无需重启）。
- 以 `language=auto|english|chinese` 存于 `voidImageViewer.ini`；静默安装可传 `/language english|chinese|auto`。

从源码构建
--------
纯 C + Win32 API，Visual Studio：

1. 打开 `vs2019/voidImageViewer.sln`（VS2022+，v143 工具集）或 `vs2026/voidImageViewer.sln`（v145 工具集）。两者共享同一份文件列表（`voidImageViewer.files.props`）。VS2019 可用 `/p:PlatformToolset=v142`（CI 不覆盖）。
2. 构建 `voidImageViewer` 项目（x64 或 Win32）。
3. 可选安装包：NSIS 3，经 `nsis\build_installer.ps1`（自动探测 VS 版本；源码以 `/utf-8` 编译）。

zig 交叉构建无需 Visual Studio：`sh build-zig/build.sh`（104 个翻译单元，`-Os` 下 x64 约 480 KB）。VS 链接器内嵌 `res/voidImageViewer.Manifest`（per-monitor v2）；zig 构建不内嵌清单，而是在 exe 旁写出 `viv.exe.manifest`，`os_init` 的运行时声明覆盖连被剥离的副本——两个文件请放在一起，需要单文件二进制时请走 VS 构建。

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
