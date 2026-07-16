# webcool 二进制安装包构建说明

本目录提供四类安装包输出目录：

- `mac/`：macOS `.pkg` 文件
- `deb/`：Ubuntu/Debian `.deb` 文件
- `rpm/`：CentOS/RHEL `.rpm` 文件
- `windows/out/`：Windows ZIP 与 Inno Setup `.exe` 文件

## 打包脚本

- `build-mac.sh`：构建 macOS 安装包
- `build-deb.sh`：构建 deb 安装包
- `build-rpm.sh`：构建 rpm 安装包
- `windows/build-windows.ps1`：构建 Windows 主包与 AI 包
- `build-all.sh`：统一入口

## 使用示例

在 `webcool/package` 目录执行：

```bash
./build-mac.sh --version 1.0.0
./build-deb.sh --version 1.0.0 --release 1
./build-rpm.sh --version 1.0.0 --release 1
```

统一入口：

```bash
./build-all.sh --target mac --version 1.0.0
./build-all.sh --target deb --version 1.0.0 --release 1
./build-all.sh --target rpm --version 1.0.0 --release 1
```

## 依赖要求

- macOS：`pkgbuild`
- Ubuntu/Debian：`dpkg-deb`
- CentOS/RHEL：`rpmbuild`

macOS 默认生成两个独立安装包：

- `webcool-<version>-macos-<arch>.pkg`：主 WebCool 包，包含主程序、网页资源、SQLite、FFmpeg 和默认功能。
- `webcool-ai-models-<version>-macos-<arch>.pkg`：可选 AI 包，包含 CodeFormer 独立运行环境、Core ML、Real-ESRGAN、Restormer、自动去红眼运行组件及模型。

两个包都安装到 `/opt/soft/webcool`。通常先安装主包；需要 AI 图片/视频增强、人脸重建或去红眼功能的机器再安装 AI 包。升级主包时不需要重新分发体积较大的 AI 包。

只构建其中一个包时可使用：

```bash
./build-mac.sh --version 2.0.0 --main-only
./build-mac.sh --version 2.0.0 --ai-only --skip-build
```

`t.sh` 默认构建、签名并公证这两个安装包。

Ubuntu/Debian 也默认生成两个独立安装包：

- `webcool_<version>-<release>_<arch>.deb`：主 WebCool 包，不包含 AI 运行时和模型。
- `webcool-ai-models_<version>-<release>_<arch>.deb`：可选 AI 包，包含 Linux CodeFormer、Real-ESRGAN 运行时和模型。

两个包均安装到 `/opt/soft/webcool`，AI 包声明依赖相同或更高版本的主包。可分别构建：

```bash
./build-deb.sh --version 2.0.0 --release 1 --main-only
./build-deb.sh --version 2.0.0 --release 1 --ai-only --skip-build
```

安装时先安装主包，再按需安装 AI 包：

```bash
sudo dpkg -i deb/webcool_2.0.0-1_amd64.deb
sudo dpkg -i deb/webcool-ai-models_2.0.0-1_amd64.deb
```

Windows 打包脚本同样默认生成主包和可选 AI 包各自的 ZIP 与 Setup EXE；详细命令和文件名见 `windows/README.md`。

所有平台共用的大型 AI 模型统一存放在项目根目录 `models/`，`tools/` 只保留平台运行程序、动态库、CodeFormer 源码及 Python 环境。具体目录结构见 `models/README.md`。

主包会把运行内容放到 `/opt/soft/webcool`，并仅安装一个启动入口到 `/usr/local/bin/webcool`。

其中：

