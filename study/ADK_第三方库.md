# ADK (Application Development Kit) 第三方库源码安装指南

## 1. 概述

本文档详细说明如何在 Ubuntu 22.04 或 Ubuntu 24.04 系统上通过源码安装 ADK 工程所依赖的所有第三方库。ADK 作为底层基础库，对依赖库的版本和性能有较高要求。

---

## 2. 依赖库清单

### 2.1 核心依赖库

| 库名称      | 推荐版本 | 用途                   | 必要性      |
| ----------- | -------- | ---------------------- | ----------- |
| Boost       | 1.75.0   | 系统、线程、原子操作等 | ⭐⭐⭐ 必需 |
| OpenSSL     | 3.0.12   | 加密通信               | ⭐⭐⭐ 必需 |
| libevent    | 2.1.12   | 事件循环               | ⭐⭐⭐ 必需 |
| libev       | 4.33     | 高性能事件循环         | ⭐⭐ 推荐   |
| libuv       | 1.44.2   | 异步 IO                | ⭐⭐ 推荐   |
| spdlog      | 1.11.0   | 快速日志               | ⭐⭐⭐ 必需 |
| glog        | 0.6.0    | Google 日志            | ⭐ 可选     |
| Google Test | 1.13.0   | 单元测试               | ⭐ 可选     |
| Protobuf    | 3.21.12  | 序列化                 | ⭐⭐ 推荐   |
| RapidJSON   | 1.1.0    | JSON 解析              | ⭐⭐ 推荐   |
| zlib        | 1.3      | 压缩                   | ⭐⭐ 推荐   |
| lz4         | 1.9.4    | 快速压缩               | ⭐ 可选     |
| snappy      | 1.1.9    | 压缩                   | ⭐ 可选     |

---

## 3. 安装前准备

### 3.1 创建安装目录

```bash
# 创建第三方库安装目录
sudo mkdir -p /opt/thirdparty
sudo chown $USER:$USER /opt/thirdparty

# 创建源码下载目录
mkdir -p ~/adk_thirdparty_src
cd ~/adk_thirdparty_src
```

### 3.2 设置环境变量

```bash
# 添加到 ~/.bashrc
cat >> ~/.bashrc << 'EOF'

# ADK Third-party Libraries
export ADK_PREFIX=/opt/thirdparty
export PATH=$ADK_PREFIX/bin:$PATH
export LD_LIBRARY_PATH=$ADK_PREFIX/lib:$ADK_PREFIX/lib64:$LD_LIBRARY_PATH
export PKG_CONFIG_PATH=$ADK_PREFIX/lib/pkgconfig:$ADK_PREFIX/lib64/pkgconfig:$PKG_CONFIG_PATH
export CPLUS_INCLUDE_PATH=$ADK_PREFIX/include:$CPLUS_INCLUDE_PATH
export LIBRARY_PATH=$ADK_PREFIX/lib:$ADK_PREFIX/lib64:$LIBRARY_PATH
EOF

# 使配置生效
source ~/.bashrc

# 验证
echo $ADK_PREFIX
```

### 3.3 安装基础编译工具

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    git \
    wget \
    curl \
    autoconf \
    automake \
    libtool \
    pkg-config \
    python3
```

---

## 4. Boost 库安装 (1.75.0)

### 4.1 下载和解压

```bash
cd ~/adk_thirdparty_src

# 下载 Boost 1.75.0
wget https://boostorg.jfrog.io/artifactory/main/release/1.75.0/source/boost_1_75_0.tar.gz

# 解压
tar -xzf boost_1_75_0.tar.gz
cd boost_1_75_0
```

### 4.2 编译安装

```bash
# 配置 (针对 ADK 优化)
./bootstrap.sh \
    --prefix=/opt/thirdparty \
    --with-libraries=system,thread,filesystem,program_options,log,timer,chrono,atomic,date_time,regex

# 编译并安装 (使用 release 模式)
./b2 -j$(nproc) \
    link=shared \
    threading=multi \
    variant=release \
    runtime-link=shared \
    install

