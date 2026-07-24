# AAF (AMI Application Framework) 第三方库源码安装指南

## 1. 概述

本文档详细说明如何在 Ubuntu 22.04 或 Ubuntu 24.04 系统上通过源码安装 AAF 工程所依赖的所有第三方库。包括版本选择、下载链接、编译步骤和安装验证。

---

## 2. 依赖库清单

### 2.1 核心依赖库

| 库名称 | 推荐版本 | 用途 | 必要性 |
|--------|---------|------|--------|
| Boost | 1.75.0 | 系统、线程、日志等基础功能 | ⭐⭐⭐ 必需 |
| OpenSSL | 3.0.12 | 加密通信 | ⭐⭐⭐ 必需 |
| libevent | 2.1.12 | 事件循环 | ⭐⭐⭐ 必需 |
| spdlog | 1.11.0 | 快速日志库 | ⭐⭐⭐ 必需 |
| Protobuf | 3.21.12 | 序列化 | ⭐⭐ 推荐 |
| RapidJSON | 1.1.0 | JSON 解析 | ⭐⭐ 推荐 |
| Google Test | 1.13.0 | 单元测试 | ⭐ 可选 |
| etcd-cpp-api | 0.1.5 | 配置中心客户端 | ⭐⭐ 推荐 |
| zlib | 1.3 | 压缩 | ⭐⭐ 推荐 |
| lz4 | 1.9.4 | 快速压缩 | ⭐ 可选 |

---

## 3. 安装前准备

### 3.1 创建安装目录

```bash
# 创建第三方库安装目录
sudo mkdir -p /opt/thirdparty
sudo chown $USER:$USER /opt/thirdparty

# 创建源码下载目录
mkdir -p ~/thirdparty_src
cd ~/thirdparty_src
```

### 3.2 设置环境变量

```bash
# 临时设置 (当前终端)
export PREFIX=/opt/thirdparty
export PATH=$PREFIX/bin:$PATH
export LD_LIBRARY_PATH=$PREFIX/lib:$PREFIX/lib64:$LD_LIBRARY_PATH
export PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig:$PREFIX/lib64/pkgconfig:$PKG_CONFIG_PATH
export CPLUS_INCLUDE_PATH=$PREFIX/include:$CPLUS_INCLUDE_PATH

# 永久设置 (添加到 ~/.bashrc)
echo 'export PREFIX=/opt/thirdparty' >> ~/.bashrc
echo 'export PATH=$PREFIX/bin:$PATH' >> ~/.bashrc
echo 'export LD_LIBRARY_PATH=$PREFIX/lib:$PREFIX/lib64:$LD_LIBRARY_PATH' >> ~/.bashrc
echo 'export PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig:$PREFIX/lib64/pkgconfig:$PKG_CONFIG_PATH' >> ~/.bashrc
echo 'export CPLUS_INCLUDE_PATH=$PREFIX/include:$CPLUS_INCLUDE_PATH' >> ~/.bashrc

source ~/.bashrc
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
    pkg-config
```

---

## 4. Boost 库安装 (1.75.0)

### 4.1 下载源码

```bash
cd ~/thirdparty_src

# 下载 Boost 1.75.0
wget https://boostorg.jfrog.io/artifactory/main/release/1.75.0/source/boost_1_75_0.tar.gz

# 解压
tar -xzf boost_1_75_0.tar.gz
cd boost_1_75_0
```

### 4.2 编译安装

```bash
# 配置 (安装到 /opt/thirdparty)
./bootstrap.sh --prefix=/opt/thirdparty

# 编译并安装 (使用所有 CPU 核心)
./b2 -j$(nproc) \
    --with-system \
    --with-thread \
    --with-filesystem \
    --with-program_options \
    --with-log \
    --with-timer \
    --with-chrono \
    --with-atomic \
    --with-date_time \
    --with-regex \
    link=shared \
    threading=multi \
    variant=release \
    install

# 验证安装
ls -la /opt/thirdparty/lib/libboost*
```

