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
- macOS / Linux / Windows（跨平台）
- C 语言编译器（`clang` 或 `gcc`，支持 C99 标准）
- SQLite3 运行库（`-lsqlite3`）
- Linux 需要 X11 开发头文件（`libx11-dev`）；Windows 需 MinGW（MSYS2）；macOS 无额外依赖

### 编译构建
```bash
# 克隆仓库（含 CUI 子模块）
git clone --recursive https://github.com/weibaohui/ai-space.git
cd ai-space

# 方式一：本地 macOS 开发（Makefile）
make

# 方式二：跨平台构建（Linux / macOS / Windows，CMake）
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/ai_space
```

### 自动化构建（GitHub Actions）
推送 / 打 tag 后，[build workflow](.github/workflows/build.yml) 会自动为以下平台矩阵编译并打包
（归档内含可执行文件与 CJK 字体 `fonts/`）：

| 平台 | 架构 | Runner |
|---|---|---|
| Linux | x86_64 / arm64 | ubuntu-22.04 / ubuntu-22.04-arm |
| macOS | arm64 / x86_64 / Universal2 | macos-14 / macos-13 |
| Windows | x86_64 / arm64 | MSYS2 MinGW64 / CLANGARM64 |

- 每次推送：产物上传为 workflow artifact（Actions 页面下载）；
- 推送 `v*` tag：产物自动附加到 GitHub Release。

> ⚠️ 运行时请保持可执行文件与 `fonts/` 目录相对位置不变（CJK 字体从相对路径加载）。

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
├── tests/                  # 跨主机同步自动化测试套件
│   ├── test_sync_git.c     # 跨后端同步 CLI 测试工具源码
│   └── test_multi_host_git_sync.sh # 真实 GitHub 多主机双向闭环自动化验证脚本
└── Makefile                # 编译配置规则
```

---

## 🧪 多主机跨端同步自动化验证

本项目提供了基于真实 GitHub 远程版本库的跨主机自动化测试套件，模拟多台独立主机（如 Host-Beijing 与 Host-Shanghai）共享同一云端主仓的全流程：

```bash
# 运行完整自动化测试套件 (自动探测真实 GitHub 远端、双向推送/拉取并校验指纹)
./tests/test_multi_host_git_sync.sh
```

**测试覆盖场景：**
1. **可用性前置强校验**：验证合法仓库连通性与权限，拦截非法/不存在仓库；
2. **首端初始化与推送**：主机 A 创建技能，计算哈希，推送到真实 GitHub 远端；
3. **次端感知与拉取**：主机 B 初始为空，感知远端漂移（remote_only），拉取后比对指纹一致性（100% 匹配）；
4. **增量演进与双向闭环**：主机 B 升级已有技能并新增技能推送到远端，主机 A 实时感知（remote_ahead 与 remote_only）并拉取更新，达成最终 0 冲突一致状态（in_sync）。

---

## 📄 开源许可证

MIT License.