# 验证安装
ls -lh /opt/thirdparty/lib/libboost_system.so*
```

### 4.3 性能优化选项

```bash
# 如果需要更高性能，可以启用 LTO
./b2 -j$(nproc) \
    link=shared \
    threading=multi \
    variant=release \
    runtime-link=shared \
    cxxflags="-flto -O3" \
    linkflags="-flto" \
    install
```

---

## 5. OpenSSL 安装 (3.0.12)

### 5.1 下载和解压

```bash
cd ~/adk_thirdparty_src

# 下载 OpenSSL 3.0.12
wget https://www.openssl.org/source/openssl-3.0.12.tar.gz

# 解压
tar -xzf openssl-3.0.12.tar.gz
cd openssl-3.0.12
```

### 5.2 编译安装

```bash
# 配置 (启用所有优化)
./config \
    --prefix=/opt/thirdparty \
    --openssldir=/opt/thirdparty/ssl \
    --libdir=lib \
    shared \
    zlib-dynamic \
    enable-ssl3 \
    enable-ssl3-method \
    enable-tls1 \
    enable-tls1-method \
    enable-tls1_1 \
    enable-tls1_1-method \
    enable-tls1_2 \
    enable-tls1_2-method \
    enable-tls1_3 \
    enable-asm

# 编译
make -j$(nproc)

# 安装
sudo make install

# 验证
/opt/thirdparty/bin/openssl version
# 应该显示 OpenSSL 3.0.12
```

---

## 6. libevent 安装 (2.1.12)

### 6.1 下载和解压

```bash
cd ~/adk_thirdparty_src

# 下载 libevent
wget https://github.com/libevent/libevent/releases/download/release-2.1.12-stable/libevent-2.1.12-stable.tar.gz

# 解压
tar -xzf libevent-2.1.12-stable.tar.gz
cd libevent-2.1.12-stable
```

### 6.2 编译安装

```bash
# 配置 (启用所有后端)
./configure \
    --prefix=/opt/thirdparty \
    --enable-shared \
    --disable-static \
    --enable-thread-support

# 编译
make -j$(nproc)

# 安装
sudo make install

# 验证
pkg-config --modversion libevent
```

---

## 7. libev 安装 (4.33)

### 7.1 下载和解压

```bash
cd ~/adk_thirdparty_src

# 下载 libev
wget http://dist.schmorp.de/libev/Attic/libev-4.33.tar.gz

# 解压
tar -xzf libev-4.33.tar.gz
cd libev-4.33
```

### 7.2 编译安装

```bash
# 配置
./configure --prefix=/opt/thirdparty

# 编译
make -j$(nproc)

# 安装
sudo make install

# 验证
ls /opt/thirdparty/include/ev.h
```

---

## 8. libuv 安装 (1.44.2)

### 8.1 下载和解压

```bash
cd ~/adk_thirdparty_src

# 下载 libuv
wget https://dist.libuv.org/dist/v1.44.2/libuv-v1.44.2.tar.gz

# 解压
tar -xzf libuv-v1.44.2.tar.gz
cd libuv-v1.44.2
```

### 8.2 编译安装

```bash
# 配置
./autogen.sh
./configure --prefix=/opt/thirdparty

# 编译
make -j$(nproc)

# 安装
sudo make install

# 验证
pkg-config --modversion libuv
```

---

## 9. spdlog 安装 (1.11.0)

### 9.1 下载和解压

```bash
cd ~/adk_thirdparty_src

# 下载 spdlog
wget https://github.com/gabime/spdlog/archive/refs/tags/v1.11.0.tar.gz -O spdlog-1.11.0.tar.gz

# 解压
tar -xzf spdlog-1.11.0.tar.gz
cd spdlog-1.11.0
```

### 9.2 编译安装

```bash
# 创建构建目录
mkdir build && cd build

# 配置 CMake (针对 ADK 优化)
cmake .. \
    -DCMAKE_INSTALL_PREFIX=/opt/thirdparty \
    -DSPDLOG_BUILD_SHARED=ON \
    -DSPDLOG_BUILD_EXAMPLE=OFF \
    -DSPDLOG_FMT_EXTERNAL=OFF

