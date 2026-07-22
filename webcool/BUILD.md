# webcool 编译与打包指南

本文档汇总 webcool 在 macOS / Linux 上的本地编译、Universal Binary（Apple Silicon + Intel）构建，以及 macOS 安装包打包流程。

## 目录结构

| 路径 | 说明 |
|------|------|
| `webcool/` | webcool 主程序源码与 Makefile |
| `webcool/package/` | 安装包构建脚本（mac / deb / rpm） |
| `third-party/acl/` | ACL 网络库（静态链接） |
| `third-party/sqlite/` | sqlite3 动态库（运行时加载） |
| `tools/mac/ffmpeg` | macOS 版 ffmpeg（打包时复制进安装目录） |
| `tools/linux/ffmpeg` | Linux 版 ffmpeg |

仓库根目录 `Makefile` 提供一键入口：

```bash
# 从仓库根目录
make          # 等价于 make app：tools + acl + sqlite + webcool
make rebuild  # clean 后全量重建
```

## 本地编译

### 仅编译 webcool

前提：ACL 静态库与 sqlite 动态库已就绪。

```bash
cd webcool
make clean && make
```

构建成功后 Makefile 会打印二进制架构（macOS 上通过 `lipo -archs`）。
macOS 还会同时生成 `webcool.app`：在 Finder 中双击该应用包可直接
打开控制窗口，不会弹出 Terminal。裸二进制 `webcool` 仍保留给命令行模式使用。

### 从仓库根目录全量编译

```bash
cd /path/to/aicool
make clean && make
```

这会依次：

1. 解压 `tools/mac/ffmpeg` 或 `tools/linux/ffmpeg`（若尚未存在）
2. 编译 `third-party/acl`
3. 编译 `third-party/sqlite/lib/sqlite3.so`
4. 编译 `webcool/webcool`

## macOS Universal Binary（arm64 + x86_64）

在 Apple Silicon 与 Intel Mac 上都能运行的通用二进制，需要 **webcool、ACL 静态库、sqlite 动态库** 均为 fat binary。

### webcool/Makefile

macOS 上默认 `MACOS_UNIVERSAL=1`，编译与链接均加 `-arch arm64 -arch x86_64`：

```bash
cd webcool
make clean && make
# 输出示例：架构：arm64 x86_64
```

仅编译本机架构（更快）：

```bash
make clean && make MACOS_UNIVERSAL=0
```

重建 universal 版 ACL 静态库：

```bash
cd webcool
make mac-universal-acl
```

### third-party/acl

ACL 含大量 **C 源码**，macOS 上编译时必须区分 C / C++ 编译器：

| 变量 | 正确用法 | 错误用法 |
|------|----------|----------|
| `ENV_CC` | `clang -arch arm64 -arch x86_64` | `g++ ...`（会把 `.c` 当 C++ 编译） |
| `ENV_CPP` | `g++ -arch arm64 -arch x86_64` | — |

错误示例及报错：

```bash
make all_lib ENV_CC="g++ -arch arm64 -arch x86_64" ENV_CPP="g++ -arch arm64 -arch x86_64"
# clang++: error: treating 'c' input as 'c++' when in C++ mode
```

正确命令：

```bash
cd third-party/acl
make clean
make all_lib \
  ENV_CC="clang -arch arm64 -arch x86_64" \
  ENV_CPP="g++ -arch arm64 -arch x86_64"
```

`webcool/Makefile` 在 `MACOS_UNIVERSAL=1` 时会自动传入上述 `ENV_CC` / `ENV_CPP`。

### third-party/sqlite

`third-party/sqlite/Makefile` 在 macOS 上默认构建 universal 版 `lib/sqlite3.so`：

```bash
cd third-party/sqlite
make clean && make build
# 输出示例：Architectures in the fat file: ... are: x86_64 arm64
```

查看架构：

```bash
make show-arch
# 或
lipo -info lib/sqlite3.so
```

单架构构建：

