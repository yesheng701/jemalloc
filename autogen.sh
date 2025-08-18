#!/bin/sh

for i in autoconf; do
    echo "$i"
    $i
    if [ $? -ne 0 ]; then
	echo "Error $? in $i"
	exit 1
    fi
done

export CC=aarch64-unknown-nto-qnx7.1.0-gcc
export CXX=aarch64-unknown-nto-qnx7.1.0-g++
export AR=aarch64-unknown-nto-qnx7.1.0-ar
export RANLIB=aarch64-unknown-nto-qnx7.1.0-ranlib
export QNX_HOST=/home/shaneye/Workspace/crosstools/qnx710_8255/toolchain/aarch64/aarch64_qnx710_8255/host/linux/x86_64
export QNX_TARGET=/home/shaneye/Workspace/crosstools/qnx710_8255/toolchain/aarch64/aarch64_qnx710_8255/target/qnx7
export SYS_ROOT=/home/shaneye/Workspace/crosstools/qnx710_8255/rootfs/ubuntu-base-22.04-arm64-8255-qnx7.1
export PATH=$QNX_HOST/usr/bin:$PATH

./configure \
    --host=aarch64-unknown-nto-qnx7.1.0 \
    --disable-static --enable-shared --enable-cxx --enable-prof --disable-prof-libgcc --disable-prof-gcc \
    CC=aarch64-unknown-nto-qnx7.1.0-gcc \
    CXX=aarch64-unknown-nto-qnx7.1.0-g++ \
    AR=aarch64-unknown-nto-qnx7.1.0-ar \
    RANLIB=aarch64-unknown-nto-qnx7.1.0-ranlib \
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
        -I$QNX_HOST/usr/lib/gcc/aarch64-unknown-nto-qnx7.1.0/8.3.0/include \
        -nostdinc" \
    CXXFLAGS="\
        -Wall \
        -Wextra \
        -Wno-unused-parameter \
        -fPIC \
        -D__QNX__ \
        -D_QNX_SOURCE \
        --sysroot=$SYS_ROOT \
        -I$QNX_TARGET/usr/include/c++/8.3.0 \
        -I$QNX_TARGET/usr/include/c++/8.3.0/aarch64-unknown-nto-qnx7.1.0/ \
        -I$SYS_ROOT/usr/include \
        -I$QNX_TARGET/usr/include" \
    LDFLAGS="--sysroot=$SYS_ROOT -L$SYS_ROOT/usr/lib -latomic -lc++ -lc"

if [ $? -ne 0 ]; then
    echo "Error $? in ./configure"
    exit 1
fi
