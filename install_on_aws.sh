#!/usr/bin/env bash

##Update kernel and spdk need package, after update kernel ,should reboot VM
sudo yum install -y gcc gcc-c++ CUnit-devel libaio-devel openssl-devel git nvme-cli
sudo yum install -y kernel kernel-devel
sudo yum upgrade -y
sudo reboot

##Download SPDK and DPDK
git clone https://github.com/spdk/spdk
cd spdk && wget http://fast.dpdk.org/rel/dpdk-17.02.tar.xz
tar xf dpdk-17.02.tar.xz

##Set FIO flag in DPDK and make DPDK
echo EXTRA_CFLAGS=-fPIC >> ./dpdk-17.02/config/defconfig_x86_64-native-linuxapp-gcc
cd dpdk-17.02 && make install T=x86_64-native-linuxapp-gcc DESTDIR=.

##Download fio source and make install
cd ..
git clone http://github.com/axboe/fio
cd fio && git checkout fio-2.18
./configure && make
sudo make install

##Compile SPDK and setup SPDK
cd ..
make DPDK_DIR=./dpdk-17.02/x86_64-native-linuxapp-gcc CONFIG_FIO_PLUGIN=y FIO_SOURCE_DIR=/home/ec2-user/spdk/fio
sudo scripts/setup.sh

##Test -- check and FIO test
./example/nvme/identity/identity
sudo /usr/local/bin/fio latency-4k-rr-nvme.fio
