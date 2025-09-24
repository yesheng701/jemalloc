#!/bin/sh

usage() {
    echo "Usage: $0 <qnx710|qnx700>"
    echo "Example:"
    echo "  $0 qnx710"
    echo "  $0 qnx700"
    exit 1
}

if [ $# -ne 1 ]; then
    usage
fi

TARGET=$1
WORKPACE_PATH=$(cd "$(dirname "")"; pwd)

case "$TARGET" in
    qnx710)
        export QNX_VERSION="7.1.0"
        export QNX_BASE="/home/shaneye/Workspace/crosstools/qnx710_8255"
        export SYS_ROOT="$QNX_BASE/rootfs/ubuntu-base-22.04-arm64-8255-qnx7.1"
        export QNX_HOST="$QNX_BASE/toolchain/aarch64/aarch64_qnx710_8255/host/linux/x86_64"
        export QNX_TARGET="$QNX_BASE/toolchain/aarch64/aarch64_qnx710_8255/target/qnx7"
        export OUTPUT="$WORKPACE_PATH/build_qnx710"
        ;;
    qnx700)
        export QNX_VERSION="7.0.0"
        export QNX_BASE="/home/shaneye/Workspace/crosstools/qnx700_8155"
        export SYS_ROOT="$QNX_BASE/rootfs/ubuntu-base-22.04-arm64-8155-qnx7.0"
        export QNX_HOST="$QNX_BASE/toolchain/aarch64/aarch64_qnx700_8155/host/linux/x86_64"
        export QNX_TARGET="$QNX_BASE/toolchain/aarch64/aarch64_qnx700_8155/target/qnx7"
        export OUTPUT="$WORKPACE_PATH/build_qnx700"
        ;;
    *)
        echo "Error: Unsupported target '$TARGET'. Choose 'qnx710' or 'qnx700'."
        usage
        ;;
esac

export CC="aarch64-unknown-nto-qnx$QNX_VERSION-gcc"
export CXX="aarch64-unknown-nto-qnx$QNX_VERSION-gcc"
export AR="aarch64-unknown-nto-qnx$QNX_VERSION-ar"
export RANLIB="aarch64-unknown-nto-qnx$QNX_VERSION-ranlib"
export PATH="$QNX_HOST/usr/bin:$PATH"

echo "Using QNX_HOST: $QNX_HOST"
echo "Using QNX_TARGET: $QNX_TARGET"
echo "Using SYS_ROOT: $SYS_ROOT"
echo "Using CC: $CC"
echo "Using CXX: $CXX"
echo "Using AR: $AR"
echo "Using RANLIB: $RANLIB"
echo "Build OUTPUT will be in: $OUTPUT"

for i in autoconf; do
    echo "$i"
    $i
    if [ $? -ne 0 ]; then
    "Error $? in $i"
    exit 1
    fi
done

mkdir -p $OUTPUT && cd $OUTPUT

echo "Configuring for $TARGET..."

case "$TARGET" in
    qnx710)
        COMMON_CFLAGS="-Wall -Wextra -Wno-unused-parameter -fPIC -D__QNX__ -D_QNX_SOURCE --sysroot=$SYS_ROOT"
        COMMON_CXXFLAGS="$COMMON_CFLAGS -std=c++14 -nostdinc++ -isystem $QNX_TARGET/usr/include/c++/v1"
        COMMON_LDFLAGS="\
            --sysroot=$SYS_ROOT \
            -nodefaultlibs \
            -L$SYS_ROOT/usr/lib \
            -L$QNX_TARGET/aarch64le/usr/lib \
            -lc++ -lc -lm -latomic"
        ../configure \
            --prefix=$OUTPUT \
            --host=aarch64-unknown-nto-qnx$QNX_VERSION \
            --disable-static \
            --enable-shared \
            --enable-cxx \
            --enable-prof \
            --disable-prof-libgcc \
            --disable-prof-gcc \
            CC=$CC \
            CXX=$CXX \
            AR=$AR \
            RANLIB=$RANLIB \
            CFLAGS="$COMMON_CFLAGS" \
            CXXFLAGS="$COMMON_CXXFLAGS" \
            LDFLAGS="$COMMON_LDFLAGS" \
            LIBS="-lc++ -lc -lm -latomic"
        ;;
    qnx700)
        ../configure \
            --prefix=$OUTPUT \
            --host=aarch64-unknown-nto-qnx$QNX_VERSION \
            --disable-static \
            --enable-shared \
            --enable-cxx \
            --enable-prof \
            --disable-prof-libgcc \
            --disable-prof-gcc \
            CC=$CC \
            CXX=$CXX \
            AR=$AR \
            RANLIB=$RANLIB \
            CFLAGS="\
                -Wall \
                -Wextra \
                -Wno-unused-parameter \
                -fPIC \
                -D__QNX__ \
                -D_QNX_SOURCE \
                -fno-stack-protector \
                --sysroot=$SYS_ROOT \
                -I$SYS_ROOT/usr/include \
                -I$QNX_TARGET/usr/include \
                -I$QNX_HOST/usr/lib/gcc/aarch64-unknown-nto-qnx7.0.0/5.4.0/include \
                -nostdinc" \
            CXXFLAGS="\
                -Wall \
                -Wextra \
                -Wno-unused-parameter \
                -fPIC \
                -D__QNX__ \
                -D_QNX_SOURCE \
                --sysroot=$SYS_ROOT \
                -I$QNX_TARGET/usr/include/c++/5.4.0 \
                -I$QNX_TARGET/usr/include/c++/5.4.0/aarch64-unknown-nto-qnx7.0.0/ \
                -I$SYS_ROOT/usr/include \
                -I$QNX_TARGET/usr/include" \
            LDFLAGS="--sysroot=$SYS_ROOT -L$SYS_ROOT/usr/lib -latomic -lc++ -lc"
        ;;
    *)
        ;;
esac


if [ $? -ne 0 ]; then
    echo "Error $? in ./configure"
    exit 1
fi

make -j4
