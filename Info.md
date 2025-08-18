## QNX
./autogen.sh --build=x86_64-linux-gnu --host=aarch64-unknown-nto-qnx7.1.0 CPPFLAGS=-D_QNX_SOURCE LDFLAGS=-lc++ --prefix=${PWD}/install

#QNX profiling
    cd ${SRC_DIR}/${source}/ && ./autogen.sh --enable-cxx --enable-prof --disable-prof-libgcc --disable-prof-gcc --build=x86_64-linux-gnu --host=aarch64-unknown-nto-qnx7.1.0 CPPFLAGS=-D_QNX_SOURCE LDFLAGS=-lc++ --enable-shared --prefix=${PWD}/install && make -j${CPU_CORES} && make install