# 编译
make -j$(nproc)

# 安装
sudo make install

# 验证
ls /opt/thirdparty/include/spdlog/spdlog.h
```

---

## 10. glog 安装 (0.6.0)

### 10.1 下载和解压

```bash
cd ~/adk_thirdparty_src

# 下载 glog
wget https://github.com/google/glog/archive/refs/tags/v0.6.0.tar.gz -O glog-0.6.0.tar.gz

# 解压
tar -xzf glog-0.6.0.tar.gz
cd glog-0.6.0
```

### 10.2 编译安装

```bash
# 创建构建目录
mkdir build && cd build

# 配置 CMake
cmake .. \
    -DCMAKE_INSTALL_PREFIX=/opt/thirdparty \
    -DBUILD_SHARED_LIBS=ON

# 编译
make -j$(nproc)

# 安装
sudo make install

# 验证
ls /opt/thirdparty/lib/libglog*
```

---

## 11. Protobuf 安装 (3.21.12)

### 11.1 下载和解压

```bash
cd ~/adk_thirdparty_src

# 下载 Protobuf
wget https://github.com/protocolbuffers/protobuf/releases/download/v3.21.12/protobuf-cpp-3.21.12.tar.gz

# 解压
tar -xzf protobuf-cpp-3.21.12.tar.gz
cd protobuf-3.21.12
```

### 11.2 编译安装

```bash
# 配置
./configure \
    --prefix=/opt/thirdparty \
    --enable-shared \
    --disable-static

# 编译
make -j$(nproc)

# 安装
sudo make install

# 更新链接库缓存
sudo ldconfig

# 验证
/opt/thirdparty/bin/protoc --version
```

---

## 12. RapidJSON 安装 (1.1.0)

### 12.1 下载和解压

```bash
cd ~/adk_thirdparty_src

# 下载 RapidJSON
wget https://github.com/Tencent/rapidjson/archive/refs/tags/v1.1.0.tar.gz -O rapidjson-1.1.0.tar.gz

# 解压
tar -xzf rapidjson-1.1.0.tar.gz
cd rapidjson-1.1.0
```

### 12.2 编译安装

```bash
# 创建构建目录
mkdir build && cd build

# 配置 CMake (纯头文件库)
cmake .. \
    -DCMAKE_INSTALL_PREFIX=/opt/thirdparty \
    -DRAPIDJSON_BUILD_DOC=OFF \
    -DRAPIDJSON_BUILD_EXAMPLES=OFF \
    -DRAPIDJSON_BUILD_TESTS=OFF

# 安装
sudo make install

# 验证
ls /opt/thirdparty/include/rapidjson/rapidjson.h
```

---

## 13. Google Test 安装 (1.13.0)

### 13.1 下载和解压

```bash
cd ~/adk_thirdparty_src

# 下载 Google Test
wget https://github.com/google/googletest/archive/refs/tags/v1.13.0.tar.gz -O googletest-1.13.0.tar.gz

# 解压
tar -xzf googletest-1.13.0.tar.gz
cd googletest-1.13.0
```

### 13.2 编译安装

```bash
# 创建构建目录
mkdir build && cd build

# 配置 CMake
cmake .. \
    -DCMAKE_INSTALL_PREFIX=/opt/thirdparty \
    -DBUILD_SHARED_LIBS=ON

# 编译
make -j$(nproc)

# 安装
sudo make install

# 验证
ls /opt/thirdparty/lib/libgtest*
```

---

## 14. zlib 安装 (1.3)

### 14.1 下载和解压

```bash
cd ~/adk_thirdparty_src

# 下载 zlib
wget https://zlib.net/zlib-1.3.tar.gz

# 解压
tar -xzf zlib-1.3.tar.gz
cd zlib-1.3
```

### 14.2 编译安装

```bash
# 配置
./configure --prefix=/opt/thirdparty

# 编译
make -j$(nproc)

# 安装
sudo make install

# 验证
cat /opt/thirdparty/include/zlib.h | grep "ZLIB_VERSION"
```

---

## 15. lz4 安装 (1.9.4)

### 15.1 下载和解压

```bash
cd ~/adk_thirdparty_src

