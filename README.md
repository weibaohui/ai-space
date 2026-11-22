# AI-Space

> **本地优先的多智能体技能与主仓集中同步管理工作台**  
> *Local-First Agent Skills Manager & Multi-Host Synchronization Hub*

AI-Space 是一款专为 AI 编码助手（如 Claude、Gemini CLI、Cursor、CodeX 等）打造的高性能本地技能管理与跨端同步工具。采用原生 C99 与轻量级 CUI 界面引擎构建，基于 SQLite3 提供强事务一致性持久化，拒绝软链接隐患，实现纯物理原子级文件分发与多主机同步。

---

## 🌟 核心特性

- **多工具本地技能自动扫描与识别**
  - 自动发现各工具目录（`~/.<tool>/skills`）下的 Skill，提取元数据并持久化到本地 SQLite。
  - 支持多维度搜索、列排序与状态统计。

- **集中主仓管理 (Master Hub) 与全景关系矩阵**
  - 以标准化主仓基准目录（默认 `~/.ai-space/skills`）作为唯一真相源。
  - 全景扫描各工具副本与主仓基准的关系矩阵，精准识别：**完全一致**、**本地改动**、**副本落后**、**主仓未收录** 等分化状态。

- **原子级代码差异比对与文件树可视化**
  - 统一平铺式文件管理器树形组件，直观标记新增、修改与删除文件。
  - 支持双栏分屏（Split Diff）与单栏行内（Inline Diff）差异比对，行级别统计改动。

- **纯物理复制与安全防御快照 (No Symlinks & Snapshot Backups)**
  - 坚持**拒绝软链接**原则，所有安装、采纳与广播均采用纯物理文件拷贝，彻底避免跨容器或多运行时环境下的软链接损坏。
  - 任何覆盖或删除操作前，自动在 `~/.ai-space/backups/` 创建带时间戳的完整快照，100% 可回溯。

- **主机无关的远端多后端同步管理 (Host-Agnostic Multi-Backend Sync)**
  - 抽象出不变的同步原语（`test_connection`、`fetch_remote_manifest`、`check_drift`、`pull_skill`、`push_skill` 等）。
  - 支持多种云端/局域网共享后端：
    - **WebDAV**（坚果云、Nextcloud、自建 WebDAV）
    - **NFS / 局域网共享挂载目录**（NAS、SMB、网络共享存储）
    - **Git 远端版本库**（GitHub、GitLab、Gitea）
  - 多台主机只需配置相同的后端，即可跨主机无缝共享技能版本，一台变更、多台即时同步。

- **现代化系统设置 (Tabbed Web-Style Settings)**
  - 类别涵盖：仓储与主库路径、远端同步后端激活与连通性测试、扫描策略与安全规则、常规界面偏好。

---

## 🚀 快速上手

### 环境要求
- macOS / Linux
- C 语言编译器（`clang` 或 `gcc`，支持 C99 标准）
- SQLite3 运行库（`-lsqlite3`）
- OpenGL & Cocoa（macOS 平台）

### 编译构建
```bash
# 克隆仓库
git clone https://github.com/weibaohui/ai-space.git
cd ai-space

# 编译生成可执行文件
make

# 运行应用程序
./ai_space
```

---

## 📁 源码架构目录

```
ai-space/
├── include/                # 头文件与接口定义
│   ├── app_state.h         # 全局应用状态机模型
│   ├── sync_backend.h      # 远端同步后端抽象与原语 VTable
│   ├── hub_manager.h       # 主仓管理、物理复制与指纹哈希
│   ├── db.h                # SQLite 本地持久化与配置表
│   ├── view_tools.h        # 工具列表视图
│   ├── view_master_hub.h   # 主仓集中管理视图
│   ├── view_hub_matrix.h   # 全景关系扫描矩阵视图
│   ├── view_hub_diff.h     # 代码差异比对视图
│   ├── view_sync.h         # 远端同步管理视图
│   └── view_settings.h     # 网页 Tab 风格系统设置
├── src/                    # 核心实现源码
│   ├── main.c              # 程序主入口与帧调度分发器
│   ├── sync_manager.c      # 同步后端驱动工厂与调度
│   ├── sync_driver_nfs.c   # NFS / 局域网共享文件系统驱动
│   ├── sync_driver_webdav.c# WebDAV 协议驱动
│   ├── sync_driver_git.c   # Git 远端版本库驱动
│   ├── hub_manager.c       # 主仓物理读写、安全快照与哈希计算
│   ├── db.c                # SQLite 数据持久化与 Key-Value 配置
│   └── ...
├── CUI/                    # 响应式即时渲染 GUI 引擎
└── Makefile                # 编译配置规则
```

---

## 📄 开源许可证

MIT License.
