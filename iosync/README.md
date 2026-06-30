# iosync

`iosync` 是一个 C++ 命令行工具，用于从 USB 连接的 iPhone/iPad 中列出并导出系统“照片”里的照片和视频。

重要限制：

- iOS 不会通过 USB 暴露完整文件系统，本工具只能访问系统照片库/DCIM 媒体对象。
- 使用前请解锁 iPhone，并在手机上点击“信任此电脑”。
- iCloud 优化存储的原片如果没有下载到手机，本地传输接口可能拿不到完整文件。
- macOS 后端使用 `ImageCaptureCore`；Windows 后端使用 Windows Portable Devices。

## 编译

macOS:

```bash
cmake -S iosync -B iosync/build
cmake --build iosync/build
```

Windows, Developer PowerShell:

```powershell
cmake -S iosync -B iosync\build -G "Visual Studio 17 2022" -A x64
cmake --build iosync\build --config Release
```

Linux/其他系统会编译出一个说明不支持的平台占位程序。

## 目录结构

- `include/`：公共接口。
- `src/main.cpp`、`src/media_item.cpp`：平台无关的命令行和工具函数。
- `src/macos/`：macOS 后端，使用 `ImageCaptureCore`。
- `src/windows/`：Windows 后端，使用 Windows Portable Devices。
- `src/unsupported/`：非 macOS/Windows 平台的占位实现。

## 使用

列出已连接的设备：

```bash
iosync devices
```

列出第一台设备上的照片和视频：

```bash
iosync list
```

macOS 上 `list` 会边枚举边输出；如果照片目录还没完全加载完，已经发现的项目会先打印出来。

导出全部照片和视频：

```bash
iosync export --all --out ./iphone-media
```

macOS 上 `export --all` 会边枚举边导出，每完成一个文件都会显示一行进度，包含时间、大小和文件名；`--verbose` 会额外显示 queued/completed/pending 调试进度。

按拍摄/修改时间排序后导出：

```bash
iosync export --all --sort-time --out ./iphone-media
```

`--sort-time` 需要先等完整照片目录加载完成，然后按时间从早到晚逐个导出。

导出指定对象：

```bash
iosync export --id 123456 --out ./iphone-media
```

指定设备：

```bash
iosync list --device 0
iosync export --device 0 --all --out ./iphone-media
```

`list` 输出为 TSV，列顺序是：

```text
id    kind    size    name
```
