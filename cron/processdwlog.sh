#!/bin/sh
cd LOGDIR || exit 2

# some locking needed

if [ -f /var/run/pdwl.pid ]; then
	for i in `seq 1 30`; do
		if ! [ -d /proc/`cat /var/run/pdwl.pid` ] ; then
			echo $$ > /var/run/pdwl.pid
			break
		fi
		sleep 2
	done
	if [ -d /proc/`cat /var/run/pdwl.pid` ] ; then
		# another instance is still running after 1 min
		exit
	fi

fi

echo $$ > /var/run/pdwl.pid
trap "rm -f /var/run/pdwl.pid" exit SIGHUP SIGINT SIGTERM 

mv dwllog dwllog.w
killall -HUP ipdwl
# process log BEGIN
php INSTDIR/readlog.php | psql -q ipr
# process log END
mv  dwllog.w LOGDIR/archive/log.`date +%Y%m%d-%H%M`
rm -f /var/run/pdwl.pid
