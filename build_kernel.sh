#!/bin/sh
# Build Script: Javilonas, 14-12-2013
# Javilonas <admin@lonasdigital.com>

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
rm Module.symvers

cp arch/arm/configs/lonas_defconfig .config;

echo "#################### Preparando Entorno ####################"
export KERNELDIR=`readlink -f .`
export RAMFS_SOURCE=`readlink -f $KERNELDIR/ramdisk`
export USE_SEC_FIPS_MODE=true

echo "kerneldir = $KERNELDIR"
echo "ramfs_source = $RAMFS_SOURCE"

if [ "${1}" != "" ];then
  export KERNELDIR=`readlink -f ${1}`
fi

TOOLCHAIN="/home/lonas/Kernel_Lonas/toolchains/arm-cortex_a9-linux-gnueabihf-linaro_4.9.1-2014.05/bin/arm-cortex_a9-linux-gnueabihf-"
TOOLCHAIN_PATCH="/home/lonas/Kernel_Lonas/toolchains/arm-cortex_a9-linux-gnueabihf-linaro_4.9.1-2014.05/bin"
ROOTFS_PATH="/home/lonas/Kernel_Lonas/Ptah-GT-I9300/ramdisk"
RAMFS_TMP="/home/lonas/Kernel_Lonas/tmp/ramfs-source-sgs3"
export KERNEL_VERSION="Ptah-2.4"
VERSION_KL="SAMMY"
REVISION="RTM"

export KBUILD_BUILD_VERSION="1"

echo "#################### Aplicando Permisos correctos ####################"
chmod 644 $ROOTFS_PATH/*.rc
chmod 750 $ROOTFS_PATH/init*
chmod 640 $ROOTFS_PATH/fstab*
chmod 644 $ROOTFS_PATH/default.prop
chmod 771 $ROOTFS_PATH/data
chmod 755 $ROOTFS_PATH/dev
chmod 755 $ROOTFS_PATH/lib
chmod 755 $ROOTFS_PATH/lib/modules
chmod 644 $ROOTFS_PATH/lib/modules/*
chmod 755 $ROOTFS_PATH/proc
chmod 750 $ROOTFS_PATH/sbin
chmod 750 $ROOTFS_PATH/sbin/*
chmod 755 $ROOTFS_PATH/res/ext/99SuperSUDaemon
chmod 755 $ROOTFS_PATH/sys
chmod 755 $ROOTFS_PATH/system

find . -type f -name '*.h' -exec chmod 644 {} \;
find . -type f -name '*.c' -exec chmod 644 {} \;
find . -type f -name '*.py' -exec chmod 755 {} \;
find . -type f -name '*.sh' -exec chmod 755 {} \;
find . -type f -name '*.pl' -exec chmod 755 {} \;

echo "ramfs_tmp = $RAMFS_TMP"

echo "#################### Eliminando build anterior ####################"
make ARCH=arm CROSS_COMPILE=$TOOLCHAIN -j`grep 'processor' /proc/cpuinfo | wc -l` mrproper

find -name '*.ko' -exec rm -rf {} \;
rm -rf $KERNELDIR/arch/arm/boot/zImage

echo "#################### Make defconfig ####################"
make ARCH=arm CROSS_COMPILE=$TOOLCHAIN lonas_defconfig

#nice -n 10 make -j7 ARCH=arm CROSS_COMPILE=$TOOLCHAIN || exit -1

nice -n 10 make -j6 ARCH=arm CROSS_COMPILE=$TOOLCHAIN >> compile.log 2>&1 || exit -1

make -j`grep 'processor' /proc/cpuinfo | wc -l` ARCH=arm CROSS_COMPILE=$TOOLCHAIN >> compile.log 2>&1 || exit -1

#make -j`grep 'processor' /proc/cpuinfo | wc -l` ARCH=arm CROSS_COMPILE=$TOOLCHAIN || exit -1

mkdir -p $ROOTFS_PATH/lib/modules
mkdir -p $ROOTFS_PATH/system/lib
ln -s $ROOTFS_PATH/lib/modules/ $ROOTFS_PATH/system/lib
find . -name '*.ko' -exec cp -av {} $ROOTFS_PATH/lib/modules/ \;
$TOOLCHAIN_PATCH/arm-cortex_a9-linux-gnueabihf-strip --strip-unneeded $ROOTFS_PATH/lib/modules/*.ko

echo "#################### Update Ramdisk ####################"
rm -f $KERNELDIR/releasetools/tar/$KERNEL_VERSION-$REVISION$KBUILD_BUILD_VERSION-$VERSION_KL.tar
rm -f $KERNELDIR/releasetools/zip/$KERNEL_VERSION-$REVISION$KBUILD_BUILD_VERSION-$VERSION_KL.zip
cp -f $KERNELDIR/arch/arm/boot/zImage .

rm -rf $RAMFS_TMP
rm -rf $RAMFS_TMP.cpio
rm -rf $RAMFS_TMP.cpio.gz
rm -rf $KERNELDIR/*.cpio
rm -rf $KERNELDIR/*.cpio.gz
cd $ROOTFS_PATH
cp -ax $ROOTFS_PATH $RAMFS_TMP
find $RAMFS_TMP -name .git -exec rm -rf {} \;
find $RAMFS_TMP -name EMPTY_DIRECTORY -exec rm -rf {} \;
find $RAMFS_TMP -name .EMPTY_DIRECTORY -exec rm -rf {} \;
rm -rf $RAMFS_TMP/tmp/*
rm -rf $RAMFS_TMP/.hg

echo "#################### Build Ramdisk ####################"
cd $RAMFS_TMP
find . | fakeroot cpio -o -H newc > $RAMFS_TMP.cpio 2>/dev/null
ls -lh $RAMFS_TMP.cpio
gzip -9 -f $RAMFS_TMP.cpio

echo "#################### Compilar Kernel ####################"
cd $KERNELDIR

nice -n 10 make -j7 ARCH=arm CROSS_COMPILE=$TOOLCHAIN zImage || exit 1

echo "#################### Generar boot.img ####################"
./mkbootimg --kernel $KERNELDIR/arch/arm/boot/zImage --ramdisk $RAMFS_TMP.cpio.gz --board smdk4x12 --base 0x10000000 --pagesize 2048 --ramdiskaddr 0x11000000 -o $KERNELDIR/boot.img

echo "#################### Preparando flasheables ####################"

cp boot.img $KERNELDIR/releasetools/zip
cp boot.img $KERNELDIR/releasetools/tar

cd $KERNELDIR
cd releasetools/zip
zip -0 -r $KERNEL_VERSION-$REVISION$KBUILD_BUILD_VERSION-$VERSION_KL.zip *
cd ..
cd tar
tar cf $KERNEL_VERSION-$REVISION$KBUILD_BUILD_VERSION-$VERSION_KL.tar boot.img && ls -lh $KERNEL_VERSION-$REVISION$KBUILD_BUILD_VERSION-$VERSION_KL.tar

echo "#################### Eliminando restos ####################"

rm $KERNELDIR/releasetools/zip/boot.img
rm $KERNELDIR/releasetools/tar/boot.img
rm $KERNELDIR/boot.img
rm $KERNELDIR/zImage
rm -rf /home/lonas/Kernel_Lonas/tmp/ramfs-source-sgs3
rm /home/lonas/Kernel_Lonas/tmp/ramfs-source-sgs3.cpio.gz
echo "#################### Terminado ####################"
