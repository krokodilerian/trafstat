#!/bin/sh
rm -f /tmp/db0.gz /tmp/db0
wget -q -O /tmp/db0.gz http://ip.ludost.net/raw/country.db.gz
zcat /tmp/db0.gz | sed 's/ /-/' |cut -d ' ' -f -2 > /tmp/db0

(cat <<EOF 
BEGIN; 
TRUNCATE cntry;
COPY cntry FROM '/tmp/db0' DELIMITER ' ';
COMMIT;
EOF
) | psql -q ipr
