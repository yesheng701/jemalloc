#!/bin/sh

for i in autoconf; do
    echo "$i"
    $i
    if [ $? -ne 0 ]; then
	echo "Error $? in $i"
	exit 1
    fi
done

export CC=aarch64-unknown-nto-qnx7.0.0-gcc
export CXX=aarch64-unknown-nto-qnx7.0.0-g++
export AR=aarch64-unknown-nto-qnx7.0.0-ar
export RANLIB=aarch64-unknown-nto-qnx7.0.0-ranlib
export QNX_HOST=/home/shaneye/Workspace/crosstools/qnx700_8155/toolchain/aarch64/aarch64_qnx700_8155/host/linux/x86_64
export QNX_TARGET=/home/shaneye/Workspace/crosstools/qnx700_8155/toolchain/aarch64/aarch64_qnx700_8155/target/qnx7
export SYS_ROOT=/home/shaneye/Workspace/crosstools/qnx700_8155/rootfs/ubuntu-base-22.04-arm64-8155-qnx7.0
export PATH=$QNX_HOST/usr/bin:$PATH

echo "./configure --enable-autogen $@"
./configure \
    --host=aarch64-unknown-nto-qnx7.0.0 \
    --disable-static \
    --enable-shared \
    CFLAGS="-D__QNX__ -std=c11 -fno-stack-protector" \
    LDFLAGS="-latomic -lc++ -lc"

if [ $? -ne 0 ]; then
    echo "Error $? in ./configure"
    exit 1
fi
