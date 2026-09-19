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
**1.1.15-rc.8 — 第七次审计响应轮（当前候选版本）：**

- **剪贴板粘贴不再越界读取** —— 粘贴 CF_DIB 时先证明全局内存的长度，再读任何头字段；先证明整张位图（掩码、调色板、像素）存在，再创建 DIB 节；stride 全程走防溢出算术（宽度×位深乘积回绕或 `INT_MIN` 高度都会落到纯位图回退路径，而不会喂给尺寸检查）。
- **纯位图粘贴与解码器同守一个像素预算** —— GB 级的 CF_BITMAP 在 `CopyImage` 分配之前就被拒绝。
- **破坏性命令绑定屏幕上的图像** —— 删除、复制、移动到、旋转与 Shell 动词改读显示槽位的文件，加载途中"下一张+删除"再也不会落在尚未显示的文件上（导航链仍从最新请求续链）。
- **跨线程状态改用正规形式** —— 预加载标志在线程启动时取不可变任务快照，阶段标记改用 Interlocked 指针形式，回复队列的尾部补发也会交还被拒投递的责任（rc.5 修的是入队一侧）。
- **设置保存不再吞错** —— 任何一次写入被拒或写短都会中止替换、保住上一份完好的 ini；替换以 write-through 落盘并先行 flush。
- **构建与发布可信度** —— zig 构建每次清空对象目录（不再有幽灵对象）、CI 的 `actions:write` 只授予唯一需要上传的 job、发布按 tag 串行、NSIS 固定 3.12.0、双语编码检查在 CI 中只校验不修改、`sha256.txt` 纳入 attestation 范围、安装脚本参数白名单化。
- 完整叙事：`Changes.txt`。

**1.1.15-rc.7 — 重入状态轮** —— 重复打开不再丢窗口尺寸：状态感知激活、最小化零尺寸扫描、placement 支撑的几何读取。完整叙事：`Changes.txt`。

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