# 下载 lz4
wget https://github.com/lz4/lz4/archive/refs/tags/v1.9.4.tar.gz -O lz4-1.9.4.tar.gz

# 解压
tar -xzf lz4-1.9.4.tar.gz
cd lz4-1.9.4
```

### 15.2 编译安装

```bash
# 编译
make -j$(nproc)

# 安装
sudo make install PREFIX=/opt/thirdparty

# 验证
/opt/thirdparty/bin/lz4 --version
```

---

## 16. snappy 安装 (1.1.9)

### 16.1 下载和解压

```bash
cd ~/adk_thirdparty_src

# 下载 snappy
wget https://github.com/google/snappy/archive/refs/tags/1.1.9.tar.gz -O snappy-1.1.9.tar.gz

# 解压
tar -xzf snappy-1.1.9.tar.gz
cd snappy-1.1.9
```

### 16.2 编译安装

```bash
# 创建构建目录
mkdir build && cd build

# 配置 CMake
cmake .. \
    -DCMAKE_INSTALL_PREFIX=/opt/thirdparty \
    -DBUILD_SHARED_LIBS=ON

# 编译
make -j$(nproc)

# 安装
sudo make install

# 验证
ls /opt/thirdparty/lib/libsnappy*
```

---

## 17. 一键安装脚本

创建 `install_adk_deps.sh`:

```bash
#!/bin/bash

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

PREFIX=/opt/thirdparty
SRC_DIR=~/adk_thirdparty_src