```bash
make build MACOS_UNIVERSAL=0
```

### 手动全量 Universal 构建（推荐顺序）

```bash
# 1. ACL
cd third-party/acl
make clean
make all_lib \
  ENV_CC="clang -arch arm64 -arch x86_64" \
  ENV_CPP="g++ -arch arm64 -arch x86_64"

# 2. sqlite
cd ../sqlite
make clean && make build

# 3. webcool
cd ../../webcool
make clean && make
```

验证：

```bash
lipo -archs webcool
lipo -archs ../third-party/sqlite/lib/sqlite3.so
lipo -archs ../third-party/acl/lib_acl/lib/lib_acl.a
```

### ffmpeg 说明

打包脚本会把 `tools/mac/ffmpeg` 复制到安装包的 `/opt/soft/webcool/bin/ffmpeg`。若该文件**不是** universal binary，在另一种 CPU 架构的 Mac 上转码可能失败。使用 `--universal` 打包时脚本会检测并打印警告；如需完整跨架构支持，需自行准备 universal 版 ffmpeg 替换 `tools/mac/ffmpeg`。

## macOS 安装包打包

脚本位于 `webcool/package/`，详细安装布局见 [`package/README.md`](package/README.md)。

### 本机架构 pkg（默认）

输出文件名：`package/mac/webcool-<版本>-macos-<uname -m>.pkg`（如 `macos-arm64.pkg`）。

```bash
cd webcool/package
./build-mac.sh --version 1.3.6
```

### Universal pkg（推荐分发）

加 `--universal` 会在打包前自动：

1. 重建 universal ACL
2. 重建 universal sqlite3.so
3. 重建 universal webcool
4. 用 `lipo` 校验 webcool 与 sqlite3.so

输出文件名：`package/mac/webcool-<版本>-macos-universal.pkg`。

```bash
cd webcool/package
./build-mac.sh --version 1.3.6 --universal
```

也可通过环境变量默认开启：

```bash
export WEBCOOL_MACOS_UNIVERSAL=1
./build-mac.sh --version 1.3.6
```

跳过编译、仅打包已有二进制：

```bash
./build-mac.sh --version 1.3.6 --universal --skip-build
```

### Apple 开发者证书创建与配置

要在**其他 Mac** 上顺利安装 `.pkg`（不出现「来自身份不明的开发者」），需要完成三件事：

| 步骤 | 内容 |
|------|------|
| 1 | 加入付费 **Apple Developer Program**（约 $99/年） |
| 2 | 创建并安装 **Developer ID Application** + **Developer ID Installer** 两种证书 |
| 3 | 对 pkg 进行 Apple **公证（Notarization）**（macOS 10.15+） |

**Xcode 里的 `Apple Development` 证书不能用于对外分发。** 它只能在本机或已注册设备上调试，list 里类似：

```text
Apple Development: your@email.com (XXXXXXXXXX)
```

这与 **Developer ID** 是两套完全不同的证书体系。

#### 第一步：确认付费开发者计划已生效

