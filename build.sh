#!/bin/bash
set -e
set -o pipefail

source versions.sh

JVM_VARIANT=client
JVM_FEATURES=shenandoahgc
#JVM_VARIANT=minimal1
#JVM_FEATURES=all-gcs,jvmti,services,vm-structs

# Use local source tree (mounted at /artifacts/jdk17u-local) instead of downloading
cp -a /artifacts/jdk17u-local jdk17u-local-build
pushd jdk17u-local-build
bash configure \
	--openjdk-target=arm-frc${YEAR}-linux-gnueabi \
	--with-abi-profile=arm-vfp-sflt \
	--with-jvm-variants=${JVM_VARIANT} \
	--with-jvm-features=${JVM_FEATURES} \
	--with-native-debug-symbols=zipped \
	--enable-unlimited-crypto \
	--with-sysroot=/usr/local/arm-nilrt-linux-gnueabi/sysroot \
	--with-version-pre=frc \
	--with-version-patch=${JAVA_PATCH} \
	--with-version-opt=${YEAR}-${VER} \
	--disable-warnings-as-errors
make JOBS=${BUILD_JOBS:-$(nproc)} LOG=cmdlines all legacy-jre-image
pushd build/linux-arm-${JVM_VARIANT}-release/images
tar czf jre_${VER}.tar.gz jre
chown -R `id -u`:`id -g` jre_${VER}.tar.gz
cp -a jre_${VER}.tar.gz /artifacts
find jre -name \*.diz -delete
find jre -name \*.so -type f | xargs arm-frc${YEAR}-linux-gnueabi-strip
arm-frc${YEAR}-linux-gnueabi-strip jre/bin/* jre/lib/jexec
tar czf jre_${VER}-strip.tar.gz jre
chown -R `id -u`:`id -g` jre_${VER}-strip.tar.gz
cp -a jre_${VER}-strip.tar.gz /artifacts
popd
popd

rm -f control.tar.gz data.tar.gz
rm -rf jre

tar xzf /artifacts/jre_${VER}-strip.tar.gz
tar czf data.tar.gz \
    --transform "s,^jre,usr/local/frc/JRE," \
    --owner=root \
    --group=root \
    jre
tar czf control.tar.gz control postinst prerm
echo 2.0 > debian-binary
ar r /artifacts/${IPK_NAME} control.tar.gz data.tar.gz debian-binary