- 主程序位于 `/opt/soft/webcool/sbin/webcool`
- 配置文件位于 `/opt/soft/webcool/conf/webcool.cf`
- 静态页面位于 `/opt/soft/webcool/html`
- sqlite 动态库位于 `/opt/soft/webcool/lib/sqlite3.so`
- ffmpeg 位于 `/opt/soft/webcool/bin/ffmpeg`
- AI 可执行文件位于 `/opt/soft/webcool/bin`
- AI 模型位于 `/opt/soft/webcool/models`
- CodeFormer 位于 `/opt/soft/webcool/codeformer`
- `/usr/local/bin/webcool` 只是启动入口：设置 `LD_LIBRARY_PATH`、`AICOOL_SQLITE_LIB`、`AICOOL_FFMPEG` 后，默认追加 `-f /opt/soft/webcool/conf/webcool.cf`，再把其余命令行参数传给 `/opt/soft/webcool/sbin/webcool`（不会 `cd` 到安装目录）
- 若配置文件不存在，则不加 `-f`，直接执行二进制
- 命令行中再次指定 `-f` 时，以后面的 `-f` 为准（覆盖默认配置）
- 通过 acl_master 部署时，请直接使用 `/opt/soft/webcool/conf/webcool.cf`，不要依赖 `/usr/local/bin/webcool`

## 默认版本号来源

`common.sh` 中的 `DEFAULT_VERSION` 会优先通过执行 `../webcool -v` 自动读取当前二进制版本号；若读取失败，则回退到 `1.0.0`。

运行 webcool 时也可手动指定 sqlite 动态库路径：

```bash
webcool -S /custom/path/sqlite3.so -s 0.0.0.0:8080 -d ./uploads
```

运行 webcool 时也可手动指定 ffmpeg 路径（覆盖自动探测）：

```bash
webcool -F /custom/path/ffmpeg -s 0.0.0.0:8080 -d ./uploads
```

macOS 在未显式传 `-d` 时，webcool 会自动使用当前用户数据目录：

- `~/Library/Application Support/webcool/data`

该目录会在程序启动时自动创建。

## macOS 安装包签名与公证

当前脚本默认只生成**未签名**的 `.pkg`，在其他 Mac 上安装时会出现「来自身份不明的开发者」提示。

**重要：** Xcode 里的 **Apple Development** 证书只能用于本机/已注册设备调试，**不能**用于给其他 Mac 分发 `.pkg`。你需要：

1. 加入 [Apple Developer Program](https://developer.apple.com/programs/)（付费开发者计划）
2. 在 [Certificates](https://developer.apple.com/account/resources/certificates/list) 创建并下载安装到钥匙串：
   - **Developer ID Application**（签名 pkg 内的二进制）
   - **Developer ID Installer**（签名 `.pkg` 本身）
3. macOS 10.15+ 还需要向 Apple **公证（Notarization）** 安装包

### 查看本机可用证书

```bash
./build-mac.sh --list-signing-identities
```

### 保存公证凭据（只需一次）

在 Apple ID 账户页面生成「App 专用密码」，然后：

```bash
./build-mac.sh --store-notary-profile webcool-notary \
  --notary-apple-id your@email.com \
  --notary-team-id YOUR_TEAM_ID \
  --notary-password xxxx-xxxx-xxxx-xxxx
```

Team ID 可在 [Membership Details](https://developer.apple.com/account#MembershipDetailsCard) 查看。

### 构建可分发安装包

```bash
./build-mac.sh --version 1.0.0 \
  --sign-app-identity "Developer ID Application: Your Name (TEAMID)" \
  --sign-installer-identity "Developer ID Installer: Your Name (TEAMID)" \
  --notarize --notary-profile webcool-notary
```

也可通过环境变量传入（适合 CI）：

```bash
export WEBCOOL_MACOS_SIGN_APP_IDENTITY='Developer ID Application: Your Name (TEAMID)'
export WEBCOOL_MACOS_SIGN_INSTALLER_IDENTITY='Developer ID Installer: Your Name (TEAMID)'
export WEBCOOL_NOTARY_PROFILE='webcool-notary'
./build-mac.sh --version 1.0.0 --notarize
```

若未显式指定证书名，脚本会自动在钥匙串中查找 `Developer ID Application` / `Developer ID Installer`。

### 临时绕过 Gatekeeper（仅测试）

未签名包可在目标 Mac 上右键 pkg →「打开」，或在「系统设置 → 隐私与安全性」中允许。生产分发请务必签名并公证。
