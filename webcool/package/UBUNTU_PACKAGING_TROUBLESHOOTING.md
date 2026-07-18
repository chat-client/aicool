# WebCool Ubuntu 安装包构建与 AI 运行时排障

本文记录 WebCool 在 Ubuntu 上构建主安装包和 AI 安装包时实际遇到的问题、原因、解决办法及安装后的验证方式。

文中的命令默认在 `aicool` 项目根目录执行；特别注明进入 `webcool/package` 的命令除外。

## 1. 安装包结构

Ubuntu 打包脚本默认生成两个相互独立的 DEB 包：

- `webcool_<version>-<release>_<arch>.deb`：主程序、网页资源、SQLite、FFmpeg 和默认功能。
- `webcool-ai-models_<version>-<release>_<arch>.deb`：CodeFormer、Real-ESRGAN 运行时及模型。

AI 模型统一放在项目根目录 `models/`，平台运行程序和 Python 环境放在 `tools/`：

```text
aicool/
├── models/
│   ├── codeformer/weights/
│   └── realesrgan/ncnn/
├── tools/
│   ├── codeformer/
│   │   ├── CodeFormer/
│   │   └── venv/linux/
│   └── linux/
│       ├── ffmpeg
│       └── realesrgan-ncnn-vulkan
└── webcool/package/
    └── build-deb.sh
```

不要把 macOS 或 Windows 创建的 `venv` 复制到 Ubuntu。Python、PyTorch、OpenCV 和动态库都包含平台及 CPU 架构相关文件，Ubuntu 必须在目标平台创建 `tools/codeformer/venv/linux`。

Restormer 当前只有 macOS Core ML 运行时，因此 Ubuntu AI 包不要求 `models/restormer/coreml`，也不会打包 Swift/Core ML 文件。

## 2. 推荐的完整构建流程

### 2.1 安装系统工具

```bash
sudo apt update
sudo apt install -y build-essential python3 python3-venv python3-pip dpkg-dev
```

如需运行 Intel/AMD Vulkan 版 Real-ESRGAN，还需要 Vulkan 工具和 Mesa 驱动：

```bash
sudo apt install -y vulkan-tools mesa-vulkan-drivers
```

### 2.2 下载并验证 AI 模型

```bash
python3 tools/codeformer/download_codeformer_models.py
python3 tools/download_realesrgan_models.py
```

两个脚本均会校验文件大小和 SHA-256。CodeFormer 下载完成后会创建：

```text
tools/codeformer/CodeFormer/weights -> ../../../models/codeformer/weights
```

该链接只用于源码目录开发。打包时脚本会把真实模型文件复制进 DEB，不依赖安装目标机器保留此链接。

### 2.3 创建 Ubuntu CodeFormer 环境

```bash
bash tools/codeformer/setup_codeformer_runtime.sh
```

成功后应存在：

```text
tools/codeformer/venv/linux/bin/python3
```

可手工验证：

```bash
cd tools/codeformer/CodeFormer
../venv/linux/bin/python3 -c \
  'import cv2, torch, torchvision, basicsr; from facelib.utils.face_restoration_helper import FaceRestoreHelper; print(torch.__version__)'
cd ../../..
```

### 2.4 检查 Real-ESRGAN 文件

```bash
test -x tools/linux/realesrgan-ncnn-vulkan
test -f models/realesrgan/ncnn/realesrgan-x4plus.param
test -f models/realesrgan/ncnn/realesrgan-x4plus.bin
```

如果可执行文件没有执行权限：

```bash
chmod 0755 tools/linux/realesrgan-ncnn-vulkan tools/linux/ffmpeg
```

### 2.5 构建两个 DEB 包

```bash
cd webcool/package
./build-deb.sh --version 2.0.0 --release 1
```

也可以分别构建：

```bash
./build-deb.sh --version 2.0.0 --release 1 --main-only
./build-deb.sh --version 2.0.0 --release 1 --ai-only --skip-build
```

### 2.6 安装

先安装主包，再安装 AI 包：

```bash
sudo dpkg -i deb/webcool_2.0.0-1_amd64.deb
sudo dpkg -i deb/webcool-ai-models_2.0.0-1_amd64.deb
```

如果 `dpkg` 报依赖未满足：

```bash
sudo apt-get -f install
```

## 3. CodeFormer 安装错误

### 3.1 `ModuleNotFoundError: No module named 'torch'`

典型错误：

```text
Processing ./basicsr
Getting requirements to build wheel ... error
ModuleNotFoundError: No module named 'torch'
```

