if [ -z "$PGHOST" ]; then
  echo "PGHOST and other PGx variables need to be set"
  exit 1
fi

if [ -z "$1" ]; then
  MOS_VERSION=MOS_ECMWF_040422
else
  MOS_VERSION=$1
fi

for i in 00 12; do
  sql="select w.mos_period_id||','||w.analysis_hour||','||w.station_id||','||st_x(s.position)||','||st_y(s.position)||','||extract(hour from w.forecast_period)||','||p.name||','||array_to_string(hstore_to_array(w.weights), ',') from mos_weight w, param p, station s, mos_version v where w.target_param_id = p.id and w.station_id = s.id and w.mos_version_id = v.id and v.label = '$MOS_VERSION' and w.analysis_hour = $i"

  echo $sql | psql -Aqt | gzip > weights-$MOS_VERSION-$i.csv.gz
done
