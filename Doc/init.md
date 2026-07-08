# ESP-IDF 安装

> 本文档介绍使用 VSCode 扩展安装 ESP-IDF 开发框架的完整流程。

---

## 目录

- [ESP-IDF 安装](#esp-idf-安装)
  - [目录](#目录)
  - [📋 前置条件](#-前置条件)
  - [🚀 安装步骤](#-安装步骤)
    - [1. 安装 VSCode](#1-安装-vscode)
    - [2. 安装 ESP-IDF 扩展](#2-安装-esp-idf-扩展)
    - [3. 通过扩展安装 ESP-IDF 工具链](#3-通过扩展安装-esp-idf-工具链)
    - [4. 配置环境变量（可选）](#4-配置环境变量可选)
  - [📂 创建示例项目](#-创建示例项目)
  - [🔨 编译、烧录与监控](#-编译烧录与监控)
  - [❓ 常见问题](#-常见问题)
    - [Q1: 安装过程中 Python 报错](#q1-安装过程中-python-报错)
    - [Q2: 下载 ESP-IDF 超时](#q2-下载-esp-idf-超时)
    - [Q3: 串口无法识别](#q3-串口无法识别)
    - [Q4: 编译报错找不到工具链](#q4-编译报错找不到工具链)
    - [Q5: 权限不足（Linux / macOS）](#q5-权限不足linux--macos)
  - [📚 参考资料](#-参考资料)

---

## 📋 前置条件

| 依赖项 | 要求 |
|--------|------|
| 操作系统 | Windows 10+ / Linux / macOS |
| [Python](https://www.python.org/downloads/) | 3.8 或更高版本 |
| [Git](https://git-scm.com/downloads) | 版本控制工具 |
| [VSCode](https://code.visualstudio.com/) | 最新稳定版 |

> **⚠️ 注意**：
> - Windows 用户安装 Python 时请勾选 **"Add Python to PATH"**。
> - Git 安装时建议选择默认选项即可。

---

## 🚀 安装步骤

### 1. 安装 VSCode

前往 [VSCode 官网](https://code.visualstudio.com/) 下载并安装适用于您操作系统的版本。安装完成后启动 VSCode。

---

### 2. 安装 ESP-IDF 扩展

1. 打开 VSCode，点击左侧活动栏中的 **扩展** 图标（快捷键 `Ctrl+Shift+X`）。
2. 在搜索框中输入 **`ESP-IDF`**。
3. 找到由 **Espressif Systems** 发布的 **ESP-IDF** 扩展，点击 **安装（Install）**。

---

### 3. 通过扩展安装 ESP-IDF 工具链

安装扩展后，VSCode 会自动弹出配置向导。如果没有弹出，可以通过命令面板手动启动：

1. 按 `Ctrl+Shift+P` 打开命令面板。
2. 输入 **`ESP-IDF: Configure ESP-IDF Extension`** 并回车。

3. 在配置界面中，选择安装方式：

| 选项 | 说明 |
|------|------|
| **Express（快速安装）** | 推荐新手使用，自动下载 ESP-IDF 和工具链 |
| **Advanced（高级安装）** | 可自定义 ESP-IDF 版本和安装路径 |
| **Use Existing Setup** | 使用已安装的 ESP-IDF |

4. **推荐选择 Express 模式**，然后：
   - 选择 ESP-IDF 版本（建议选择最新的稳定版，如 `v5.x`）。
   - 选择工具链的安装路径（路径中 **不要包含中文或空格**）。
   - 点击 **Install** 开始安装。

5. 等待安装完成，安装过程包括：
   - 下载 ESP-IDF 源码
   - 下载并安装工具链（编译器、CMake、Ninja 等）
   - 安装 Python 虚拟环境及依赖

> **⏳ 提示**：整个安装过程可能需要 10~30 分钟，取决于网络速度。如果下载缓慢，可以尝试设置代理或使用镜像源。

6. 安装成功后，VSCode 底部状态栏会显示 ESP-IDF 版本号，表示环境已就绪。

---

### 4. 配置环境变量（可选）

如果需要在 VSCode 外部（如系统终端）使用 `idf.py` 命令，需要手动配置环境变量：

**Windows（PowerShell）**：
```powershell
# 将以下路径添加到系统 PATH 环境变量中
# ESP-IDF 安装路径下的 tools 目录
$env:IDF_PATH = "C:\Users\<用户名>\.espressif\esp-idf\v5.x"
```

**Linux / macOS**：
```bash
# 在 ~/.bashrc 或 ~/.zshrc 中添加
export IDF_PATH="$HOME/.espressif/esp-idf/v5.x"
source ~/.espressif/esp-idf/v5.x/export.sh
```

> **💡 建议**：如果仅在 VSCode 中开发，扩展已自动处理环境变量，无需额外配置。

---

## 📂 创建示例项目

1. 按 `Ctrl+Shift+P` 打开命令面板。
2. 输入 **`ESP-IDF: New Project`** 并回车。
3. 填写项目信息：
   - **Project Name**：项目名称
   - **Choose ESP-IDF Version**：选择已安装的 ESP-IDF 版本
   - **Select Board**：选择目标芯片或开发板（如 `ESP32`、`ESP32-S3` 等）
   - **Choose Template**：选择项目模板（可选 `blink` 示例）
4. 点击 **Choose Template** 后，选择一个目录保存项目。
5. 项目创建完成后，VSCode 会自动打开新项目。

> **📝 提示**：也可以在终端中使用以下命令创建项目：
> ```bash
> idf.py create-project my_project
> ```

---

## 🔨 编译、烧录与监控

VSCode 底部状态栏提供了快捷操作按钮：

| 操作 | 方法 | 快捷键 |
|------|------|--------|
| **选择目标芯片** | 点击状态栏中的 `ESP-IDF: <chip>` | `Ctrl+Shift+P` → `ESP-IDF: Set Espressif Device Target` |
| **设置串口** | 点击状态栏中的串口图标 | `Ctrl+Shift+P` → `ESP-IDF: Select Port to Use` |
| **编译** | 点击状态栏中的 🔨 图标 | `Ctrl+Shift+P` → `ESP-IDF: Build your Project` |
| **烧录** | 点击状态栏中的 ⚡ 图标 | `Ctrl+Shift+P` → `ESP-IDF: Flash your Project` |
| **监控** | 点击状态栏中的 📟 图标 | `Ctrl+Shift+P` → `ESP-IDF: Monitor your Project` |
| **编译+烧录+监控** | — | `Ctrl+Shift+P` → `ESP-IDF: Build, Flash and Monitor` |

**基本流程**：

```
选择芯片 → 设置串口 → 编译 → 烧录 → 监控
```

1. **选择目标芯片**：`Ctrl+Shift+P` → 输入 `ESP-IDF: Set Espressif Device Target`，选择您的 ESP32 型号。
2. **设置串口**：将 ESP32 通过 USB 连接电脑，`Ctrl+Shift+P` → `ESP-IDF: Select Port to Use`，选择对应串口。
3. **编译**：点击底部状态栏的 🔨 图标，等待编译完成。
4. **烧录**：点击底部状态栏的 ⚡ 图标，将固件烧录到芯片。
5. **监控**：点击底部状态栏的 📟 图标，查看串口输出日志。

---

## ❓ 常见问题

### Q1: 安装过程中 Python 报错

确保已安装 Python 3.8+，并且已添加到系统 PATH。在终端中验证：

```bash
python --version
pip --version
```

### Q2: 下载 ESP-IDF 超时

可以通过以下方式解决：
- 设置代理：在安装向导中配置 HTTP 代理地址。
- 使用 GitHub 镜像：将 ESP-IDF 仓库 fork 后修改为国内镜像地址。
- 手动克隆：在终端中预先克隆 ESP-IDF 仓库到指定目录，再选择 **Use Existing Setup**。

```bash
git clone --recursive https://github.com/espressif/esp-idf.git --depth 1
```

### Q3: 串口无法识别

- **Windows**：安装 [CP2102](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers) 或 [CH340](http://www.wch-ic.com/downloads/CH341SER_EXE.html) 驱动。
- **Linux**：将当前用户添加到 `dialout` 组：
  ```bash
  sudo usermod -a -G dialout $USER
  ```
  重新登录后生效。

### Q4: 编译报错找不到工具链

确认 VSCode 底部状态栏显示了正确的 ESP-IDF 版本。如果没有，按 `Ctrl+Shift+P` → `ESP-IDF: Configure ESP-IDF Extension` 重新配置。

### Q5: 权限不足（Linux / macOS）

```bash
# 给串口设备添加读写权限
sudo chmod 666 /dev/ttyUSB0
```

---

## 📚 参考资料

- [ESP-IDF 官方文档](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32/get-started/)
- [VSCode ESP-IDF 扩展文档](https://github.com/espressif/vscode-esp-idf-extension/blob/master/docs/tutorial/install.md)
- [ESP-IDF 编程指南](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32/api-guides/index.html)