原因是 `pip` 构建 BasicSR wheel 时启用了隔离构建环境。虽然当前虚拟环境已经安装 PyTorch，隔离环境中仍然没有 `torch`，而 BasicSR 的构建脚本导入了它。

当前解决办法是不再把 CodeFormer 自带的 BasicSR 作为 wheel 安装。BasicSR 和 FaceLib 已包含在 `tools/codeformer/CodeFormer` 中，运行器通过 `PYTHONPATH` 直接导入源码。

不要再手工执行：

```bash
pip install ./basicsr
```

应重新运行统一安装脚本：

```bash
bash tools/codeformer/setup_codeformer_runtime.sh
```

### 3.2 `ModuleNotFoundError: No module named 'basicsr.version'`

典型错误：

```text
File ".../CodeFormer/basicsr/__init__.py", line 11, in <module>
    from .version import __gitsha__, __version__
ModuleNotFoundError: No module named 'basicsr.version'
```

CodeFormer 源码中的 `basicsr/version.py` 通常由安装过程生成。取消 BasicSR wheel 安装后，源码目录必须主动生成这个文件。

当前 `setup_codeformer_runtime.sh` 会自动执行：

```bash
python3 tools/codeformer/prepare_codeformer_source.py
```

如果使用的是较早检出的代码，可以先单独执行该命令，再重建虚拟环境。

### 3.3 Python 3.12 安装了大量 NVIDIA 包，但仍使用 CPU

在 Linux 上安装 PyTorch 时可能看到：

```text
nvidia-cublas-cu12
nvidia-cudnn-cu12
nvidia-cuda-runtime-cu12
```

这些是 PyTorch CUDA 运行依赖，不代表机器一定能使用 CUDA。CUDA 需要 NVIDIA GPU 和可用驱动。Intel UHD 620 不能通过 `torch.cuda` 使用这些库，因此 CodeFormer 会自动回退 CPU。

使用源码环境检查：

```bash
tools/codeformer/venv/linux/bin/python3 -c \
  'import torch; print("torch:", torch.__version__); print("cuda build:", torch.version.cuda); print("cuda available:", torch.cuda.is_available()); print("devices:", torch.cuda.device_count())'
```

使用安装包环境检查：

```bash
/opt/soft/webcool/codeformer/venv/bin/python3 -c \
  'import torch; print(torch.__version__, torch.version.cuda, torch.cuda.is_available(), torch.cuda.device_count())'
```

- NVIDIA GPU：驱动和 CUDA 版 PyTorch匹配时，官方 CodeFormer 会自动选择 CUDA。
- AMD GPU：需要兼容的 ROCm PyTorch；普通 CUDA wheel 无法使用 AMD GPU。
- Intel UHD 620：当前 Python CodeFormer 路径只能使用 CPU。
- Apple Silicon：部分 CodeFormer 功能优先使用项目中的原生 Core ML 运行时。

Intel GPU 加速需要新增 OpenVINO IR/FP16 后端和常驻推理进程，不能只增加一个 `--gpu` 参数解决。该功能目前属于后续优化项，不应把 CPU 回退误认为打包失败。

## 4. Real-ESRGAN 打包错误

### 4.1 已有 `tools/linux/realesrgan-ncnn-vulkan`，仍提示运行时缺失

典型错误：

```text
[package] warning: Real-ESRGAN runtime not found
[package] error: incomplete Linux AI package payload; missing bin/realesrgan-ncnn-vulkan
```

检查以下事项：

```bash
ls -l tools/linux/realesrgan-ncnn-vulkan
file tools/linux/realesrgan-ncnn-vulkan
test -x tools/linux/realesrgan-ncnn-vulkan
```

当前打包脚本从 `tools/linux` 选择 Linux 可执行文件，并在暂存目录中统一设置 `0755`。源码文件至少必须存在且可读取；建议也保留执行权限。

还要确认可执行文件与目标架构一致。例如 amd64 包不能包含 ARM64 可执行文件：

```bash
dpkg --print-architecture
file tools/linux/realesrgan-ncnn-vulkan
```

### 4.2 `missing models/realesrgan`

典型错误：

```text
[package] error: incomplete Linux AI package payload; missing models/realesrgan
```

模型已经从 `tools/` 移到项目根目录 `models/`，macOS、Ubuntu 和 Windows 共用相同的 NCNN 模型数据。Ubuntu 新检出的代码库中如果只有 CodeFormer 模型，需要执行：

