Storage Performance Development Kit
===================================

[![Build Status](https://travis-ci.org/spdk/spdk.svg?branch=master)](https://travis-ci.org/spdk/spdk)

[SPDK Mailing List](https://lists.01.org/mailman/listinfo/spdk)

[SPDK on 01.org](https://01.org/spdk)

The Storage Performance Development Kit (SPDK) provides a set of tools
and libraries for writing high performance, scalable, user-mode storage
applications. It achieves high performance by moving all of the necessary
drivers into userspace and operating in a polled mode instead of relying on
interrupts, which avoids kernel context switches and eliminates interrupt
handling overhead.

The development kit currently includes:
* [NVMe driver](http://www.spdk.io/doc/nvme.html)
* [I/OAT (DMA engine) driver](http://www.spdk.io/doc/ioat.html)
* [NVMe over Fabrics target](http://www.spdk.io/doc/nvmf.html)
* [iSCSI target](http://www.spdk.io/doc/iscsi.html)

Documentation
=============

[Doxygen API documentation](http://www.spdk.io/doc/) is available, as
well as a [Porting Guide](http://www.spdk.io/doc/porting.html) for porting SPDK to different frameworks
and operating systems.

Many examples are available in the `examples` directory.

[Changelog](CHANGELOG.md)

Prerequisites
=============

To build SPDK, some dependencies must be installed.

Update kernel and header file (Linux kernel headers or sources required to build kernel modules)
   
    sudo yum install -y kernel kernel-devel git
    sudo reboot

Fedora/CentOS:

    sudo yum install -y gcc gcc-c++ CUnit-devel libaio-devel openssl-devel
    # Additional dependencies for NVMe over Fabrics:
    sudo yum install -y libibverbs-devel librdmacm-devel

Ubuntu/Debian:

    sudo apt-get install -y gcc g++ make libcunit1-dev libaio-dev libssl-dev
    # Additional dependencies for NVMe over Fabrics:
    sudo apt-get install -y libibverbs-dev librdmacm-dev

FreeBSD:

    sudo pkg install gmake cunit openssl

Additionally, [DPDK](http://dpdk.org/doc/quick-start) is required.

    0) git clone https://github.com/spdk/spdk
    1) cd /path/to/spdk
    2) wget http://fast.dpdk.org/rel/dpdk-17.02.tar.xz
    3) tar xf dpdk-17.02.tar.xz

Install SPDK fio PLUGIN (https://github.com/spdk/spdk/tree/master/examples/nvme/fio_plugin)

    1）git clone http://github.com/axboe/fio
    2）cd fio && git checkout fio-2.18
    3）./configure && make
       编译安装对应版本的FIO, sudo make install
    4) 编辑SPDK目录下的CONFIG 文件，设置 CONFIG_FIO_PLUGIN 为y，FIO_SOURCE_DIR 为下载的fio的源码路径
       CONFIG_FIO_PLUGIN=y     FIO_SOURCE_DIR=/home/ec2-user/spdk/fio
    5) 修改DPDK config file (如：config/defconfig_x86_64-native-linuxapp-gcc) ，在其中增加 EXTRA_CFLAGS=-fPIC
    
Linux:

    4) (cd dpdk-17.02 && make install T=x86_64-native-linuxapp-gcc DESTDIR=.)

FreeBSD:

    4) (cd dpdk-17.02 && gmake install T=x86_64-native-bsdapp-clang DESTDIR=.)

Build Configuration
===================

Optional components and other build-time configuration are controlled by the `CONFIG` file
in the root SPDK directory.  `CONFIG` is a Makefile fragment that may be edited before building to
control which options are enabled.

Boolean (on/off) options are configured with a 'y' (yes) or 'n' (no).  For example, this line of
`CONFIG` controls whether the optional RDMA (libibverbs) support is enabled:

    CONFIG_RDMA?=n

To enable RDMA, this line of CONFIG may be modified to contain 'y' instead of 'n'.

Alternatively, `CONFIG` options may also be overrriden on the `make` command line:

    make CONFIG_RDMA=y

The options specified on the `make` command line take precedence over the default values in
`CONFIG`.

Building
========

Once the prerequisites are installed, run 'make' within the SPDK directory
to build the SPDK libraries and examples.
If you followed the instructions above for building DPDK:

Linux:

    make DPDK_DIR=./dpdk-17.02/x86_64-native-linuxapp-gcc

FreeBSD:

    gmake DPDK_DIR=./dpdk-17.02/x86_64-native-bsdapp-clang

Hugepages and Device Binding
============================

http://dpdk.org/doc/guides/linux_gsg/sys_reqs.html

Before running an SPDK application, some hugepages must be allocated and
any NVMe and I/OAT devices must be unbound from the native kernel drivers.
SPDK includes a script to automate this process on both Linux and FreeBSD.
This script should be run as root.

    sudo scripts/setup.sh

Examples
========

Example code is located in the examples directory. The examples are compiled
automatically as part of the build process. Simply call any of the examples
with no arguments to see the help output. You'll likely need to run the examples
as a privileged user (root) unless you've done additional configuration
to grant your user permission to allocate huge pages and map devices through
vfio.

Test
========
sudo ./identify
sudo /usr/local/bin/fio example.fio


