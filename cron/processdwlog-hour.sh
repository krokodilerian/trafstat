#!/bin/sh
INSTDIR/processdwlog.sh
(cat <<EOF 
BEGIN; 
INSERT INTO iptraf_arch (prefix, aspath, req, bytes, pkt, lost, start, traffic_id, router_id) 
SELECT prefix, aspath, req, bytes, pkt, lost, (date_trunc('hour',now() + interval '10 minutes') - interval '1 hours'), traffic_id, router_id 
FROM iptraf WHERE bytes>0 ;

INSERT INTO iptraf_cn (dt,country,mb, traffic_id) 
SELECT  (date_trunc('hour',now() + interval '10 minutes') - interval '1 hours') as dt, c.country as country,
 (sum(i.bytes)/(1000*1000))::int8 as mb, i.traffic_id FROM iptraf i, cntry c WHERE i.prefix <<=c.prefix 
GROUP BY c.country, i.traffic_id
HAVING (sum(i.bytes)/(1000*1000))::int8 >0;

INSERT INTO iptraf_dt (dt,asn,     
pathmb,endmb,
traffic_id, router_id)
SELECT
(date_trunc('hour',now() + interval '10 minutes') - interval '1 hours') as dt,
'ALL',
(sum(bytes)/(1000*1000))::int8,
(sum(bytes)/(1000*1000))::int8,
traffic_id, router_id
FROM iptraf GROUP BY router_id, traffic_id;

INSERT INTO iptraf_cn (dt, country, mb, traffic_id) 
SELECT  (date_trunc('hour',now() + interval '10 minutes') - interval '1 hours') as dt, 'e2' as country,
(sum(i.n_bytes+i.p_bytes)/(1000*1000))::int8 as mb,
i.traffic_id
FROM iptraf i, cntry c 
WHERE i.prefix <<=c.prefix AND c.country in ('al','ad','at','be','ba','bg','hr','cy','cz','dk','ee','fi','fr','de','gr','hu','is','ie','it','lv','li','lt','lu','mk','mt','md','mc','me','nl','no','pl','pt','ro','sm','rs','sk','si','es','se','ch','gb') 
GROUP BY i.traffic_id;

INSERT INTO iptraf_cn (dt,country,mb,traffic_id) 
SELECT  (date_trunc('hour',now() + interval '10 minutes') - interval '1 hours') as dt, 'a2' as country,
(sum(bytes)/(1000*1000))::int8 as gb,
i.traffic_id
FROM iptraf i, cntry c WHERE i.prefix <<=c.prefix
GROUP BY i.traffic_id;

TRUNCATE iptraf;
INSERT INTO iptraf (prefix,aspath,router_id, traffic_id) SELECT p.prefix,p.aspath,p.router_id,t.traffic_id FROM pfxtmp p, traffic t;
COMMIT;
EOF
) | psql -e ipr
/usr/bin/php INSTDIR/asnaggr.php 2>/dev/null >/dev/null