```bash
python3 tools/download_realesrgan_models.py
```

打包前至少要有：

```text
models/realesrgan/ncnn/realesrgan-x4plus.param
models/realesrgan/ncnn/realesrgan-x4plus.bin
models/realesrgan/ncnn/realesr-animevideov3-x2.param
models/realesrgan/ncnn/realesr-animevideov3-x2.bin
models/realesrgan/ncnn/realesr-animevideov3-x3.param
models/realesrgan/ncnn/realesr-animevideov3-x3.bin
models/realesrgan/ncnn/realesr-animevideov3-x4.param
models/realesrgan/ncnn/realesr-animevideov3-x4.bin
```

下载脚本会从官方发布包提取文件并校验 SHA-256，已有且校验正确的文件不会重复下载。

## 5. 安装后提示 Real-ESRGAN 未安装

典型错误：

```text
画质提升失败：selected Real-ESRGAN runtime or model is not installed
```

首先确认两个 DEB 都已安装：

```bash
dpkg -l webcool webcool-ai-models
```

确认安装内容：

```bash
test -x /opt/soft/webcool/bin/realesrgan-ncnn-vulkan
test -f /opt/soft/webcool/models/realesrgan/realesrgan-x4plus.param
test -f /opt/soft/webcool/models/realesrgan/realesrgan-x4plus.bin
```

早期版本的前端会在非 macOS 平台默认提交 `coreml-*` 模型，导致 Linux 后端找不到模型。当前版本已做两层修复：

- Ubuntu/Windows 页面不再显示和默认选择 Core ML 模型。
- 非 Apple Silicon 后端收到旧的 `coreml-*` 参数时会回退到 `realesrgan-x4plus`。

如果文件均存在但仍报错，应确认正在运行的是新安装的二进制，而不是开发目录中的旧进程：

```bash
readlink -f "$(command -v webcool)"
ps -ef | grep '[w]ebcool'
```

重启 WebCool 后重新测试。

## 6. Ubuntu 无法使用 GPU 的分析与解决

WebCool 的 AI 功能并不共用同一个 GPU 后端。开始排障前应先确认是哪个功能没有使用 GPU：

| 功能 | Ubuntu 后端 | 可直接使用的 GPU |
| --- | --- | --- |
| Real-ESRGAN 图片/视频超分 | NCNN + Vulkan | Intel、AMD、NVIDIA Vulkan GPU |
| CodeFormer 人脸修复/重建 | Python + PyTorch | 当前主要是 NVIDIA CUDA；兼容的 ROCm 环境可使用 AMD |
| FFmpeg 最终视频编码 | FFmpeg 编码器 | 与 AI 推理相互独立，取决于 VAAPI/QSV/NVENC 等编码器 |
| Core ML、Swift 运行程序 | Apple Core ML | 仅 macOS，不适用于 Ubuntu |

因此，`intel_gpu_top` 能看到 Real-ESRGAN，并不意味着 CodeFormer 也能使用 Intel GPU；`nvidia-smi` 能识别显卡，也不表示 Vulkan 或当前 PyTorch 环境已经正确安装。

### 6.1 第一步：识别显卡、内核驱动和运行用户

```bash
lspci -nnk | grep -EA3 'VGA|3D|Display'
lsmod | grep -E 'i915|amdgpu|nouveau|nvidia'
ls -l /dev/dri
id
```

常见的正确内核驱动如下：

- Intel 核显：`i915` 或较新平台的 `xe`。
- AMD 显卡：`amdgpu`。
- NVIDIA 显卡：发行版安装的专有 `nvidia` 驱动；`nouveau` 通常不适合 CUDA。

Intel/AMD 通常应存在 `/dev/dri/renderD128`。多显卡机器还可能出现 `renderD129` 等设备。

WebCool 子进程继承 WebCool 服务的用户和用户组。不要只检查当前登录用户，还要检查实际运行 WebCool 的用户：

```bash
ps -eo user,group,pid,cmd | grep '[w]ebcool'
```

如果由 systemd 启动，还应查看服务配置中的 `User=` 和 `Group=`：

```bash
systemctl cat webcool 2>/dev/null
systemctl status webcool --no-pager 2>/dev/null
```

服务名称不是 `webcool` 时，替换为实际 unit 名称。

### 6.2 修复 `/dev/dri` 权限

运行 WebCool 的用户通常需要加入 `render` 和 `video` 组：

```bash
sudo usermod -aG render,video <webcool-user>
```