### 4.3 验证

```bash
# 检查版本
cat /opt/thirdparty/include/boost/version.hpp | grep "BOOST_LIB_VERSION"

# 应该显示 1_75
```

---

## 5. OpenSSL 安装 (3.0.12)

### 5.1 下载源码

```bash
cd ~/thirdparty_src

# 下载 OpenSSL 3.0.12
wget https://www.openssl.org/source/openssl-3.0.12.tar.gz

# 解压
tar -xzf openssl-3.0.12.tar.gz
cd openssl-3.0.12
```

### 5.2 编译安装

```bash
# 配置
./config \
    --prefix=/opt/thirdparty \
    --openssldir=/opt/thirdparty/ssl \
    --libdir=lib \
    shared \
    zlib-dynamic

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

### 6.1 下载源码

```bash
cd ~/thirdparty_src

# 下载 libevent 2.1.12
wget https://github.com/libevent/libevent/releases/download/release-2.1.12-stable/libevent-2.1.12-stable.tar.gz

# 解压
tar -xzf libevent-2.1.12-stable.tar.gz
cd libevent-2.1.12-stable
```

### 6.2 编译安装

```bash
# 配置
./configure --prefix=/opt/thirdparty

# 编译
make -j$(nproc)

# 安装
sudo make install

# 验证
pkg-config --modversion libevent
# 应该显示 2.1.12
```

---

## 7. spdlog 安装 (1.11.0)

### 7.1 下载源码

```bash
cd ~/thirdparty_src

# 下载 spdlog 1.11.0
wget https://github.com/gabime/spdlog/archive/refs/tags/v1.11.0.tar.gz -O spdlog-1.11.0.tar.gz

# 解压
tar -xzf spdlog-1.11.0.tar.gz
cd spdlog-1.11.0
```

### 7.2 编译安装

```bash
# 创建构建目录
mkdir build
cd build

# 配置 CMake
cmake .. \
    -DCMAKE_INSTALL_PREFIX=/opt/thirdparty \
    -DSPDLOG_BUILD_SHARED=ON \
    -DSPDLOG_BUILD_EXAMPLE=OFF

# 编译
make -j$(nproc)

# 安装
sudo make install

# 验证
ls /opt/thirdparty/include/spdlog/spdlog.h
```

---

## 8. Protobuf 安装 (3.21.12)

### 8.1 下载源码

```bash
cd ~/thirdparty_src

# 下载 Protobuf 3.21.12
wget https://github.com/protocolbuffers/protobuf/releases/download/v3.21.12/protobuf-cpp-3.21.12.tar.gz

# 解压
tar -xzf protobuf-cpp-3.21.12.tar.gz
cd protobuf-3.21.12
```

### 8.2 编译安装

```bash
# 配置
./configure --prefix=/opt/thirdparty

# 编译
make -j$(nproc)

# 安装
sudo make install

# 更新动态链接库缓存
sudo ldconfig

# 验证
/opt/thirdparty/bin/protoc --version
# 应该显示 libprotoc 3.21.12
```

---

## 9. RapidJSON 安装 (1.1.0)

### 9.1 下载源码

```bash
cd ~/thirdparty_src

# 下载 RapidJSON 1.1.0
wget https://github.com/Tencent/rapidjson/archive/refs/tags/v1.1.0.tar.gz -O rapidjson-1.1.0.tar.gz

# 解压
tar -xzf rapidjson-1.1.0.tar.gz
cd rapidjson-1.1.0
```

### 9.2 编译安装

```bash
# 创建构建目录
mkdir build
cd build

# 配置 CMake (RapidJSON 是纯头文件库)
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

## 10. Google Test 安装 (1.13.0)

### 10.1 下载源码

```bash
cd ~/thirdparty_src

# 下载 Google Test 1.13.0
wget https://github.com/google/googletest/archive/refs/tags/v1.13.0.tar.gz -O googletest-1.13.0.tar.gz

# 解压
tar -xzf googletest-1.13.0.tar.gz
cd googletest-1.13.0
```

