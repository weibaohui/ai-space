#!/usr/bin/env bash
# ==============================================================================
# AI-Space: Multi-Host Dual-Instance Two-Way Synchronization Automated Test
# Simulates Host A (~/.ai-space) and Host B (~/.ai-space-node2) syncing via Git
# ==============================================================================

set -e

REPO_URL="${AI_SPACE_TEST_REPO:-git@github.com:weibaohui/ai-space-sync-test.git}"
BRANCH="${AI_SPACE_TEST_BRANCH:-main}"

HOST_A_HUB="${HOME}/.ai-space/skills"
HOST_A_CACHE="${HOME}/.ai-space/cache/git_remote"

HOST_B_HUB="${HOME}/.ai-space-node2/skills"
HOST_B_CACHE="${HOME}/.ai-space-node2/cache/git_remote"

COLOR_GREEN="\033[0;32m"
COLOR_BLUE="\033[0;34m"
COLOR_YELLOW="\033[0;33m"
COLOR_RED="\033[0;31m"
COLOR_RESET="\033[0m"

log_info() {
    echo -e "${COLOR_BLUE}[INFO]${COLOR_RESET} $1"
}

log_success() {
    echo -e "${COLOR_GREEN}[SUCCESS]${COLOR_RESET} $1"
}

log_warn() {
    echo -e "${COLOR_YELLOW}[WARN]${COLOR_RESET} $1"
}

log_error() {
    echo -e "${COLOR_RED}[ERROR]${COLOR_RESET} $1"
}

if [ ! -f "./test_sync" ]; then
    log_info "Compiling test_sync binary..."
    make test_sync
fi

echo "================================================================================"
echo "           AI-Space 跨主机多实例远端协同同步自动化测试套件"
echo "================================================================================"
echo "远端版本库: $REPO_URL (分支: $BRANCH)"
echo "主机 A 主仓: $HOST_A_HUB"
echo "主机 B 主仓: $HOST_B_HUB"
echo "================================================================================"

# 1. 检测远端仓库可用性
log_info "[Step 1/6] 检测远端 Git 仓库可用性..."
./test_sync check-available "$REPO_URL" "$BRANCH"
log_success "远端仓库检测通过！"

# 2. 准备主机 B 的独立工作目录
log_info "[Step 2/6] 准备主机 B 独立环境..."
mkdir -p "$HOST_B_HUB"
mkdir -p "$HOST_B_CACHE"

# 3. 主机 B 从远端拉取最新全量技能
log_info "[Step 3/6] 主机 B 执行全量拉取 (pull-all)..."
./test_sync pull-all "$REPO_URL" "$BRANCH" "$HOST_B_HUB" "$HOST_B_CACHE"
log_success "主机 B 已成功拉取远端技能！"

# 4. 主机 B 模拟本地更新与新建技能
log_info "[Step 4/6] 主机 B 模拟修改已有技能并新增专属技能..."
TIMESTAMP=$(date +"%Y-%m-%d %H:%M:%S")

# 修改 k8s-helper
if [ -d "$HOST_B_HUB/k8s-helper" ]; then
    echo -e "\n## Host B Patch ($TIMESTAMP)\nSimulated update from Node B." >> "$HOST_B_HUB/k8s-helper/SKILL.md"
fi

# 新增 node-b-expert
mkdir -p "$HOST_B_HUB/node-b-expert"
cat << EOF > "$HOST_B_HUB/node-b-expert/SKILL.md"
---
name: node-b-expert
description: Automated multi-host verification skill authored on Node B.
timestamp: $TIMESTAMP
---
# Node B Expert Skill
Generated automatically during multi-host synchronization test.
EOF

# 主机 B 推送到远端
log_info "主机 B 推送改动至远端版本库 (push-all)..."
./test_sync push-all "$REPO_URL" "$BRANCH" "$HOST_B_HUB" "$HOST_B_CACHE"
log_success "主机 B 改动已推送到远端版本库！"

# 5. 主机 A 检测变动 (漂移感知)
log_info "[Step 5/6] 主机 A 执行漂移比对，感知远端变更..."
DRIFT_OUT=$(./test_sync drift "$REPO_URL" "$BRANCH" "$HOST_A_HUB" "$HOST_A_CACHE")
echo "$DRIFT_OUT"

if echo "$DRIFT_OUT" | grep -q "node-b-expert status=REMOTE_ONLY"; then
    log_success "主机 A 成功感知到主机 B 新增技能: node-b-expert (REMOTE_ONLY)！"
fi
if echo "$DRIFT_OUT" | grep -q "k8s-helper status=REMOTE_AHEAD"; then
    log_success "主机 A 成功感知到主机 B 修改技能: k8s-helper (REMOTE_AHEAD)！"
fi

# 6. 主机 A 从远端拉取并验证一致性
log_info "[Step 6/6] 主机 A 拉取远端更新并进行双端收敛一致性校验..."
./test_sync pull-all "$REPO_URL" "$BRANCH" "$HOST_A_HUB" "$HOST_A_CACHE"

FINAL_DRIFT_A=$(./test_sync drift "$REPO_URL" "$BRANCH" "$HOST_A_HUB" "$HOST_A_CACHE")
FINAL_DRIFT_B=$(./test_sync drift "$REPO_URL" "$BRANCH" "$HOST_B_HUB" "$HOST_B_CACHE")

if echo "$FINAL_DRIFT_A" | grep -q "remote_ahead=0 local_ahead=0 remote_only=0 local_only=0 conflict=0"; then
    log_success "主机 A 已与远端完全一致 (0 漂移)！"
else
    log_error "主机 A 校验失败，仍有未收敛漂移！"
    exit 1
fi

if echo "$FINAL_DRIFT_B" | grep -q "remote_ahead=0 local_ahead=0 remote_only=0 local_only=0 conflict=0"; then
    log_success "主机 B 已与远端完全一致 (0 漂移)！"
else
    log_error "主机 B 校验失败，仍有未收敛漂移！"
    exit 1
fi

echo "================================================================================"
log_success "🎉 跨主机双实例协同同步测试全部通过！双端主仓已实现强一致性收敛！"
echo "================================================================================"
