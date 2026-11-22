#!/usr/bin/env bash
# ==============================================================================
# AI-Space 多主机跨端同步自动化验证脚本 (真实 GitHub 远端版本库)
# ==============================================================================
# 模拟场景：
#   主机 A (Host-Beijing) 与 主机 B (Host-Shanghai)
#   两台主机配置相同的远端 Git 仓库后端，各自拥有独立的本地技能主仓与本地暂存缓存。
#   验证可用性前置检测、首端推送、次端发现漂移、全量拉取、内容指纹校验、
#   次端增量修改与新增技能推送、首端感知漂移、双向拉取闭环与最终一致性。
# ==============================================================================

set -eo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN="$PROJECT_ROOT/test_sync"

# 默认测试远端仓库 (真实 GitHub 仓库)
DEFAULT_REPO_URL="git@github.com:weibaohui/ai-space-sync-test.git"
REPO_URL="${1:-$DEFAULT_REPO_URL}"
BRANCH="${2:-main}"

echo -e "${CYAN}${BOLD}==============================================================================${NC}"
echo -e "${CYAN}${BOLD}  AI-Space 跨主机 Git 远端同步自动化验证套件 (Real GitHub Service) ${NC}"
echo -e "${CYAN}${BOLD}==============================================================================${NC}"
echo -e "测试远端仓库: ${YELLOW}${REPO_URL}${NC}"
echo -e "测试目标分支: ${YELLOW}${BRANCH}${NC}"
echo -e "二进制工具:   ${YELLOW}${BIN}${NC}"
echo ""

# 1. 编译验证
if [[ ! -x "$BIN" ]]; then
    echo -e "${BLUE}[+] 编译 test_sync 测试工具...${NC}"
    make -C "$PROJECT_ROOT" test_sync
fi

# 2. 初始化隔离环境
TEST_BASE="/tmp/ai_space_multihost_test"
HOST_A_HUB="$TEST_BASE/host_a_beijing/master_hub"
HOST_A_CACHE="$TEST_BASE/host_a_beijing/git_cache"

HOST_B_HUB="$TEST_BASE/host_b_shanghai/master_hub"
HOST_B_CACHE="$TEST_BASE/host_b_shanghai/git_cache"

echo -e "${BLUE}[+] 准备多主机模拟运行环境...${NC}"
rm -rf "$TEST_BASE"
mkdir -p "$HOST_A_HUB" "$HOST_A_CACHE" "$HOST_B_HUB" "$HOST_B_CACHE"
echo -e "  - 主机 A (北京) 本地主仓: $HOST_A_HUB"
echo -e "  - 主机 B (上海) 本地主仓: $HOST_B_HUB"
echo ""

# Helper for test assertions
assert_ok() {
    local desc="$1"
    local status="$2"
    if [[ "$status" -eq 0 ]]; then
        echo -e "  ${GREEN}✓ [PASS]${NC} $desc"
    else
        echo -e "  ${RED}✗ [FAIL]${NC} $desc (exit code: $status)"
        exit 1
    fi
}

assert_match() {
    local desc="$1"
    local expected="$2"
    local actual="$3"
    if [[ "$actual" == *"$expected"* ]]; then
        echo -e "  ${GREEN}✓ [PASS]${NC} $desc"
    else
        echo -e "  ${RED}✗ [FAIL]${NC} $desc"
        echo -e "      期望包含: ${YELLOW}$expected${NC}"
        echo -e "      实际输出: ${RED}$actual${NC}"
        exit 1
    fi
}

# ------------------------------------------------------------------------------
# STEP 1: 可用性前置检测 (必须通过才可激活)
# ------------------------------------------------------------------------------
echo -e "${BOLD}【测试阶段 1】各后端可用性前置检测 (check_available 接口)${NC}"

echo "  > 测试 1.1: 真实远端仓库合法性探测 (Positive Test)..."
AVAIL_RES=$("$BIN" check-available "$REPO_URL" "$BRANCH" 2>&1)
assert_match "真实有效 GitHub 仓库应检测通过" "AVAILABILITY_OK" "$AVAIL_RES"

echo "  > 测试 1.2: 不存在或未授权仓库探测拦截 (Negative Test)..."
FAKE_RES=$("$BIN" check-available "git@github.com:weibaohui/non-existent-ai-space-repo-xyz-999.git" "main" 2>&1 || true)
assert_match "非法/不存在仓库必须严格被拦截" "AVAILABILITY_FAILED" "$FAKE_RES"
echo ""

# ------------------------------------------------------------------------------
# STEP 2: 主机 A 本地主仓录入新技能
# ------------------------------------------------------------------------------
echo -e "${BOLD}【测试阶段 2】主机 A 创建初始技能集合${NC}"

