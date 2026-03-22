#!/bin/bash
# scripts/publish_opensource.sh - 发布公开版本到 GitHub
#
# 将源码（排除私有部分）同步到 github.com/asvoxel/vxlstudio
#
# 发版流程：
#   1. 确保代码已提交并推送到 origin (asfly.ltd)
#   2. ./scripts/publish_opensource.sh              # 同步 + 提交（预览）
#   3. ./scripts/publish_opensource.sh --push       # 同步 + 提交 + 推送 + 打 tag
#
# Studio 二进制发布（在编译完成后）：
#   ./scripts/publish_opensource.sh --release       # 同步 + 推送 + 上传 Release 附件
#
# 前提：
#   公开仓库已 clone 到 ../vxlstudio-pub/
#   git clone git@github.com:asvoxel/vxlstudio.git ../vxlstudio-pub

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}✓${NC} $1"; }
warn()  { echo -e "${YELLOW}⚠${NC} $1"; }
error() { echo -e "${RED}✗${NC} $1"; exit 1; }

# ─── 路径 ─────────────────────────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
PUB_REPO="${PROJECT_DIR}/../vxlstudio-pub"

# ─── 检查前提 ─────────────────────────────────────────────────────────────────

if [ ! -d "${PUB_REPO}/.git" ]; then
    error "Public repo not found at ${PUB_REPO}\nClone it:\n  git clone git@github.com:asvoxel/vxlstudio.git ${PUB_REPO}"
fi

# 检查未提交的变更
if ! git -C "${PROJECT_DIR}" diff --quiet || ! git -C "${PROJECT_DIR}" diff --cached --quiet; then
    error "Uncommitted changes in source repo. Commit first."
fi

# ─── 读取版本 ─────────────────────────────────────────────────────────────────

# 从 CMakeLists.txt 提取版本（支持多行 project() 声明）
VERSION=$(grep -A5 'project(' "${PROJECT_DIR}/CMakeLists.txt" \
    | grep 'VERSION' | head -1 \
    | sed 's/.*VERSION[[:space:]]*\([0-9.]*\).*/\1/' 2>/dev/null)
if [ -z "${VERSION}" ]; then
    VERSION="0.0.0"
    warn "Cannot extract version from CMakeLists.txt, using ${VERSION}"
fi

COMMIT=$(git -C "${PROJECT_DIR}" rev-parse --short HEAD)
BRANCH=$(git -C "${PROJECT_DIR}" symbolic-ref --short HEAD 2>/dev/null || echo "detached")

echo "======================================"
echo "  VxlStudio Open Source Publish"
echo "======================================"
echo "  Version:  ${VERSION}"
echo "  Commit:   ${COMMIT} (${BRANCH})"
echo "  Source:   ${PROJECT_DIR}"
echo "  Target:   ${PUB_REPO}"
echo "======================================"
echo ""

# ─── 排除列表 ─────────────────────────────────────────────────────────────────

# 私有内容 + 大型第三方 + 构建产物
EXCLUDE=(
    --exclude='.git'
    --exclude='.git/'
    --exclude='studio/'
    --exclude='models/'
    --exclude='build/'
    --exclude='.claude/'
    --exclude='__pycache__/'
    --exclude='*.pyc'
    --exclude='.DS_Store'
    --exclude='node_modules/'
    --exclude='dist/'
    --exclude='*.egg-info/'
    --exclude='3rds/vcpkg/'
    --exclude='3rds/*.tar.gz'
    --exclude='3rds/*.zip'
)

# ─── 同步文件 ─────────────────────────────────────────────────────────────────

info "Syncing public source files..."

# rsync: 删除目标中源不存在的文件（但保留 .git/ LICENSE studio-releases/）
rsync -av --delete \
    "${EXCLUDE[@]}" \
    --exclude='.git/' \
    --filter='protect .git/' \
    --filter='protect LICENSE' \
    --filter='protect studio-releases/' \
    "${PROJECT_DIR}/" "${PUB_REPO}/"

# ─── 确保关键文件存在 ─────────────────────────────────────────────────────────

# 生成 .gitignore（如果不存在）
if [ ! -f "${PUB_REPO}/.gitignore" ]; then
    info "Generating .gitignore"
    cat > "${PUB_REPO}/.gitignore" <<'GITIGNORE'
# Build
build/
install/

# Python
__pycache__/
*.pyc
*.egg-info/
*.so

# IDE
.vscode/
.idea/
*.swp
*~

# OS
.DS_Store
Thumbs.db

# Studio releases (binary, managed via GitHub Release)
studio-releases/
GITIGNORE
fi

info "Sync complete."

# ─── 统计变化 ─────────────────────────────────────────────────────────────────

cd "${PUB_REPO}"

git add -A

if git diff --cached --quiet; then
    warn "No changes to publish (source identical to last publish)"
    exit 0
fi

# 显示变化统计
echo ""
info "Changes summary:"
git diff --cached --stat | tail -5
echo ""

# ─── 提交 ─────────────────────────────────────────────────────────────────────

COMMIT_MSG="release: v${VERSION}

Source commit: ${COMMIT} (${BRANCH})
Sync date: $(date +%Y-%m-%d)

Excludes: studio/ (binary via Release), models/"

info "Committing: v${VERSION}"
git commit -m "${COMMIT_MSG}"

# ─── 推送 / 发布 ──────────────────────────────────────────────────────────────

case "$1" in
    --push|--release)
        info "Pushing to GitHub..."
        git push origin main

        # 打 tag
        TAG="v${VERSION}"
        if git rev-parse "${TAG}" >/dev/null 2>&1; then
            warn "Tag ${TAG} already exists, skipping"
        else
            info "Creating tag ${TAG}"
            git tag -a "${TAG}" -m "VxlStudio v${VERSION}"
            git push origin "${TAG}"
        fi

        if [ "$1" == "--release" ]; then
            echo ""
            info "Creating GitHub Release..."

            # 查找 studio 二进制文件
            RELEASE_DIR="${PROJECT_DIR}/studio-releases"
            if [ -d "${RELEASE_DIR}" ] && [ "$(ls -A "${RELEASE_DIR}" 2>/dev/null)" ]; then
                # 使用 gh CLI 创建 release 并上传附件
                if command -v gh &>/dev/null; then
                    gh release create "${TAG}" \
                        --repo asvoxel/vxlstudio \
                        --title "VxlStudio v${VERSION}" \
                        --notes "See CHANGELOG for details." \
                        "${RELEASE_DIR}"/*
                    info "Release created with studio binaries."
                else
                    warn "gh CLI not found. Create release manually:"
                    echo "  gh release create ${TAG} ${RELEASE_DIR}/*"
                fi
            else
                warn "No studio binaries found in ${RELEASE_DIR}/"
                warn "Create release manually and upload .dmg/.exe/.deb"
                if command -v gh &>/dev/null; then
                    gh release create "${TAG}" \
                        --repo asvoxel/vxlstudio \
                        --title "VxlStudio v${VERSION}" \
                        --notes "Source release. Studio binaries to be added."
                    info "Draft release created (no binaries attached)."
                fi
            fi
        fi

        echo ""
        echo "======================================"
        echo "  Published: v${VERSION}"
        echo "  https://github.com/asvoxel/vxlstudio"
        echo "======================================"
        ;;
    *)
        echo ""
        echo "======================================"
        echo "  Committed locally: v${VERSION}"
        echo ""
        echo "  To push:    ./scripts/publish_opensource.sh --push"
        echo "  To release: ./scripts/publish_opensource.sh --release"
        echo "======================================"
        ;;
esac

info "Done!"
