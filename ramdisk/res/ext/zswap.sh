#!/sbin/busybox sh
#
# zswap
#

zswap_size=512

	if [ $zswap_size -gt 0 ]; then
		echo `expr $zswap_size \* 1024 \* 1024` > /sys/devices/virtual/block/vnswap0/disksize
		/sbin/busybox mkswap /dev/block/vnswap0 > /dev/null 2>&1
		/sbin/busybox swapon /dev/block/vnswap0 > /dev/null 2>&1
	fi