# Skill 1: search-engine
mkdir -p "$HOST_A_HUB/search-engine"
cat << 'EOF' > "$HOST_A_HUB/search-engine/SKILL.md"
---
name: search-engine
description: 全球搜索引擎自动化检索与聚合技能
version: 1.0.0
---
# Search Engine Skill
Provides multi-engine search scraping and synthesis.
EOF
cat << 'EOF' > "$HOST_A_HUB/search-engine/search.py"
def do_search(query):
    return f"Results for: {query}"
EOF

# Skill 2: doc-summarizer
mkdir -p "$HOST_A_HUB/doc-summarizer"
cat << 'EOF' > "$HOST_A_HUB/doc-summarizer/SKILL.md"
---
name: doc-summarizer
description: 长文档深度分析与核心摘要生成技能
version: 1.0.0
---
# Document Summarizer
Summarizes Markdown, PDF, and text documents.
EOF

echo -e "  主机 A 创建了技能: ${CYAN}search-engine${NC} 与 ${CYAN}doc-summarizer${NC}"
HASH_A1=$("$BIN" hash "$HOST_A_HUB/search-engine" | awk '{print $2}')
HASH_A2=$("$BIN" hash "$HOST_A_HUB/doc-summarizer" | awk '{print $2}')
echo -e "  - search-engine 指纹:    ${YELLOW}$HASH_A1${NC}"
echo -e "  - doc-summarizer 指纹:   ${YELLOW}$HASH_A2${NC}"
echo ""

# ------------------------------------------------------------------------------
# STEP 3: 主机 A 远端差异漂移检测 (check_drift)
# ------------------------------------------------------------------------------
echo -e "${BOLD}【测试阶段 3】主机 A 漂移检测 (Check Drift)${NC}"
DRIFT_A1=$("$BIN" drift "$REPO_URL" "$BRANCH" "$HOST_A_HUB" "$HOST_A_CACHE")
assert_match "漂移检测应识别到 2 个待推送技能 (local_only=2)" "local_only=2" "$DRIFT_A1"
echo ""

# ------------------------------------------------------------------------------
# STEP 4: 主机 A 全量推送到真实 GitHub 远端仓库 (push_all)
# ------------------------------------------------------------------------------
echo -e "${BOLD}【测试阶段 4】主机 A 推送所有技能至 GitHub 远端仓库 (Push All)${NC}"
PUSH_RES=$("$BIN" push-all "$REPO_URL" "$BRANCH" "$HOST_A_HUB" "$HOST_A_CACHE")
assert_match "全量推送至 Git 远端版本库成功" "PUSH_ALL_OK" "$PUSH_RES"

DRIFT_A2=$("$BIN" drift "$REPO_URL" "$BRANCH" "$HOST_A_HUB" "$HOST_A_CACHE")
assert_match "推送完成后主机 A 与远端达到完全一致 (in_sync=2)" "in_sync=2" "$DRIFT_A2"
echo ""

# ------------------------------------------------------------------------------
# STEP 5: 主机 B 初始启动感知远端技能并拉取 (pull_all)
# ------------------------------------------------------------------------------
echo -e "${BOLD}【测试阶段 5】主机 B 启动并感知远端技能，拉取至本地主仓 (Pull All)${NC}"

# 主机 B 本地主仓初始为空，应感知到 2 个远端独有技能
DRIFT_B1=$("$BIN" drift "$REPO_URL" "$BRANCH" "$HOST_B_HUB" "$HOST_B_CACHE")
assert_match "主机 B 初始识别到 2 个远端独有技能 (remote_only=2)" "remote_only=2" "$DRIFT_B1"

# 主机 B 从远端拉取全部技能
PULL_RES=$("$BIN" pull-all "$REPO_URL" "$BRANCH" "$HOST_B_HUB" "$HOST_B_CACHE")
assert_match "主机 B 全量拉取技能成功" "PULL_ALL_OK" "$PULL_RES"

# 验证主机 B 本地主仓文件存在性与指纹一致性
test -f "$HOST_B_HUB/search-engine/SKILL.md"
assert_ok "主机 B 存在 search-engine/SKILL.md" $?
test -f "$HOST_B_HUB/search-engine/search.py"
assert_ok "主机 B 存在 search-engine/search.py" $?
test -f "$HOST_B_HUB/doc-summarizer/SKILL.md"
assert_ok "主机 B 存在 doc-summarizer/SKILL.md" $?

HASH_B1=$("$BIN" hash "$HOST_B_HUB/search-engine" | awk '{print $2}')
HASH_B2=$("$BIN" hash "$HOST_B_HUB/doc-summarizer" | awk '{print $2}')

if [[ "$HASH_A1" == "$HASH_B1" && "$HASH_A2" == "$HASH_B2" ]]; then
    echo -e "  ${GREEN}✓ [PASS]${NC} 主机 B 技能内容哈希与主机 A 100% 绝对一致！"
else
    echo -e "  ${RED}✗ [FAIL]${NC} 主机 A 与主机 B 技能内容哈希不匹配"
    exit 1
