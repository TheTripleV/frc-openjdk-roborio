#!/bin/bash
# Fast incremental build using a persistent Docker container.
# First run: creates container, runs setup, copies full source, builds (~20min).
# Subsequent runs: syncs only changed hotspot files, does incremental build (~3min).
#
# To reset: docker rm -f shenandoah-builder
set -e
set -o pipefail

source versions.sh

CONTAINER_NAME="shenandoah-builder"
WIN_PWD=$(pwd -W 2>/dev/null || echo "${PWD}")

# Create container if needed
if ! docker inspect "$CONTAINER_NAME" &>/dev/null; then
    echo "=== Creating persistent container ==="
    MSYS_NO_PATHCONV=1 docker create \
        --name "$CONTAINER_NAME" \
        -v "${WIN_PWD}:/artifacts" \
        ${DOCKER_IMAGE} \
        sleep infinity
fi

# Start container if not running
if [ "$(docker inspect -f '{{.State.Running}}' "$CONTAINER_NAME" 2>/dev/null)" != "true" ]; then
    docker start "$CONTAINER_NAME"
fi

# One-time setup
if ! MSYS_NO_PATHCONV=1 docker exec "$CONTAINER_NAME" test -d /jdk17u-local-build/build 2>/dev/null; then
    echo "=== Running initial setup ==="
    MSYS_NO_PATHCONV=1 docker exec "$CONTAINER_NAME" bash -c "\
        cp /artifacts/*.jinfo /artifacts/*.tar.xz /artifacts/*.sh /artifacts/control /artifacts/postinst /artifacts/prerm . \
        && ./setup.sh"
    echo "=== Initial source copy (slow, only happens once) ==="
    MSYS_NO_PATHCONV=1 docker exec "$CONTAINER_NAME" bash -c "\
        cp -a /artifacts/jdk17u-local /jdk17u-local-build"
    echo "=== Running configure (only happens once) ==="
    MSYS_NO_PATHCONV=1 docker exec "$CONTAINER_NAME" bash -c "\
        source /artifacts/versions.sh && \
        cd /jdk17u-local-build && \
        bash configure \
            --openjdk-target=arm-frc\${YEAR}-linux-gnueabi \
            --with-abi-profile=arm-vfp-sflt \
            --with-jvm-variants=client \
            --with-jvm-features=shenandoahgc \
            --with-native-debug-symbols=zipped \
            --enable-unlimited-crypto \
            --with-sysroot=/usr/local/arm-nilrt-linux-gnueabi/sysroot \
            --with-version-pre=frc \
            --with-version-patch=\${JAVA_PATCH} \
            --with-version-opt=\${YEAR}-\${VER} \
            --disable-warnings-as-errors"
fi

# Sync changed files from the hotspot directory (where our code lives)
echo "=== Syncing changed source files ==="
MSYS_NO_PATHCONV=1 docker exec "$CONTAINER_NAME" bash -c "\
    cp -a /artifacts/jdk17u-local/src/hotspot/. /jdk17u-local-build/src/hotspot/ && \
    cp -a /artifacts/jdk17u-local/make/. /jdk17u-local-build/make/"

# Copy build scripts
MSYS_NO_PATHCONV=1 docker exec "$CONTAINER_NAME" bash -c "\
    cp /artifacts/*.sh /artifacts/control /artifacts/postinst /artifacts/prerm ."

# Build (incremental — make only recompiles changed files)
echo "=== Building ==="
MSYS_NO_PATHCONV=1 docker exec "$CONTAINER_NAME" bash -c "\
    source /artifacts/versions.sh && \
    cd /jdk17u-local-build && \
    make JOBS=${BUILD_JOBS:-16} LOG=cmdlines all legacy-jre-image && \
    cd build/linux-arm-client-release/images && \
    tar czf jre_\${VER}.tar.gz jre && \
    cp -a jre_\${VER}.tar.gz /artifacts && \
    find jre -name \*.diz -delete && \
    find jre -name \*.so -type f | xargs arm-frc\${YEAR}-linux-gnueabi-strip && \
    arm-frc\${YEAR}-linux-gnueabi-strip jre/bin/* jre/lib/jexec && \
    tar czf jre_\${VER}-strip.tar.gz jre && \
    cp -a jre_\${VER}-strip.tar.gz /artifacts"

# Package into ipk
echo "=== Packaging ==="
MSYS_NO_PATHCONV=1 docker exec "$CONTAINER_NAME" bash -c "\
    source /artifacts/versions.sh && \
    cd / && rm -f control.tar.gz data.tar.gz && rm -rf jre && \
    tar xzf /artifacts/jre_\${VER}-strip.tar.gz && \
    tar czf data.tar.gz --transform 's,^jre,usr/local/frc/JRE,' --owner=root --group=root jre && \
    tar czf control.tar.gz control postinst prerm && \
    echo 2.0 > debian-binary && \
    ar r /artifacts/\${IPK_NAME} control.tar.gz data.tar.gz debian-binary"

echo "Build completed in ${SECONDS}s"
