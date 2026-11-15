# 星缘编语 (XingYuanBianYu)

一个用 C++17 从零编写的轻量级虚拟机/系统模拟器。目前包含核心的虚拟机执行引擎、虚拟文件系统(VFS)、内存管理(AMM)等模块。

> ⚠️ **当前状态**：早期开发中。目前仅在 Android (Termux) 环境下编译测试通过，尚未在 PC 端进行广泛测试，欢迎提 Issue 交流。

## 🚀 快速开始

### 依赖环境
- g++ (支持 C++17)
- OpenSSL 库 (`libssl-dev`)
- pthread

### 编译与运行
```bash
# 赋予脚本执行权限
chmod +x XYBY.sh

# 编译并启动（如果没有 disk.img 会自动创建）
./XYBY.sh

```
## 📄 开源协议 (License)

本项目采用自定义开源协议，**强制保留原作者署名（XingYuan），禁止未经许可的商业使用**。
详细条款请参阅根目录下的 [LICENSE](./LICENSE) 文件。