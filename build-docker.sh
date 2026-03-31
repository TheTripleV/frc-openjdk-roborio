#!/bin/bash
set -e
set -o pipefail

source versions.sh

docker pull ${DOCKER_IMAGE}
# On Windows (Git Bash/MSYS), use pwd -W for the Windows path and disable path conversion
WIN_PWD=$(pwd -W 2>/dev/null || echo "${PWD}")
MSYS_NO_PATHCONV=1 docker run -v "${WIN_PWD}:/artifacts" ${DOCKER_IMAGE} bash -c "\
    cp /artifacts/*.jinfo /artifacts/*.tar.xz /artifacts/*.sh /artifacts/control /artifacts/postinst /artifacts/prerm . \
    && ./setup.sh \
    && BUILD_JOBS=$(nproc) ./build.sh"
echo "Build completed in ${SECONDS}s"