### 10.2 编译安装

```bash
# 创建构建目录
mkdir build
cd build

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

## 11. etcd-cpp-api 安装 (0.1.5)

### 11.1 下载源码

```bash
cd ~/thirdparty_src

# 下载 etcd-cpp-api
git clone https://github.com/edwardcapriolo/etcd-cpp-api.git
cd etcd-cpp-api

# 切换到稳定版本
git checkout v0.1.5
```

### 11.2 编译安装

```bash
# 创建构建目录
mkdir build
cd build

# 配置 CMake
cmake .. \
    -DCMAKE_INSTALL_PREFIX=/opt/thirdparty \
    -DBUILD_SHARED_LIBS=ON

# 编译
make -j$(nproc)

# 安装
sudo make install

# 验证
ls /opt/thirdparty/lib/libetcd-cpp-api*
```

---

## 12. zlib 安装 (1.3)

### 12.1 下载源码

```bash
cd ~/thirdparty_src

# 下载 zlib 1.3
wget https://zlib.net/zlib-1.3.tar.gz

# 解压
tar -xzf zlib-1.3.tar.gz
cd zlib-1.3
```

### 12.2 编译安装

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

## 13. lz4 安装 (1.9.4)

### 13.1 下载源码

```bash
cd ~/thirdparty_src

# 下载 lz4 1.9.4
wget https://github.com/lz4/lz4/archive/refs/tags/v1.9.4.tar.gz -O lz4-1.9.4.tar.gz

# 解压
tar -xzf lz4-1.9.4.tar.gz
cd lz4-1.9.4
```

### 13.2 编译安装

```bash
# 编译
make -j$(nproc)

# 安装到指定目录
sudo make install PREFIX=/opt/thirdparty

# 验证
/opt/thirdparty/bin/lz4 --version
```

---

## 14. 一键安装脚本

创建 `install_all_deps.sh`:

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

info() { echo -e "${GREEN}[INFO]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; }
step() { echo -e "${BLUE}[STEP]${NC} $1"; }

# 创建目录
create_directories() {
    step "Creating installation directories..."
    sudo mkdir -p $PREFIX
    sudo chown $USER:$USER $PREFIX
    mkdir -p ~/thirdparty_src
    info "Directories created"
}

# 安装 Boost
install_boost() {
    step "Installing Boost 1.75.0..."
    cd ~/thirdparty_src
    
    if [ ! -f "boost_1_75_0.tar.gz" ]; then
        wget https://boostorg.jfrog.io/artifactory/main/release/1.75.0/source/boost_1_75_0.tar.gz
    fi
    
    tar -xzf boost_1_75_0.tar.gz
    cd boost_1_75_0
    
    ./bootstrap.sh --prefix=$PREFIX
    ./b2 -j$(nproc) \
        --with-system --with-thread --with-filesystem \
        --with-program_options --with-log --with-timer \
        --with-chrono --with-atomic --with-date_time --with-regex \
        link=shared threading=multi variant=release install
    
    info "Boost installed successfully"
}

# 安装 OpenSSL
install_openssl() {
    step "Installing OpenSSL 3.0.12..."
    cd ~/thirdparty_src
    
    if [ ! -f "openssl-3.0.12.tar.gz" ]; then
        wget https://www.openssl.org/source/openssl-3.0.12.tar.gz
    fi
    
    tar -xzf openssl-3.0.12.tar.gz
    cd openssl-3.0.12
    
    ./config --prefix=$PREFIX --openssldir=$PREFIX/ssl --libdir=lib shared zlib-dynamic
    make -j$(nproc)
    sudo make install
    
    info "OpenSSL installed successfully"
}

# 安装 libevent
install_libevent() {
    step "Installing libevent 2.1.12..."
    cd ~/thirdparty_src
    
    if [ ! -f "libevent-2.1.12-stable.tar.gz" ]; then
        wget https://github.com/libevent/libevent/releases/download/release-2.1.12-stable/libevent-2.1.12-stable.tar.gz
    fi
    
    tar -xzf libevent-2.1.12-stable.tar.gz
    cd libevent-2.1.12-stable
    
    ./configure --prefix=$PREFIX
    make -j$(nproc)
    sudo make install
    
    info "libevent installed successfully"
}

# 安装 spdlog
install_spdlog() {
    step "Installing spdlog 1.11.0..."
    cd ~/thirdparty_src
    
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
    info "AAF Third-party Dependencies Installer"
    info "========================================="
    
    create_directories
    install_boost
    install_openssl
    install_libevent
    install_spdlog
    
    info "========================================="
    info "All dependencies installed successfully!"
    info "========================================="
    info ""
    info "Installation prefix: $PREFIX"
    info ""
    info "Environment setup:"
    info "  export PATH=$PREFIX/bin:\$PATH"
    info "  export LD_LIBRARY_PATH=$PREFIX/lib:$PREFIX/lib64:\$LD_LIBRARY_PATH"
    info "  export PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig:\$PKG_CONFIG_PATH"
}

main "$@"
```

