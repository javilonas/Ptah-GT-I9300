#!/sbin/busybox sh
#
#

/sbin/busybox rm /data/.cid.info
/sbin/busybox sleep 2
/sbin/busybox printf "samsung | murata | semco | semcosh | semcove | semco3rd | muratafem1 | muratafem2 | muratafem3 | wisol | wisolfem1" > /data/.cid.info
/sbin/busybox chown system /data/.cid.info
/sbin/busybox chown -R 1000 /data/.cid.info
/sbin/busybox chgrp wifi /data/.cid.info
/sbin/busybox chgrp -R 1010 /data/.cid.info
/sbin/busybox chmod 0666 /data/.cid.info
/sbin/busybox chmod -R 0666 /data/.cid.info
/sbin/busybox sync