如果 WebCool 就由当前登录用户启动，可执行：

```bash
sudo usermod -aG render,video "$USER"
```

修改组后必须重新登录或重启。对于 systemd 服务，还需要重启服务：

```bash
sudo systemctl restart webcool
```

不要长期使用 `sudo webcool` 绕过权限；这会改变配置、上传文件和临时目录的所有者，并掩盖真正的服务用户配置问题。

验证实际运行用户是否能访问 GPU：

```bash
sudo -u <webcool-user> test -r /dev/dri/renderD128
sudo -u <webcool-user> vulkaninfo --summary
```

如果设备编号不是 `renderD128`，请替换为实际设备。

### 6.3 安装 Vulkan Loader、驱动和诊断工具

Intel 和 AMD 使用 Mesa Vulkan 驱动：

```bash
sudo apt update
sudo apt install -y libvulkan1 vulkan-tools mesa-vulkan-drivers
```

NVIDIA 应通过 Ubuntu 的“附加驱动”或适合当前 Ubuntu 版本的发行版驱动包安装，不要把其他 Ubuntu 版本的驱动文件直接复制过来。安装后检查：

```bash
nvidia-smi
vulkaninfo --summary
```

查看 Vulkan ICD 文件：

```bash
ls -l /usr/share/vulkan/icd.d /etc/vulkan/icd.d 2>/dev/null
```

通常会看到与硬件匹配的 Intel、Radeon 或 NVIDIA JSON 文件。如果目录中只有软件渲染 ICD，Vulkan 会回退到 llvmpipe。

还应检查是否设置了错误的 Vulkan 环境变量：

```bash
env | grep -E '^VK_' || true
```

错误的 `VK_ICD_FILENAMES`、`VK_DRIVER_FILES` 或 `VK_LAYER_PATH` 可能强制加载不存在的驱动或软件设备。除非明确需要指定 ICD，建议先取消这些自定义变量再测试：

```bash
unset VK_ICD_FILENAMES VK_DRIVER_FILES VK_LAYER_PATH
vulkaninfo --summary
```

需要查看 Vulkan Loader 具体加载了哪些驱动时：

```bash
VK_LOADER_DEBUG=all vulkaninfo --summary 2>&1 | less
```

### 6.4 Vulkan 只识别到 llvmpipe

如果 `vulkaninfo --summary` 显示：

```text
deviceType = PHYSICAL_DEVICE_TYPE_CPU
deviceName = llvmpipe
```

说明 Real-ESRGAN 虽然使用 Vulkan，但实际走 Mesa 软件渲染，仍由 CPU 计算。依次检查：

```bash
lspci -nnk | grep -EA3 'VGA|3D|Display'
ls -l /dev/dri
id
ls -l /usr/share/vulkan/icd.d
```

Intel UHD 620 应使用 `i915`，并存在 `/dev/dri/renderD128`。WebCool 运行用户还需要访问 `render` 和 `video` 组：

```bash
sudo usermod -aG render,video "$USER"
```

执行后必须注销并重新登录，或重启系统。然后再次检查：

```bash
vulkaninfo --summary
```

正确结果应显示 Intel 物理 GPU，而不是 llvmpipe。

如果 `lspci` 和 `/dev/dri` 正常，但 `vulkaninfo` 仍只有 llvmpipe，常见原因是：

- `mesa-vulkan-drivers` 没有安装或文件损坏。
- WebCool 服务用户没有 `render` 组权限。
- 自定义 `VK_*` 环境变量选择了错误 ICD。
- 容器没有映射 `/dev/dri`。
- 虚拟机没有启用 GPU 直通，来宾系统只能看到虚拟或软件显卡。
- SSH/无桌面环境本身不是问题；Vulkan 计算不要求打开图形桌面，但仍要求 DRM 设备和驱动可用。

### 6.5 多 GPU 机器选择错误

先列出 Vulkan 设备：

```bash
vulkaninfo --summary
```

Real-ESRGAN 的 `-g` 参数选择 Vulkan GPU 编号。WebCool 高级选项中的“GPU编号”与此对应：

```text
-1：自动选择
 0：GPU 0
 1：GPU 1
```

如果自动模式选择到低性能核显，可明确选择独立 GPU。选择不存在的编号会出现 `invalid gpu device`，此时应恢复自动或使用 `vulkaninfo` 中的实际编号。

可以用一张小图直接测试安装后的运行时，排除 WebCool 页面参数影响：

