#!/bin/sh

usage() {
    echo "Usage: $0 <qnx710|qnx700|sa8620p>"
    echo "Example:"
    echo "  $0 qnx710"
    echo "  $0 qnx700"
    echo "  $0 sa8620p"
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
    sa8620p)
        export CROSSTOOL_PATH="/home/shaneye/Workspace/crosstools/nexus_sa8620p"
        export GCC_PATH="$CROSSTOOL_PATH/toolchain/aarch64/aarch64_sa8620p"
        export SYS_ROOT="$CROSSTOOL_PATH/rootfs/sa8620p-ubuntu-base-22.04-aarch64"
        export GCC_HOST="$GCC_PATH/usr/bin/aarch64-oe-linux"
        export OUTPUT="$WORKPACE_PATH/build_sa8620p"
        ;;
    *)
        echo "Error: Unsupported target '$TARGET'. Choose 'qnx710', 'qnx700', or 'sa8620p'."
        usage
        ;;
esac

if [ "$TARGET" = "sa8620p" ]; then
    export CC="$GCC_HOST/aarch64-oe-linux-gcc"
    export CXX="$GCC_HOST/aarch64-oe-linux-g++"
    export AR="$GCC_HOST/aarch64-oe-linux-ar"
    export RANLIB="$GCC_HOST/aarch64-oe-linux-ranlib"
    export PATH="$GCC_HOST:$PATH"
else
    export CC="aarch64-unknown-nto-qnx$QNX_VERSION-gcc"
    export CXX="aarch64-unknown-nto-qnx$QNX_VERSION-gcc"
    export AR="aarch64-unknown-nto-qnx$QNX_VERSION-ar"
    export RANLIB="aarch64-unknown-nto-qnx$QNX_VERSION-ranlib"
    export PATH="$QNX_HOST/usr/bin:$PATH"
fi

if [ "$TARGET" = "sa8620p" ]; then
    echo "Using CROSSTOOL_PATH: $CROSSTOOL_PATH"
    echo "Using GCC_PATH: $GCC_PATH"
    echo "Using SYS_ROOT: $SYS_ROOT"
else
    echo "Using QNX_HOST: $QNX_HOST"
    echo "Using QNX_TARGET: $QNX_TARGET"
    echo "Using SYS_ROOT: $SYS_ROOT"
fi

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
        COMMON_CFLAGS="-Wall -Wextra -Wno-unused-parameter -fPIC"
        COMMON_CFLAGS="$COMMON_CFLAGS -D_QNX_SOURCE --sysroot=$SYS_ROOT"
        COMMON_CXXFLAGS="$COMMON_CFLAGS -std=c++14"
        COMMON_CXXFLAGS="$COMMON_CXXFLAGS -nostdinc++ -isystem $QNX_TARGET/usr/include/c++/v1"
        COMMON_LDFLAGS="--sysroot=$SYS_ROOT -nodefaultlibs"
        COMMON_LDFLAGS="$COMMON_LDFLAGS -L$SYS_ROOT/usr/lib -L$QNX_TARGET/aarch64le/usr/lib"
        COMMON_LDFLAGS="$COMMON_LDFLAGS -lc++ -lc -lm -latomic"
        ../configure \
            --prefix=$OUTPUT \
            --host=aarch64-unknown-nto-qnx$QNX_VERSION \
            --disable-static \
            --enable-shared \
            --enable-cxx \
            --enable-prof \
            --enable-stats \
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
        COMMON_CFLAGS="-Wall -Wextra -Wno-unused-parameter -fPIC"
        COMMON_CFLAGS="$COMMON_CFLAGS -D_QNX_SOURCE -fno-stack-protector"
        COMMON_CFLAGS="$COMMON_CFLAGS --sysroot=$SYS_ROOT"
        COMMON_CFLAGS="$COMMON_CFLAGS -I$SYS_ROOT/usr/include -I$QNX_TARGET/usr/include"
        COMMON_CFLAGS="$COMMON_CFLAGS -I$QNX_HOST/usr/lib/gcc/aarch64-unknown-nto-qnx7.0.0/5.4.0/include"
        COMMON_CFLAGS="$COMMON_CFLAGS -nostdinc"
        COMMON_CXXFLAGS="-Wall -Wextra -Wno-unused-parameter -fPIC"
        COMMON_CXXFLAGS="$COMMON_CXXFLAGS -D__QNX__ -D_QNX_SOURCE --sysroot=$SYS_ROOT"
        COMMON_CXXFLAGS="$COMMON_CXXFLAGS -I$QNX_TARGET/usr/include/c++/5.4.0"
        COMMON_CXXFLAGS="$COMMON_CXXFLAGS -I$QNX_TARGET/usr/include/c++/5.4.0/aarch64-unknown-nto-qnx7.0.0/"
        COMMON_CXXFLAGS="$COMMON_CXXFLAGS -I$SYS_ROOT/usr/include -I$QNX_TARGET/usr/include"
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
            LDFLAGS="$COMMON_LDFLAGS"
        ;;
    sa8620p)
        COMMON_CFLAGS="-Wall -Wextra -Wsign-compare -Wundef -Wno-format-zero-length -Wpointer-arith -Wno-missing-braces -Wno-missing-field-initializers -Wno-missing-attributes -pipe -g3"
        COMMON_CFLAGS="$COMMON_CFLAGS -D__NEXUS__ -D__NEXUS_SA8620P__"
        COMMON_CFLAGS="$COMMON_CFLAGS --sysroot=$SYS_ROOT"
        COMMON_CFLAGS="$COMMON_CFLAGS -I$SYS_ROOT/usr/include"
        COMMON_CFLAGS="$COMMON_CFLAGS -I$SYS_ROOT/usr/lib/aarch64-oe-linux/11.3.0/include"
        COMMON_CFLAGS="$COMMON_CFLAGS -I$GCC_PATH/usr/include"
        COMMON_CFLAGS="$COMMON_CFLAGS -I$GCC_PATH/usr/lib/aarch64-oe-linux/gcc/aarch64-oe-linux/11.3.0/include"
        COMMON_CFLAGS="$COMMON_CFLAGS -I$GCC_PATH/usr/lib/aarch64-oe-linux/gcc/aarch64-oe-linux/11.3.0/include-fixed"
        COMMON_CFLAGS="$COMMON_CFLAGS -std=gnu11"
        COMMON_CXXFLAGS="$COMMON_CFLAGS -std=c++17"
        COMMON_LDFLAGS="--sysroot=$SYS_ROOT -Wl,--as-needed"
        COMMON_LDFLAGS="$COMMON_LDFLAGS -Wl,-rpath-link,$SYS_ROOT/usr/lib:$SYS_ROOT/usr/lib64:$SYS_ROOT/lib:$SYS_ROOT/lib64"
        ../configure \
            --prefix=$OUTPUT \
            --host=aarch64-oe-linux \
            --disable-static \
            --enable-shared \
            --enable-cxx \
            --enable-prof \
            --enable-stats \
            CC=$CC \
            CXX=$CXX \
            AR=$AR \
            RANLIB=$RANLIB \
            CFLAGS="$COMMON_CFLAGS" \
            CXXFLAGS="$COMMON_CXXFLAGS" \
            LDFLAGS="$COMMON_LDFLAGS" \
            LIBS="-lstdc++ -lpthread -lm"
        ;;
    *)
        ;;
esac

if [ $? -ne 0 ]; then
    echo "Error $? in ./configure"
    exit 1
fi

make -j4