info() { echo -e "${GREEN}[INFO]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; }
step() { echo -e "${BLUE}[STEP]${NC} $1"; }

# 创建目录
setup_environment() {
    step "Setting up environment..."
    sudo mkdir -p $PREFIX
    sudo chown $USER:$USER $PREFIX
    mkdir -p $SRC_DIR
    info "Environment setup complete"
}

# 安装 Boost
install_boost() {
    step "Installing Boost 1.75.0..."
    cd $SRC_DIR
  
    if [ ! -f "boost_1_75_0.tar.gz" ]; then
        wget https://boostorg.jfrog.io/artifactory/main/release/1.75.0/source/boost_1_75_0.tar.gz
    fi
  
    tar -xzf boost_1_75_0.tar.gz
    cd boost_1_75_0
  
    ./bootstrap.sh --prefix=$PREFIX \
        --with-libraries=system,thread,filesystem,program_options,log,timer,chrono,atomic,date_time,regex
  
    ./b2 -j$(nproc) \
        link=shared threading=multi variant=release runtime-link=shared \
        install
  
    info "Boost installed successfully"
}

# 安装 OpenSSL
install_openssl() {
    step "Installing OpenSSL 3.0.12..."
    cd $SRC_DIR
  
    if [ ! -f "openssl-3.0.12.tar.gz" ]; then
        wget https://www.openssl.org/source/openssl-3.0.12.tar.gz
    fi
  
    tar -xzf openssl-3.0.12.tar.gz
    cd openssl-3.0.12
  
    ./config --prefix=$PREFIX --openssldir=$PREFIX/ssl --libdir=lib shared zlib-dynamic enable-asm
    make -j$(nproc)
    sudo make install
  
    info "OpenSSL installed successfully"
}

# 安装 libevent
install_libevent() {
    step "Installing libevent 2.1.12..."
    cd $SRC_DIR
  
    if [ ! -f "libevent-2.1.12-stable.tar.gz" ]; then
        wget https://github.com/libevent/libevent/releases/download/release-2.1.12-stable/libevent-2.1.12-stable.tar.gz
    fi
  
    tar -xzf libevent-2.1.12-stable.tar.gz
    cd libevent-2.1.12-stable
  
    ./configure --prefix=$PREFIX --enable-shared --disable-static --enable-thread-support
    make -j$(nproc)
    sudo make install
  
    info "libevent installed successfully"
}

# 安装 spdlog
install_spdlog() {
    step "Installing spdlog 1.11.0..."
    cd $SRC_DIR
  
    if [ ! -f "spdlog-1.11.0.tar.gz" ]; then
        wget https://github.com/gabime/spdlog/archive/refs/tags/v1.11.0.tar.gz -O spdlog-1.11.0.tar.gz
    fi
  
    tar -xzf spdlog-1.11.0.tar.gz
    cd spdlog-1.11.0
  
    mkdir build && cd build
    cmake .. -DCMAKE_INSTALL_PREFIX=$PREFIX -DSPDLOG_BUILD_SHARED=ON
    make -j$(nproc)
    sudo make install
  
    info "spdlog installed successfully"
}

# 主函数
main() {
    info "========================================="
    info "ADK Third-party Dependencies Installer"
    info "========================================="
  
    setup_environment
    install_boost
    install_openssl
    install_libevent
    install_spdlog
  
    info "========================================="
    info "Core dependencies installed!"
    info "========================================="
    info ""
    info "Installation prefix: $PREFIX"
    info ""
    info "Environment setup:"
    info "  source ~/.bashrc"
    info ""
    info "Next steps:"
    info "  1. Verify installations"
    info "  2. Compile ADK project"
}

main "$@"
```

使用方法:

```bash
chmod +x install_adk_deps.sh
./install_adk_deps.sh
```

---

## 18. 验证安装

### 18.1 检查所有库

```bash
# 检查 Boost
ls /opt/thirdparty/lib/libboost* | wc -l

# 检查 OpenSSL
/opt/thirdparty/bin/openssl version

# 检查 libevent
pkg-config --modversion libevent

# 检查 spdlog
ls /opt/thirdparty/include/spdlog/spdlog.h

# 检查 Protobuf
/opt/thirdparty/bin/protoc --version
```

### 18.2 编译测试程序

创建 `test_adk_deps.cpp`:

```cpp
#include <boost/system/error_code.hpp>
#include <spdlog/spdlog.h>
#include <event2/event.h>
#include <iostream>

int main() {
    std::cout << "Testing ADK dependencies..." << std::endl;
  
    // Test Boost
    boost::system::error_code ec;
    std::cout << "✓ Boost works" << std::endl;
  
    // Test spdlog
    spdlog::info("Testing spdlog");
    std::cout << "✓ spdlog works" << std::endl;
  
    // Test libevent
    struct event_base* base = event_base_new();
    if (base) {
        std::cout << "✓ libevent works" << std::endl;
        event_base_free(base);
    }
  
    std::cout << "\nAll dependencies are working!" << std::endl;
    return 0;
}
```

编译:

```bash
g++ -o test_adk_deps test_adk_deps.cpp \
    -I/opt/thirdparty/include \
    -L/opt/thirdparty/lib \
    -lboost_system -lspdlog -levent -pthread

./test_adk_deps
```

---

## 19. 常见问题

### Q1: Boost 编译时间过长

**解决**:

```bash
# 只编译需要的模块
./b2 -j$(nproc) --with-system --with-thread --with-filesystem install
```

### Q2: 链接错误

**解决**:

```bash
# 更新动态链接库缓存
sudo ldconfig

# 或者添加配置
echo "/opt/thirdparty/lib" | sudo tee /etc/ld.so.conf.d/thirdparty.conf
sudo ldconfig
```

### Q3: 找不到头文件

**解决**:

```bash
# 验证环境变量
echo $CPLUS_INCLUDE_PATH

# 重新设置
export CPLUS_INCLUDE_PATH=/opt/thirdparty/include:$CPLUS_INCLUDE_PATH
```

---

## 20. 总结

完成以上步骤后，您应该成功通过源码安装了 ADK 所需的所有第三方库。

**检查清单**:

- ✅ Boost 1.75.0 已安装
- ✅ OpenSSL 3.0.12 已安装
- ✅ libevent 2.1.12 已安装
- ✅ spdlog 1.11.0 已安装
- ✅ 其他依赖库已安装

**下一步**:

- 参考 `ADK_编译配置.md` 编译 ADK 工程
- 确保编译时指定正确的库路径

祝您安装顺利！🎉
