# 星缘编语
![C++](https://img.shields.io/badge/Language-C++17-blue.svg)
![Platform](https://img.shields.io/badge/Platform-Android%20%7C%20Linux%20%7C%20Windows-green.svg)
![License](https://img.shields.io/badge/License-XingYuan--Custom-orange.svg)

## 🚀 快速开始

> ⚠️ **注意**：本项目目前仅在 **Android (Termux)** 环境下进行过完整编译与测试。由于底层代码可能包含特定系统调用，PC 端（Windows/Linux）目前处于**未测试状态**，欢迎有环境的开发者在 PC 端尝试并提交 Issue/PR。

### 🛠️ 依赖环境说明

本项目基于 C++17 编写，编译和运行需要以下核心组件：
- **C++ 编译器**：支持 C++17 标准的 `g++` 或 `clang++`。
- **OpenSSL 库**：用于底层加密/网络通信（链接 `-lssl -lcrypto`）。
- **pthread**：用于多线程支持。
- **构建工具**：`make` 或 `bash`。

---

### 📱 方案一：在 Android 手机上运行（已测试）

如果你也想在手机上体验，可以使用 **Termux** 终端：

1. **下载并安装 Termux**（建议从 F-Droid 或 GitHub 下载最新版）。
2. 打开 Termux，更新软件源并安装基础依赖：
   ```bash
   pkg update && pkg upgrade
   pkg install git clang make openssl
   ```
   (注：Termux 下通常使用 clang 编译，如果你的脚本绑定了 g++，可以尝试 pkg install gcc)
3. 克隆本项目并进入目录：
   ```bash
   git clone https://gitee.com/starry-language/XingYuanBianYu.git
   cd XingYuanBianYu
   ```
4. 赋予脚本执行权限并编译运行：
   ```bash
   chmod +x XYBY.sh
   ./XYBY.sh
   ```
   
💻 方案二：在 PC（Linux / Windows）上运行（待测试）

对于 Linux (以 Ubuntu/Debian 为例)：
如果你有 Linux 环境，可以直接安装依赖并编译：

1. 安装依赖：
   ```bash
   sudo apt-get update
   sudo apt-get install g++ libssl-dev make
   ```
2. 克隆项目并运行：
   ```bash
   git clone https://gitee.com/starry-language/XingYuanBianYu.git
   cd XingYuanBianYu
   chmod +x XYBY.sh
   ./XYBY.sh
   ```

对于 Windows：

1. 本项目包含 XYBY.bat 脚本，理论上适用于 Windows。
2. 你需要安装 MinGW 或 MSYS2 作为 C++ 编译环境，并确保安装了 OpenSSL 库。
3. 由于开发者暂无 Windows 测试环境，如果你在运行 XYBY.bat 时遇到报错，请提交 Issue 告诉我们。

## 系统内部指令
[指令表](./指令表.md)

## 📄 开源协议 (License)

本项目采用自定义开源协议，**强制保留原作者署名（XingYuan），禁止未经许可的商业使用**。
详细条款请参阅根目录下的 [LICENSE](./LICENSE) 文件。