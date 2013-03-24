<?

require "INSTDIR/asn.php";
$dbconn = pg_connect("host=localhost dbname=ipr user=root")
    or die('Could not connect: ' . pg_last_error());

pg_query("BEGIN");
pg_query("delete from iptraf_dt where date(dt) >= date(now()-interval '1 day') and asn <> 'ALL'");
pg_query("
	insert
		into iptraf_dt
	(dt, asn, pathgb, pathgb_free, pathgb_prem, router_id)
		select
			start,
			(regexp_split_to_table(aspath,' ')) as asn,
			(sum(n_bytes+p_bytes)/(1000*1000*1000))::int as pathgb,
			(sum(n_bytes)/(1000*1000*1000))::int as pathgb_free,
			(sum(p_bytes)/(1000*1000*1000))::int as pathgb_prem,
			router_id
		from
			iptraf_arch
		where
			date(start) >= date(now()-interval '1 day')
		group by
			asn, start, router_id
");
//

$sql = pg_query("
	select
		start,
		regexp_replace(aspath,'^([0123456789 ]* )*([0123456789]+)$',E'\\\\2') as asn,
		(sum(n_bytes+p_bytes)/(1000*1000*1000))::int as endgb,
		(sum(n_bytes)/(1000*1000*1000))::int as endgb_free,
		(sum(p_bytes)/(1000*1000*1000))::int as endgb_prem,
		router_id
	from
		iptraf_arch
	where
		date(start) >= date(now()-interval '1 day')
	group by
		asn, start, router_id
");

while ($r = pg_fetch_assoc($sql)) {
	pg_query("
		update
			iptraf_dt
		set
			endgb=endgb+{$r['endgb']},
			endgb_free=endgb_free+{$r['endgb_free']},
			endgb_prem=endgb_prem+{$r['endgb_prem']}
		where
			asn 		= '{$r['asn']}'
		and dt  		= '{$r['start']}'
		and router_id 	= '{$r['router_id']}'
	");
}

pg_query("COMMIT");
