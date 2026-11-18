# 1.1.18-rc.3 验证记录

## 改动

- 自绘工具栏按当前 DPI 和实际剩余宽度布局。空间不足时先使用图标，进一步缩窄后把次要命令收进「更多」。窗口和悬浮控制条均可切换全屏。
- 状态栏左侧显示文件名和图片信息。点击文件名打开并定位原文件。Shell 定位失败时会打开它的父目录，包括盘符根目录。
- 旋转在系统临时目录中保留一个原文件副本和当前旋转预览 PNG。成功后提交更新后的显示帧，失败时保留本次旋转前的显示。临时文件随离开图片或退出释放。动图预览旋转所有已加载的帧，另存为导出当前帧，并建议带有 `-rotated` 的新文件名。
- 队列从空白和加载失败状态继续工作。重复「加入文件」复用现有条目，同名文件按完整路径识别。快速换向时取消的解码不会覆盖最新请求，预加载也不会替换显式打开。
- 渲染器提供 GDI、D3D9、D3D11 和 D3D12（11On12）。旧 Direct3D 配置继续选择 D3D9，旧 OpenGL 配置迁移到 GDI。D3D9 预算计入后备缓冲区，现代渲染器复用图像纹理，并在可用时查询 DXGI 显存预算。

Microsoft 文档规定从 `WM_DPICHANGED` 读取新 DPI，并按其建议矩形重新调整窗口。100% 对应 96 DPI，因此 Windows 的 300% 是 288 DPI。布局验证覆盖了 288 和 300 两档。[WM_DPICHANGED](https://learn.microsoft.com/en-us/windows/win32/hidpi/wm-dpichanged)

D3D12 路径使用 D3D12 设备、队列和交换链，共用 D3D11 着色代码。WARP 用于可复现的软件渲染检查。[D3D11On12](https://learn.microsoft.com/en-us/windows/win32/direct3d12/direct3d-11-on-12) · [DXGI 显存预算](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgiadapter3-queryvideomemoryinfo) · [WARP](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/directx-warp)

## 已完成

| 项目 | 验证结果 |
| --- | --- |
| TDD 复现 | 旧行为先在工具栏裁切、队列不可达、旋转确认、4K 缓冲预算、快速 A → B → A 和文件定位用例中失败，修复后通过 |
| 工具栏与状态栏 | 执行生产 C 布局代码，覆盖 6 档 DPI、7,116 组工具栏条件和 4,688 组状态栏条件 |
| 回归 | Python 套件、C/UBSan 回归、NSIS 架构规则、libwebp 依赖闭包、空白检查和 GitHub Actions 静态校验通过。故障注入检出 134 / 134 个回归变体 |
| D3D 内存与像素输入 | C 故障测试覆盖 D3D9 预算和分配失败、D3D11/12 的 24/32 位行距、资源复用和上传失败后的恢复 |
| 便携构建 | LLVM-MinGW 20261104 交叉构建 x86、x64 和 ARM64。通过 PE 架构、GUI 子系统、嵌入的 Manifest 和版本资源、LAA、ASLR、DEP 与 CFG 校验 |
| 图像复核 | [300 DPI 布局图](previews/rc3-300dpi-layout.png) 取自生产 C 的绘制坐标，含恢复窗口与溢出菜单 |

便携程序可从 `portable/x64/voidImageViewer.exe`、`portable/x86/voidImageViewer.exe` 或 `portable/arm64/voidImageViewer.exe` 启动。它们是未签名的交叉构建，校验和在 `portable/SHA256SUMS.txt`。

## Windows 运行验证

本地环境为 Linux，Wine 初始化受本地套接字权限限制。Windows GUI、WARP 像素回读、显卡驱动和真实 4K／300 DPI 屏幕上的结果均待验证。

- `tests/gui_rc3.ps1` 使用 Windows 程序实际操作旋转、队列、更多菜单和全屏，在 96 与 300 DPI 消息下截图。它另外检查 D3D9、D3D11 WARP 和 D3D12 WARP 的窗口呈现与缩放。
- `tests/render_golden.ps1` 调用生产渲染导出，比较 13 张图的 GDI／D3D9 基准，并分别检查 D3D11 和 D3D12 的实际像素。D3D 路径回退会使测试失败。传 `-Hardware` 可使用物理适配器。
- 上述脚本已接入 CI。渲染输出和窗口截图会作为运行证据上传。
