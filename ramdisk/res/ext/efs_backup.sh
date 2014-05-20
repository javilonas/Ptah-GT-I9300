#!/sbin/busybox sh
#
# EFS backup by Javilonas
# 

if [ ! -f /data/media/0/efs_backup/efs.tar.gz ];
then
  mkdir /data/media/0/efs_backup
  chmod 777 /data/media/0/efs_backup
  /sbin/busybox tar zcvf /data/media/0/efs_backup/efs.tar.gz /efs
  /sbin/busybox cat /dev/block/mmcblk0p3 > /data/media/0/efs_backup/efs.img
  chmod 777 /data/media/0/efs_backup/efs.img
  chmod 777 /data/media/0/efs_backup/efs.tar.gz
fi