1. 打开 [Apple Developer 账户](https://developer.apple.com/account)
2. 查看 **Membership** 卡片，应显示 **Apple Developer Program — Active**
3. 点击 **Membership Details**，记录 **Team ID**（10 位字母数字）

| Membership 显示 | 能否创建 Developer ID / 公证 |
|-----------------|------------------------------|
| Apple Developer Program — Active | ✅ 可以 |
| Personal Team / 仅免费 Apple ID | ❌ 不可以 |

**Team ID 常见混淆：**

- **Membership 页面的 Team ID** — 用于 `--notary-team-id`（公证凭据）
- **证书括号里的 `(XXXXXXXXXX)`** — 来自该证书所属团队，可能与 Xcode 个人团队的 ID **不同**
- **切勿**把 `Apple Development` 证书括号里的 ID 当作公证 Team ID，否则会报 `403 Invalid or inaccessible developer team ID`

Team ID 也可在 Xcode → **Settings → Accounts** → 选中 Apple ID → 查看 Team 详情。

#### 第二步：在本机生成 CSR（证书签名请求）

CSR 在生成时会在本机钥匙串中创建**私钥**；Apple 发回的 `.cer` 只有公钥，必须与这把私钥配对才能签名。

1. 打开 **钥匙串访问（Keychain Access）**
2. 菜单：**钥匙串访问 → 证书助理 → 从证书颁发机构请求证书…**
3. 填写邮箱，选 **存储到磁盘**
4. 得到 `CertificateSigningRequest.certSigningRequest`

**重要：一份 CSR 只能申请一张证书。** Application 与 Installer 必须各用**不同的 CSR**（各对应一把私钥）。若上传同一 CSR 创建第二张证，Apple 会报错：

```text
The uploaded CSR file has already been used to generate another certificate.
```

推荐流程：

1. 生成 **CSR #1** → 用于 **Developer ID Application**
2. 再生成 **CSR #2** → 用于 **Developer ID Installer**

#### 第三步：在 Apple 网站创建 Developer ID Application

1. 打开 [Certificates → +](https://developer.apple.com/account/resources/certificates/add)
2. 选择 **Developer ID Application**（用于签名 pkg 内的 Mach-O：webcool、ffmpeg、sqlite3.so 等）
3. 上传 **CSR #1**
4. 下载 `.cer`，**双击**安装到「登录」钥匙串

#### 第四步：创建 Developer ID Installer

1. 再次打开 [Certificates → +](https://developer.apple.com/account/resources/certificates/add)
2. 选择 **Developer ID Installer**（**不是** Application；用于 `productsign` 签名 `.pkg`）
3. 上传 **CSR #2**（必须是未使用过的新 CSR）
4. 下载 `.cer`，**双击**安装

若提示 **已有有效的 Developer ID Installer，无法创建新的**：

- 到 [Certificates 列表](https://developer.apple.com/account/resources/certificates/list) 找到旧 Installer，进入详情页 **Revoke（吊销）**
- Revoke 按钮仅 **Account Holder / Admin** 可见；Developer 角色可能只能查看
- 吊销后，在本机用**新 CSR** 重新申请 Installer

#### 第五步：安装 .cer 并确认私钥配对

**推荐：** 在 Finder 中**双击** `.cer`，系统自动导入「登录」钥匙串并与 CSR 生成的私钥配对。

若手动导入：**文件 → 导入项目…**（不要用「添加钥匙串…」）。左侧选中 **登录** 钥匙串后再导入。

在钥匙串访问中检查（**我的证书** 分类，不是「证书」）：

```text
▶ Developer ID Application: Your Name (TEAMID)
    └─ 私钥

▶ Developer ID Installer: Your Name (TEAMID)
    └─ 私钥
```

若证书出现在「证书」分类、**没有** ▶ 和「私钥」，说明 CSR 不是在这台 Mac 上生成的，或 `.cer` 与私钥不匹配，需要在本机重新生成 CSR 并重新申请。

#### 第六步：验证本机可用签名身份

```bash
cd webcool/package
./build-mac.sh --list-signing-identities
```

脚本使用 `security find-identity -v -p basic`（**不是** `-p codesigning`），因为 **Developer ID Installer 通常只出现在 `basic` 策略下**，用 `codesigning` 会误报「缺少 Installer」。

**合格示例**（应看到 **3 valid identities found**，且同时包含 Application 与 Installer）：

```text
1) xxxx... "Apple Development: your@email.com (XXXXXXXXXX)"
2) yyyy... "Developer ID Installer: Your Name (TEAMID)"
3) zzzz... "Developer ID Application: Your Name (TEAMID)"
```

也可直接用系统命令对比：

```bash
security find-identity -v -p basic | grep -E 'Developer ID|valid identities'
```

**证书名称格式**（传给 `--sign-app-identity` 时须与钥匙串**完全一致**）：

```text
Developer ID Application: Your Name (TEAMID)
Developer ID Installer: Your Name (TEAMID)
```

注意 **名字与 `(TEAMID)` 之间有一个空格**。错误示例：`Your Name(TEAMID)`（缺空格）会导致 `no identity found`。

可使用完整名称，也可使用 list 输出的 **40 位哈希**（更不易拼错）：

```bash
--sign-app-identity "F75A4786D88240E4B651D717C930D5F1E5B0CED4"
--sign-installer-identity "839AE2544C625D4D915BB495D9901217E76374BB"
```

#### 第七步：保存公证凭据（只需一次）

公证需 Apple ID 的 **App 专用密码**（不是 Apple ID 登录密码）：

1. 登录 [appleid.apple.com](https://appleid.apple.com)
2. **登录与安全性 → App 专用密码 → 生成**
3. 保存生成的密码（格式 `xxxx-xxxx-xxxx-xxxx`）

```bash
./build-mac.sh --store-notary-profile webcool-notary \
  --notary-apple-id your@email.com \
  --notary-team-id YOUR_TEAM_ID \
  --notary-password xxxx-xxxx-xxxx-xxxx
```

`YOUR_TEAM_ID` 必须用 **Membership Details** 里的 Team ID，**不是** Apple Development 证书括号里的 ID。

验证成功后再进行打包。若报 `403 Invalid or inaccessible developer team ID`，说明 Team ID 与 Apple ID 不匹配，或尚未加入付费开发者计划。

#### 第八步：构建、签名并公证

```bash
cd webcool/package

./build-mac.sh --version 1.3.6 --universal \
  --sign-app-identity "Developer ID Application: Your Name (TEAMID)" \
  --sign-installer-identity "Developer ID Installer: Your Name (TEAMID)" \
  --notarize --notary-profile webcool-notary
```

成功时输出类似：

```text
[package] notarization complete and ticket stapled
[package] done: .../mac/webcool-1.3.6-macos-universal.pkg
```

也可通过环境变量传入（适合 CI）：

```bash
export WEBCOOL_MACOS_SIGN_APP_IDENTITY='Developer ID Application: Your Name (TEAMID)'
export WEBCOOL_MACOS_SIGN_INSTALLER_IDENTITY='Developer ID Installer: Your Name (TEAMID)'
export WEBCOOL_NOTARY_PROFILE='webcool-notary'
./build-mac.sh --version 1.3.6 --universal --notarize
```

#### 证书配置检查清单

| 检查项 | 合格标准 |
|--------|----------|
| 付费开发者计划 | Membership 为 Active |
| Developer ID Application | list 中可见，且钥匙串「我的证书」下有私钥 |
| Developer ID Installer | list 中可见（`-p basic`），且「我的证书」下有私钥 |
| CSR | Application 与 Installer 各用一份，未复用 |
| 公证 Team ID | Membership Details 中的 10 位 ID |
| App 专用密码 | `store-notary-profile` 验证通过 |
| 打包 prune | 日志出现 removing node_modules / view-heic-browser-extension.zip |

#### 证书相关常见陷阱

| 现象 | 原因 | 处理 |
|------|------|------|
| list 只有 Apple Development | 尚未创建 Developer ID 证书 | 按上文第三、四步在 Apple 网站申请 |
| 有 Application，无 Installer | 只申请了一种，或误申请了两张 Application | Installer 须单独用新 CSR 申请 |
| `.cer` 已装但 list 无 Installer | 证书无私钥（CSR 在别的 Mac 生成） | 本机新 CSR → 吊销旧证（若需）→ 重新申请 |
| CSR 已用过 | 同一 CSR 申请第二张证 | 再生成一份新 CSR |
| 无法新建 Installer | Apple 账户已有有效 Installer | Account Holder 吊销旧 Installer 后重建 |
| `no identity found` | 证书名拼写/空格错误，或证书未装 | 从 list 原样复制名称或哈希 |
| 公证 403 | Team ID 错误或账号未付费 | 用 Membership 的 Team ID，确认 Program Active |
| list 用 codesigning 看不到 Installer | Installer 不在 codesigning 策略下 | 用 `./build-mac.sh --list-signing-identities`（已改用 basic） |

打包前脚本会自动 `prune` 以下内容以通过公证：

- 安装树中的 `node_modules` 目录
- `html/js/view-heic-browser-extension.zip`（zip 内含未签名的 node 原生模块，Apple 会扫描 zip 内部）

### 安装包内容

安装根目录：`/opt/soft/webcool`

| 路径 | 内容 |
|------|------|
| `sbin/webcool` | 主程序 |
| `conf/webcool.cf` | 默认配置 |
| `html/` | 静态页面 |
| `lib/sqlite3.so` | sqlite 动态库 |
| `lib/*.so` / `*.dylib` | ACL 运行时库（如有） |
| `bin/ffmpeg` | ffmpeg |
| `/usr/local/bin/webcool` | 启动入口（设置库路径与环境变量） |

## Linux 打包

```bash
cd webcool/package
./build-deb.sh --version 1.0.0 --release 1
./build-rpm.sh --version 1.0.0 --release 1
```

## 常见问题

### ACL 编译报错 `treating 'c' input as 'c++'`

原因：`ENV_CC` 使用了 `g++`。解决：改用 `clang` 作为 `ENV_CC`（见上文 ACL 章节）。

### `ranlib: ... stdafx.o has no symbols`

ACL 构建时的常见警告，可忽略。

### pkg 公证失败（Invalid）

常见原因：

- pkg 内含有未签名的 Mach-O（如浏览器扩展 zip、node_modules 内原生模块）
- 脚本默认会 prune 上述文件；若仍失败，检查打包日志是否出现 `removing:` 相关输出

### 公证报 403（Invalid or inaccessible developer team ID）

- `--notary-team-id` 须用 [Membership Details](https://developer.apple.com/account#MembershipDetailsCard) 中的 Team ID
- **不要**使用 `Apple Development` 证书括号里的 ID
- 确认 Apple ID 已加入付费 **Apple Developer Program** 且状态为 Active

### list 里看不到 Developer ID Installer

- Installer 可能已安装但**没有配对私钥**（CSR 在其他 Mac 生成）→ 本机重新生成 CSR 并申请
- 用 `security find-identity -v -p basic` 查看；`-p codesigning` 通常**不包含** Installer
- 使用 `./build-mac.sh --list-signing-identities`（脚本已改用 basic 策略）

### codesign 报 `no identity found`

- 证书名称须与钥匙串完全一致，**名字与 `(TEAMID)` 之间有空格**
- 或直接使用 list 输出的 40 位证书哈希代替完整名称

### Apple 网站无法创建新 Installer

- 账户里已有有效 Installer 证书占名额 → 需 Account Holder **Revoke** 旧证后再用新 CSR 申请
- 若看不到 Revoke 按钮，当前 Apple ID 可能只有 Developer 权限，需账户持有人操作

### webcool 找不到 sqlite

运行时可通过 `-S` 指定动态库路径，或设置环境变量 `AICOOL_SQLITE_LIB`：

```bash
webcool -S /opt/soft/webcool/lib/sqlite3.so -s 0.0.0.0:8080 -d ./uploads
```

安装包内的 `/usr/local/bin/webcool` 启动脚本会自动设置 `AICOOL_SQLITE_LIB` 与 `DYLD_LIBRARY_PATH`。

## 相关文件

- [`Makefile`](Makefile) — webcool 编译选项与 universal / ACL 集成
- [`../third-party/sqlite/Makefile`](../third-party/sqlite/Makefile) — sqlite 动态库
- [`package/build-mac.sh`](package/build-mac.sh) — macOS pkg 构建入口
- [`package/common.sh`](package/common.sh) — 编译、打包、签名、公证逻辑
- [`package/README.md`](package/README.md) — 安装包布局与脚本索引