使用方法:

```bash
chmod +x install_all_deps.sh
./install_all_deps.sh
```

---

## 15. 验证安装

### 15.1 检查所有库

```bash
# 检查 Boost
ls /opt/thirdparty/lib/libboost* | wc -l
# 应该显示多个文件

# 检查 OpenSSL
/opt/thirdparty/bin/openssl version
# OpenSSL 3.0.12

# 检查 libevent
pkg-config --modversion libevent
# 2.1.12

# 检查 spdlog
ls /opt/thirdparty/include/spdlog/spdlog.h
# 文件存在

# 检查 Protobuf
/opt/thirdparty/bin/protoc --version
# libprotoc 3.21.12
```

### 15.2 编译测试程序

创建 `test_deps.cpp`:

```cpp
#include <boost/system/error_code.hpp>
#include <spdlog/spdlog.h>
#include <iostream>

int main() {
    std::cout << "Boost version: " << BOOST_VERSION / 100000 << "." 
              << BOOST_VERSION / 100 % 1000 << "." 
              << BOOST_VERSION % 100 << std::endl;
    
    spdlog::info("Testing spdlog");
    
    return 0;
}
```

编译:

```bash
g++ -o test_deps test_deps.cpp \
    -I/opt/thirdparty/include \
    -L/opt/thirdparty/lib \
    -lboost_system -lspdlog -pthread

./test_deps
```

---

## 16. 常见问题

### Q1: Boost 编译失败

**解决**:
```bash
# 清理旧的构建
rm -rf boost_1_75_0
tar -xzf boost_1_75_0.tar.gz

# 重新编译，减少并行度
./b2 -j2 install
```

### Q2: 找不到动态库

**解决**:
```bash
# 更新 ldconfig
sudo ldconfig

# 或者添加到配置文件
echo "/opt/thirdparty/lib" | sudo tee /etc/ld.so.conf.d/thirdparty.conf
sudo ldconfig
```

### Q3: pkg-config 找不到

**解决**:
```bash
# 设置环境变量
export PKG_CONFIG_PATH=/opt/thirdparty/lib/pkgconfig:$PKG_CONFIG_PATH
```

---

## 17. 总结

完成以上步骤后，您应该成功通过源码安装了 AAF 所需的所有第三方库。

**检查清单**:
- ✅ Boost 1.75.0 已安装
- ✅ OpenSSL 3.0.12 已安装
- ✅ libevent 2.1.12 已安装
- ✅ spdlog 1.11.0 已安装
- ✅ Protobuf 3.21.12 已安装
- ✅ 其他依赖库已安装

**下一步**:
- 参考 `AAF_编译配置.md` 编译 AAF 工程
- 确保编译时指定正确的库路径

祝您安装顺利！🎉
