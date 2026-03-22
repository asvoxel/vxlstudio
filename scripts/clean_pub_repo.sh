#!/bin/bash
# scripts/clean_pub_repo.sh - 清理公开仓库中不应存在的私有/大型文件
#
# 用于修复 publish_opensource.sh 首次同步后可能残留的文件。
# 安全操作：只删除已知应排除的目录，不影响其他内容。
#
# 使用方法：
#   ./scripts/clean_pub_repo.sh              # 预览（仅显示将删除的内容）
#   ./scripts/clean_pub_repo.sh --apply      # 执行清理 + 提交 + 推送

set -e

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

info()  { echo -e "${GREEN}✓${NC} $1"; }
warn()  { echo -e "${YELLOW}⚠${NC} $1"; }

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
PUB_REPO="${PROJECT_DIR}/../vxlstudio-pub"

if [ ! -d "${PUB_REPO}/.git" ]; then
    echo -e "${RED}✗${NC} Public repo not found at ${PUB_REPO}"
    exit 1
fi

# 应排除的目录/文件
PRIVATE_DIRS=(
    "studio"
    "models"
    "3rds/vcpkg"
    ".claude"
    "build"
    "node_modules"
)

cd "${PUB_REPO}"

echo "Scanning for files that should not be in public repo..."
echo ""

FOUND=0
for dir in "${PRIVATE_DIRS[@]}"; do
    if [ -e "${dir}" ]; then
        SIZE=$(du -sh "${dir}" 2>/dev/null | cut -f1)
        warn "Found: ${dir}/ (${SIZE})"
        FOUND=1
    fi
done

if [ ${FOUND} -eq 0 ]; then
    info "Public repo is clean. Nothing to remove."
    exit 0
fi

echo ""

if [ "$1" == "--apply" ]; then
    for dir in "${PRIVATE_DIRS[@]}"; do
        if [ -e "${dir}" ]; then
            info "Removing ${dir}/"
            rm -rf "${dir}"
        fi
    done

    # 提交并推送（如果有 git 变化）
    git add -A
    if ! git diff --cached --quiet; then
        git commit -m "chore: remove private/excluded files from public repo"
        git push origin main
        info "Cleaned and pushed."
    else
        info "Removed local files (not tracked by git, no commit needed)."
    fi
else
    echo "Run with --apply to remove these files:"
    echo "  ./scripts/clean_pub_repo.sh --apply"
fi
