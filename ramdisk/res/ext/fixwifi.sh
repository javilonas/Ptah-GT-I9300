#!/sbin/busybox sh
#
#

/sbin/busybox rm /data/.cid.info
/sbin/busybox sleep 2
/sbin/busybox printf "Samsung | murata | semco | semcosh | semcove" > /data/.cid.info
/sbin/busybox chown system /data/.cid.info
/sbin/busybox chown -R 1000 /data/.cid.info
/sbin/busybox chgrp wifi /data/.cid.info
/sbin/busybox chgrp -R 1010 /data/.cid.info
/sbin/busybox chmod 0660 /data/.cid.info
/sbin/busybox chmod -R 0660 /data/.cid.info
/sbin/busybox sync

