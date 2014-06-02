#!/bin/bash

TOOLCHAIN="/home/lonas/Kernel_Lonas/toolchains/arm-cortex_a9-linux-gnueabihf-linaro_4.9.1-2014.05/bin/arm-cortex_a9-linux-gnueabihf-"
DIR="/home/lonas/Kernel_Lonas/Ptah-GT-I9300"

echo "#################### Eliminando Restos ####################"
if [ -e boot.img ]; then
	rm boot.img
fi

if [ -e compile.log ]; then
	rm compile.log
fi

if [ -e ramdisk.cpio ]; then
	rm ramdisk.cpio
fi

if [ -e ramdisk.cpio.gz ]; then
        rm ramdisk.cpio.gz
fi

# make distclean
make clean

# clean ccache
read -t 5 -p "clean ccache, 5sec timeout (y/n)?"
if [ "$REPLY" == "y" ]; then
	ccache -C
fi

echo "ramfs_tmp = $RAMFS_TMP"

echo "#################### Eliminando build anterior ####################"

echo "Cleaning latest build"
make ARCH=arm CROSS_COMPILE=$TOOLCHAIN -j`grep 'processor' /proc/cpuinfo | wc -l` mrproper

find -name '*.ko' -exec rm -rf {} \;
rm -f $DIR/releasetools/tar/*.tar
rm -f $DIR/releasetools/zip/*.zip
rm -rf $DIR/arch/arm/boot/zImage
