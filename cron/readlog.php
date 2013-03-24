<?

$trafdat = array();
#error_reporting)E_NONE);

if (empty($argv[1])) $filename="LOGDIR/dwllog.w";
	else $filename=$argv[1];

$dwllog = fopen($filename,"r");
if (!$dwllog) {
	echo "log file ".$filename." couldn't be open!\n";
	exit;
}

$ip = array();
$srvip = array();
$types = array();

$rid=1;

//$dbconn = pg_connect("host=localhost dbname=ipr user=root")
$dbconn = pg_connect("dbname=ipr")
    or die('Could not connect: ' . pg_last_error());

$res = pg_query($dbconn,"SELECT router_id,lower(prefix)::bigint as start,upper(prefix)::bigint as end FROM router;");
if (!$res) {
	echo "no router data/db conn!\n";
	exit;
}
$numr = pg_num_rows($res);

while ($r = pg_fetch_assoc($res)) {
	$routerpref[$r['router_id']]['start'] = $r['start'];
	$routerpref[$r['router_id']]['end'] = $r['end'];
}

/* 
  had to do these as << wasn't working properly for some reason
*/
$o24=16777216;
$o16=65536;
$o8=256;

while (!feof($dwllog)){
	fscanf($dwllog,"%d %d.%d.%d.%d %ld %d %d %ld %ld %d.%d.%d.%d %ld %ld\n", $crap,$ip[0],$ip[1],$ip[2],$ip[3],$bytes,$crap,$ttype,$retransmits, $mss,$srvip[0],$srvip[1],$srvip[2],$srvip[3],$crap,$crap);

	$packets = (int) ($bytes/$mss);
	$srvip_i= $srvip[0]*$o24 + $srvip[1]*$o16 + $srvip[2]*$o8 + $srvip[3];

	for ($i=1;$i<$numr+1;$i++) {
		if ($srvip_i>=$routerpref[$i]['start'] && $srvip_i<=$routerpref[$i]['end']) {
			$rid=$i;
			break;
		}
	}
	$ipaddr = $ip[0].'.'.$ip[1].'.'.$ip[2].'.'.$ip[3];

	$trafdat[$rid][$ttype][$ipaddr]['ip']=$ip[0].'.'.$ip[1].'.'.$ip[2].'.'.$ip[3];
	if (!isset($trafdat[$rid][$ttype][$ipaddr]['num'])) $trafdat[$rid][$ttype][$ipaddr]['num']=0;
	if (!isset($trafdat[$rid][$ttype][$ipaddr]['bytes'])) $trafdat[$rid][$ttype][$ipaddr]['bytes']=0;
	if (!isset($trafdat[$rid][$ttype][$ipaddr]['packets'])) $trafdat[$rid][$ttype][$ipaddr]['packets']=0;
	if (!isset($trafdat[$rid][$ttype][$ipaddr]['retransmits'])) $trafdat[$rid][$ttype][$ipaddr]['retransmits']=0;

	$trafdat[$rid][$ttype][$ipaddr]['num']++;
	$trafdat[$rid][$ttype][$ipaddr]['bytes']+=$bytes;
	$trafdat[$rid][$ttype][$ipaddr]['packets']+=$packets;
	$trafdat[$rid][$ttype][$ipaddr]['retransmits']+=$retransmits;

	$types[$rid][$ttype]=1;


};



fclose($dwllog);

$qnum=0;
echo "BEGIN;\n";
for ($rid=1;$rid<$numr+1;$rid++) {
	if(!isset($trafdat[$rid])) continue;
	foreach ($types[$rid] as $ttypeid => $crap ) {
		foreach ($trafdat[$rid][$ttypeid] as $r) {
			$qnum++;
			if ($qnum==5000) {
				$qnum=0;
				echo "COMMIT;BEGIN;\n";
			}
			$sql="UPDATE iptraf SET req=req+".$r['num'].", bytes=bytes+".$r['bytes'].", pkt=pkt+".$r['packets'].", lost=lost+".$r['retransmits']." WHERE iptraf_id= (SELECT iptraf_id FROM iptraf WHERE prefix >>= '".$r['ip']."' AND router_id = ".$rid." AND traffic_id = ".$ttypeid." ORDER by ip4r_size(prefix) DESC LIMIT 1);";
			echo $sql."\n";
		}
	}
}
echo "COMMIT;\n"

//print_r($free);
//print_r($premium);

?>
