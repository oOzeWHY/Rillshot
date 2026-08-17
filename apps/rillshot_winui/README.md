# Rillshot WinUI 3

这是 Rillshot 1.1.9 的 C++/WinRT 商业主界面。它直接连接共享 `CaptureController` 和 `CaptureSession`，不是演示壳层。

## 职责拆分

- `MainWindow.xaml.cpp`：窗口生命周期与初始化。
- `MainWindow.Navigation.cpp`：导航状态、最新意图、工作区/DPI 目标几何，以及刷新率感知、原子合并的窗口脉冲。
- `MainWindow.Navigation.Animation.cpp`：独立页面视口、目标尺寸预热、可复用合成模板、中心缩放和批次清理；合成器入口显式限定到 `Microsoft::UI::Xaml::Media`。
- `MainWindow.Preferences.cpp`：主题、全局快捷键和偏好交互。
- `MainWindow.PreferencePersistence.cpp`：代次合并、串行后台写入和关闭补写。
- `MainWindow.Selection.cpp`：区域、滚动点和固定页头选择。
- `MainWindow.Capture.cpp`：捕获命令与完成处理。
- `MainWindow.Presentation.cpp`：状态、焦点、文案与通知呈现。

核心捕获、拼接、输入和编码不能复制进表现层；后台回调通过 `DispatcherQueue` 返回 UI 线程。完整构建请从工程根目录运行 `build-release.cmd`。
