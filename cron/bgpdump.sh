#!/bin/bash -e
rm -f /tmp/r
vtysh -c "conf t
dump bgp routes-mrt /tmp/r
exit
exit
" < /dev/null

for i in `seq 1 10`; do
	if [ ! -f /tmp/r ]; then
		sleep 1;
	fi
done
/usr/local/bin/bgpdump /tmp/r |tr -d '{}' > /tmp/rt
psql -q ipr -c " 
BEGIN; 
CREATE TEMPORARY TABLE r1pfx (prefix ip4r, aspath varchar(255)) ON COMMIT DROP; 
COPY r1pfx(prefix,aspath) FROM STDIN;
DELETE FROM pfxtmp WHERE router_id=1;
INSERT INTO pfxtmp (prefix,aspath,router_id) SELECT prefix,aspath,1 FROM r1pfx; 
COMMIT;
" < /tmp/rt