fi
echo ""

# ------------------------------------------------------------------------------
# STEP 6: 主机 B 本地升级 search-engine 并新增 k8s-helper 技能，推送到远端
# ------------------------------------------------------------------------------
echo -e "${BOLD}【测试阶段 6】主机 B 演进技能 (更新 search-engine 并新增 k8s-helper)，推送到远端${NC}"

# 修改 search-engine
cat << 'EOF' >> "$HOST_B_HUB/search-engine/search.py"
def advanced_filter(results):
    return [r for r in results if "AI" in r]
EOF
echo "# Updated by Host B" >> "$HOST_B_HUB/search-engine/SKILL.md"

# 新增 k8s-helper
mkdir -p "$HOST_B_HUB/k8s-helper"
cat << 'EOF' > "$HOST_B_HUB/k8s-helper/SKILL.md"
---
name: k8s-helper
description: Kubernetes 资源调度与集群状态自检技能
version: 1.0.0
---
# K8s Helper
Manages Pods, Deployments, and Helm Releases.
EOF

echo -e "  主机 B 更新了 ${CYAN}search-engine${NC}，新增了 ${CYAN}k8s-helper${NC}"

# 主机 B 检查漂移
DRIFT_B2=$("$BIN" drift "$REPO_URL" "$BRANCH" "$HOST_B_HUB" "$HOST_B_CACHE")
assert_match "主机 B 感知 1 个待推送改动与 1 个本地新增" "local_ahead=1" "$DRIFT_B2"
assert_match "主机 B 感知本地独有技能" "local_only=1" "$DRIFT_B2"

# 主机 B 推送全量改动
PUSH_B_RES=$("$BIN" push-all "$REPO_URL" "$BRANCH" "$HOST_B_HUB" "$HOST_B_CACHE")
assert_match "主机 B 全量推送成功" "PUSH_ALL_OK" "$PUSH_B_RES"
echo ""

# ------------------------------------------------------------------------------
# STEP 7: 主机 A 感知主机 B 的远端提交并拉取，验证多主机最终一致性
# ------------------------------------------------------------------------------
echo -e "${BOLD}【测试阶段 7】主机 A 感知远端变更并拉取更新，验证双向最终一致性${NC}"

# 主机 A 检查漂移：应该发现 search-engine 远端已更新 (remote_ahead=1)，k8s-helper 远端独有 (remote_only=1)
DRIFT_A3=$("$BIN" drift "$REPO_URL" "$BRANCH" "$HOST_A_HUB" "$HOST_A_CACHE")
assert_match "主机 A 感知 search-engine 远端已更新 (remote_ahead=1)" "remote_ahead=1" "$DRIFT_A3"
assert_match "主机 A 感知 k8s-helper 远端新技能 (remote_only=1)" "remote_only=1" "$DRIFT_A3"

# 主机 A 拉取全量更新
PULL_A_RES=$("$BIN" pull-all "$REPO_URL" "$BRANCH" "$HOST_A_HUB" "$HOST_A_CACHE")
assert_match "主机 A 全量拉取成功" "PULL_ALL_OK" "$PULL_A_RES"

# 最终两端一致性校验
DRIFT_A_FINAL=$("$BIN" drift "$REPO_URL" "$BRANCH" "$HOST_A_HUB" "$HOST_A_CACHE")
DRIFT_B_FINAL=$("$BIN" drift "$REPO_URL" "$BRANCH" "$HOST_B_HUB" "$HOST_B_CACHE")

assert_match "主机 A 最终状态：3 个技能全量在同步 (in_sync=3)" "in_sync=3" "$DRIFT_A_FINAL"
assert_match "主机 B 最终状态：3 个技能全量在同步 (in_sync=3)" "in_sync=3" "$DRIFT_B_FINAL"

echo ""
echo -e "${GREEN}${BOLD}==============================================================================${NC}"
echo -e "${GREEN}${BOLD}  🎉 ALL TESTS PASSED! 多主机真实 Git 远端同步双向闭环验证全部通过！${NC}"
echo -e "${GREEN}${BOLD}==============================================================================${NC}"
echo -e "已验证能力项："
echo -e "  1. 真实 GitHub 远端版本库连通性与权限前置强校验 (check_available)"
echo -e "  2. 主机 A (Host-Beijing) 技能收录与远端推送 (push_all)"
echo -e "  3. 主机 B (Host-Shanghai) 跨端漂移感知与全量拉取 (check_drift -> pull_all)"
echo -e "  4. 多主机技能内容确定性哈希对比 (SHA/FNV 100% 一致)"
echo -e "  5. 主机 B 演进改动与新增技能推送 (push_all)"
echo -e "  6. 主机 A 远端增量更新感知 (remote_ahead / remote_only) 与闭环同步 (pull_all)"
echo -e "  7. 多端 0 冲突最终一致性达成 (in_sync=3)"
echo ""
