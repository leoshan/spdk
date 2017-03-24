#!/usr/bin/env bash

##Update kernel , after update kernel ,should reboot VM
sudo yum install -y kernel kernel-devel
sleep 10
sudo reboot

##Install spdk need package and upgrade OS
sudo yum install -y gcc gcc-c++ CUnit-devel libaio-devel openssl-devel git nvme-cli
sleep 20
sudo yum upgrade -y

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
lsblk
cat /proc/meminfo | grep -i huge
mount | grep -i huge
sudo scripts/setup.sh status
sudo dpdk-17.02/usertools/dpdk-devbind.py --status

sudo /home/ec2-user/spdk/examples/nvme/identify/identify > a.txt
sudo /usr/local/bin/fio latency-4k-rr-spdk.fio >> a.txt
sudo /usr/local/bin/fio latency-4k-rw-spdk.fio >> a.txt
sudo /usr/local/bin/fio bw-1m-rr-spdk.fio >> a.txt
sudo /usr/local/bin/fio bw-1m-rw-spdk.fio >> a.txt
sudo /usr/local/bin/fio iops-4k-rr-spdk.fio >> a.txt
sudo /usr/local/bin/fio iops-4k-rw-spdk.fio >> a.txts

sudo scripts/setup.sh reset
lsblk
cat /proc/meminfo | grep -i huge
mount | grep -i huge

sudo /usr/local/bin/fio latency-4k-rr-nvme.fio >> a.txt
sudo /usr/local/bin/fio latency-4k-rw-nvme.fio >> a.txt
sudo /usr/local/bin/fio bw-1m-rr-nvme.fio >> a.txt
sudo /usr/local/bin/fio bw-1m-rw-nvme.fio >> a.txt
sudo /usr/local/bin/fio iops-4k-rr-nvme.fio >> a.txt
sudo /usr/local/bin/fio iops-4k-rw-nvme.fio >> a.txt