```bash
/opt/soft/webcool/bin/realesrgan-ncnn-vulkan \
  -i /path/to/input.jpg \
  -o /tmp/realesrgan-test.png \
  -n realesrgan-x4plus \
  -m /opt/soft/webcool/models/realesrgan \
  -g 0 -t 128 -v
```

### 6.6 确认 Real-ESRGAN 是否真的使用 GPU

处理视频时可使用下面的命令观察 Intel GPU：

```bash
sudo apt install -y intel-gpu-tools
sudo intel_gpu_top
```

如果 `Render/3D` 接近 100%，且进程名是 `realesrgan-ncnn`，说明 GPU 已经被正确使用。

NVIDIA 可使用：

```bash
watch -n 1 nvidia-smi
```

AMD 可安装并使用：

```bash
sudo apt install -y radeontop
sudo radeontop
```

同时出现较高 CPU 占用不代表 GPU 没有使用。图片解码、PNG/JPEG 写入、视频拆帧、最终 H.264 编码和磁盘 I/O 仍可能由 CPU 完成。

### 6.7 CodeFormer 没有使用 GPU

CodeFormer 使用 PyTorch，而不是 Real-ESRGAN 的 Vulkan。先用安装包内的 Python 检查：

```bash
/opt/soft/webcool/codeformer/venv/bin/python3 -c '
import torch
print("torch:", torch.__version__)
print("CUDA build:", torch.version.cuda)
print("CUDA available:", torch.cuda.is_available())
print("CUDA devices:", torch.cuda.device_count())
print("cuDNN available:", torch.backends.cudnn.is_available())
if torch.cuda.is_available():
    print("GPU:", torch.cuda.get_device_name(0))
'
```

判断方法：

- `torch.version.cuda` 为 `None`：安装的是 CPU 版 PyTorch。
- `torch.version.cuda` 有值但 `torch.cuda.is_available()` 为 false：常见原因是没有 NVIDIA GPU、驱动不可用、驱动与 CUDA wheel 不兼容，或容器没有映射 GPU。
- `torch.cuda.is_available()` 为 true：官方 CodeFormer 会自动选择 CUDA，不需要 WebCool 再传 GPU 开关。
- Intel UHD 620：CUDA 不支持 Intel GPU，当前 CodeFormer 会使用 CPU；Real-ESRGAN 可以使用 Intel Vulkan 并不能改变这一点。
- AMD：普通 CUDA wheel 不能使用 AMD GPU，需要与系统和显卡兼容的 ROCm PyTorch 环境。

NVIDIA 机器应同时检查宿主机：

```bash
nvidia-smi
ls -l /dev/nvidia* 2>/dev/null
```

如果 `nvidia-smi` 本身失败，应先修复驱动；如果它成功而 PyTorch 检测失败，再检查 PyTorch CUDA 版本和驱动兼容性。

Intel GPU 上的 CodeFormer 需要新增 OpenVINO IR/FP16 推理后端。当前安装包尚未提供该后端，因此不能通过安装 CUDA 包或设置 `-g 0` 解决。

### 6.8 Docker、容器和虚拟机

容器中的 Intel/AMD Vulkan 至少需要映射 DRM 设备，并让容器用户拥有对应权限，例如：

```bash
docker run --device=/dev/dri:/dev/dri ...
```

NVIDIA 容器需要正确安装并配置 NVIDIA Container Toolkit。仅在宿主机安装驱动但不向容器暴露 GPU，容器内的 PyTorch 仍会返回 CUDA 不可用。

虚拟机默认通常不能直接使用宿主机物理 GPU。必须配置受支持的 GPU 直通或虚拟 GPU；否则看到 llvmpipe 属于预期结果。

### 6.9 最小诊断信息清单

仍无法判断原因时，保存以下命令的完整输出：

```bash
uname -a
lsb_release -a 2>/dev/null
lspci -nnk | grep -EA3 'VGA|3D|Display'
ls -l /dev/dri /dev/nvidia* 2>/dev/null
id
ps -eo user,group,pid,cmd | grep '[w]ebcool'
vulkaninfo --summary
env | grep -E '^VK_' || true
nvidia-smi 2>/dev/null || true
/opt/soft/webcool/codeformer/venv/bin/python3 -c \
  'import torch; print(torch.__version__, torch.version.cuda, torch.cuda.is_available(), torch.cuda.device_count())'
```

其中 `vulkaninfo` 用于 Real-ESRGAN，PyTorch 检查用于 CodeFormer，二者不能互相替代。

