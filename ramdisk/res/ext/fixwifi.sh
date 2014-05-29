#!/sbin/busybox sh
#
#

/sbin/busybox rm /data/.cid.info
/sbin/busybox sleep 1
/sbin/busybox sync
/sbin/busybox echo Samsung | murata | semco | semcosh | semcove > /data/.cid.info
/sbin/busybox chown system /data/.cid.info
/sbin/busybox chgrp wifi /data/.cid.info
/sbin/busybox chmod 0666 /data/.cid.info
/sbin/busybox sync