## 7. GPU 已到 100%，AI 超分仍然很慢

Intel UHD 620 的 Vulkan GPU 算力和内存带宽远低于 Apple M 系列 Neural Engine 或较新的独立 GPU。`realesrgan-x4plus` 又是当前 NCNN 包中最重的真人模型，因此 GPU 100% 表示计算单元已经饱和，不表示速度应当很快。

当前视频处理流程为：

1. FFmpeg 解码视频并生成图片帧。
2. `realesrgan-ncnn-vulkan` 对图片逐帧推理。
3. FFmpeg读取增强帧、缩放到目标尺寸、编码 H.264 并封装原音轨。

NCNN 命令行工具只接受图片或图片目录，暂时不能直接接收 FFmpeg 管道，因此中间帧流程仍然必要。

当前版本已经包含以下优化：

- “极速”输入尺寸会按目标分辨率和模型倍数预缩小推理输入。
- “均衡”输入尺寸会把宽高缩小到源画面的 75%。
- “快速”档使用高质量 JPEG 中间帧，降低磁盘 I/O。
- “快速”档最终使用 x264 `veryfast`。
- NCNN 后端的“每 2 帧/每 3 帧推理”已经真正生效，并复用相邻增强帧。
- 10/30/60 秒试跑按实际片段时长计算进度。

UHD 620 推荐先使用：

```text
性能档位：快速
推理输入尺寸：极速
时序复用：每 2 帧推理
Tile：256（显存不足时改为 128）
推理线程：4:4:4
最终编码速度：Fast
试跑范围：前 10 秒
```

仍然太慢时可改为“每 3 帧推理”，代价是运动画面可能不够连贯。动漫视频可选择轻量的 `realesr-animevideov3`；它不适合真人照片和真人视频。

## 8. 验证 DEB 内容

构建完成后，不安装也可以检查包内容：

```bash
dpkg-deb -c webcool/package/deb/webcool_2.0.0-1_amd64.deb | less
dpkg-deb -c webcool/package/deb/webcool-ai-models_2.0.0-1_amd64.deb | less
```

主包不应包含大型 AI 模型。AI 包至少应包含：

```text
/opt/soft/webcool/bin/realesrgan-ncnn-vulkan
/opt/soft/webcool/models/realesrgan/realesrgan-x4plus.param
/opt/soft/webcool/models/realesrgan/realesrgan-x4plus.bin
/opt/soft/webcool/codeformer/CodeFormer/inference_codeformer.py
/opt/soft/webcool/codeformer/CodeFormer/weights/CodeFormer/codeformer.pth
/opt/soft/webcool/codeformer/CodeFormer/weights/CodeFormer/codeformer_inpainting.pth
/opt/soft/webcool/codeformer/venv/bin/python3
/opt/soft/webcool/libexec/codeformer_runner.py
```

检查包依赖和架构：

```bash
dpkg-deb -f webcool/package/deb/webcool_2.0.0-1_amd64.deb Package Version Architecture Depends
dpkg-deb -f webcool/package/deb/webcool-ai-models_2.0.0-1_amd64.deb Package Version Architecture Depends
```

安装后进行最终检查：

```bash
/opt/soft/webcool/bin/realesrgan-ncnn-vulkan -h
/opt/soft/webcool/codeformer/venv/bin/python3 -c \
  'import cv2, torch, torchvision, basicsr; print("CodeFormer runtime OK", torch.__version__)'
```

## 9. 快速排障清单

打包失败时按下面的顺序检查：

1. 当前目录是否为 `aicool` 项目根目录，目录结构是否完整。
2. `tools/linux/realesrgan-ncnn-vulkan` 是否存在且架构正确。
3. 是否执行了两个模型下载脚本，所有权重是否校验成功。
4. 是否在当前 Ubuntu 机器创建了 `tools/codeformer/venv/linux`。
5. CodeFormer 的 `cv2`、`torch`、`torchvision`、`basicsr`、`facelib` 是否可导入。
6. 是否同时生成并安装主包和 AI 包。
7. 运行中的 WebCool 是否为新安装版本。
8. `vulkaninfo` 是否识别真实 GPU，而非 llvmpipe。
9. 实际运行 WebCool 的服务用户是否有 `render`、`video` 或 NVIDIA设备权限。
10. CodeFormer 的 `torch.cuda.is_available()` 是否符合实际硬件类型。
11. Intel GPU 已满载但仍慢时，应降低推理输入或启用时序复用，而不是重复安装 Vulkan。
