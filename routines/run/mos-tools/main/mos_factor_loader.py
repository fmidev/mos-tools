#!/usr/bin/env python

import sys
import os
import psycopg2
import psycopg2.extras
import argparse

WMO_ID = 0
TARGET_PARAM_NAME = ""
AHOUR = -1


parameters = {
	#"CSV_PARAMETER_NAME : DATABASE_PARAMETER_NAME/DATABASE_LEVEL_NAME/LEVEL_VALUE/TIME_STEP_ADJUSTMENT"
	"DECLINATION" : "DECLINATION-N/NOLEVEL/0",
	"Intercept" : "INTERCEPT-N/NOLEVEL/0",
	"SD" : "SD-M/GROUND/0",
	"MSL" : "P-PA/MEANSEA/0",
	"T2" : "T-K/GROUND/0",
	"T2_M1" : "T-K/GROUND/0/-1",
	"D2" : "TD-K/GROUND/0",
	"TP" : "RR-KGM2/GROUND/0",
	"STR" : "RNETLW-WM2/GROUND/0",
	"LCC" : "NL-PRCNT/GROUND/0",
	"MCC" : "NM-PRCNT/GROUND/0",
	"HCC" : "NH-PRCNT/GROUND/0",
	"CIN" : "CIN-JKG/GROUND/0",
	"CAPE" : "CAPE-JKG/GROUND/0",
	"U10" : "U-MS/GROUND/0",
	"V10" : "V-MS/GROUND/0",
	"CP" : "RRC-KGM2/GROUND/0",
	"LSP" : "RRL-KGM2/GROUND/0",
	"SSHF" : "FLSEN-JM2/GROUND/0",
	"STRD" : "RADLW-WM2/GROUND/0",
	"FG10_3" : "FFG3H-MS/GROUND/0",
	"SKT" : "SKT-K/GROUND/0",
	"MX2T3" : "MAXT2M-K/GROUND/0",
	"MN2T3" : "MINT2M-K/GROUND/0",
	"TCW" : "TOTCW-KGM2/GROUND/0",
	"SLHF" : "FLLAT-JM2/GROUND/0",
	"CBH" : "CLDBASE-M/GROUND/0",
	"SSR" : "RNETSW-WM2/GROUND/0",
	"BLH" : "MIXHGT-M/GROUND/0",
	"DEG0" : "H0C-M/GROUND/0",
	"GH_950" : "Z-M2S2/PRESSURE/950",
	"GH_925" : "Z-M2S2/PRESSURE/925",
	"GH_850" : "Z-M2S2/PRESSURE/850",
	"GH_700" : "Z-M2S2/PRESSURE/700",
	"GH_500" : "Z-M2S2/PRESSURE/500",
	"RH_950" : "RH-PRCNT/PRESSURE/950",
	"RH_925" : "RH-PRCNT/PRESSURE/925",
	"RH_850" : "RH-PRCNT/PRESSURE/850",
	"RH_700" : "RH-PRCNT/PRESSURE/700",
	"RH_500" : "RH-PRCNT/PRESSURE/500",
	"T_950_M1" : "T-K/PRESSURE/950/-1",
	"T_950" : "T-K/PRESSURE/950",
	"T_925_M1" : "T-K/PRESSURE/925/-1",
	"T_925" : "T-K/PRESSURE/925",
	"T_850" : "T-K/PRESSURE/850",
	"T_700" : "T-K/PRESSURE/700",
	"T_500" : "T-K/PRESSURE/500",
	"W_950" : "VV-PAS/PRESSURE/950",
	"W_925" : "VV-PAS/PRESSURE/925",
	"W_850" : "VV-PAS/PRESSURE/850",
	"W_700" : "VV-PAS/PRESSURE/700",
	"W_500" : "VV-PAS/PRESSURE/500",
	"Z_925" : "Z-M2S2/PRESSURE/925",
	"Z_850" : "Z-M2S2/PRESSURE/850",
	"Z_700" : "Z-M2S2/PRESSURE/700",
	"Z_500" : "Z-M2S2/PRESSURE/500"	
}

def ParseCommandLine(argv):
	parser = argparse.ArgumentParser()

	parser.add_argument("-m", "--mos-label", required=True, help="mos label (version) name")
	parser.add_argument("-a", "--analysis-hour", required=False, help="analysis hour")
	parser.add_argument("-w", "--wmo", required=False, help="wmo id of station")
	parser.add_argument("-p", "--param", required=False, help="neons parameter name of target param")
	parser.add_argument("-s", "--season", required=False, help="season id, 1=winter, 3=summer")
	parser.add_argument('file', nargs='+', help='Input file (csv)')

 	args = parser.parse_args()

	return args

def Read(infile):
	ret = {}

	f = open(infile)

	i = 0
	periods = None

	for line in f:
		i = i+1

		line = line.strip()

		if i == 1:
			periods = line.split(',')
			periods = [int(x.strip().replace('"','')) for x in periods[1:]]
			assert(len(periods) == 65)

			continue

		factors = line.split(",")

		factors = [x.strip() for x in factors]

		param = factors[0].replace('"','')

		# remove first element from list which is the param name
		factors.pop(0)

		assert(len(factors) == 65)

		elem = None

		try:
			elem = parameters[param]
		except KeyError:
			print "Unknown parameter %s" % (param)
			sys.exit(1)

		for j, factor in enumerate(factors):

			try:
				ret[periods[j]][elem] = factor
			except KeyError:
				ret[periods[j]] = {}
				ret[periods[j]][elem] = factor

	return ret

def Load(values, mos_label, analysis_hour, wmo_id, target_param_name, season_id):

	try:
		password = os.environ["MOS_MOSRW_PASSWORD"]
	except:
		print "password should be given with env variable MOS_MOSRW_PASSWORD"
		sys.exit(1)

	dsn = "user=%s password=%s host=%s dbname=%s port=%s" % ("mos_rw", password, "vorlon.fmi.fi", "mos", 5432)

	conn = psycopg2.connect(dsn)
	conn.autocommit = 1

	dbparam = parameters[target_param_name].split('/')[0]

	psycopg2.extras.register_hstore(conn)

	cur = conn.cursor()

	sql = "SELECT id FROM param WHERE name = %s"

	cur.execute(sql, [dbparam,])

	row = cur.fetchone()

	if row == None:
		print "Parameter id not found for name %s" % (target_param_name)
		sys.exit(1)

	target_param_id = int(row[0])

	sql = "SELECT id FROM mos_version WHERE label = %s"

	cur.execute(sql, [mos_label,])

	row = cur.fetchone()

	if row == None:
		print "mos version not found for label %s" % (mos_label)
		sys.exit(1)

	mos_version_id = int(row[0])

	sql = "SELECT station_id FROM station_network_mapping WHERE network_id = 1 AND local_station_id::int = %s"

	cur.execute(sql, [wmo_id,])

	row = cur.fetchone()

	if row == None:
		print "station id not found for wmo id %s" % (wmo_id)
		sys.exit(1)

	station_id = int(row[0])

	sql = "SELECT id,name FROM level"

	cur.execute(sql)

	levels = {}

	for row in cur.fetchall():
		levels[row[1]] = row[0]

	sql = "SELECT id,name FROM param"

	cur.execute(sql)

	params = {}

	for row in cur.fetchall():
		params[row[1]] = row[0]

	count = 0

	for forecast_period,weightlist in values.items():
		count = count+1

		if set(weightlist.values()) == set(['0']):
			# all weights were zero --> this step is not supported and it will not
			# be inserted to database table
			continue
			
		sql ="""
INSERT INTO 
  mos_weight (mos_version_id, mos_period_id, analysis_hour, station_id, forecast_period, target_param_id, target_level_id, target_level_value, weights)
VALUES(%s, %s, %s, %s, %s * interval '1 hour', %s, 1, 0, %s)
"""

		try:
			cur.execute(sql, [mos_version_id, season_id, analysis_hour, station_id, forecast_period, target_param_id, weightlist])
		except psycopg2.IntegrityError,e:
			if e.pgcode == "23505":
				sql ="""
UPDATE mos_weight
SET weights = %s
WHERE
	mos_version_id = %s AND
	analysis_hour = %s AND
	station_id = %s AND
	forecast_period = %s * interval '1 hour' AND
	target_param_id = %s AND
	target_level_id = 1 AND
	target_level_value = 0
"""
				#print cur.mogrify(sql, (factor, mos_version_id, PRODUCER_ID, int(station_id), int(period), target_param_id, param_id, level_id, level_value))
				cur.execute(sql, (weightlist, mos_version_id, analysis_hour, int(station_id), int(forecast_period), target_param_id))
			else:
				print e	
				sys.exit(1)
	print "Inserted %s rows" % (count)

	conn.commit()

def main():

	opts = ParseCommandLine(sys.argv)

	global WMO_ID
	global TARGET_PARAM_NAME
	global AHOUR

	# Filename example:
	# station_8579_12_season1_TA_lm_MOS_constant_maxvars14.csv

	nameInfo = os.path.basename(opts.file[0]).split('_')

	wmo_id = None
	ahour = None
	target_param_name = None
	season_id = None

	if opts.wmo is not None:
		wmo_id = opts.wmo
	else:
		wmo_id = nameInfo[1]

	if opts.analysis_hour is not None:
		ahour = opts.analysis_hour
	else:
		ahour = nameInfo[2]

	if opts.param is not None:
		target_param_name = opts.param
	else:
		target_param_name = nameInfo[4]

	if opts.season is not None:
		season_id = opts.param
	else:
		season_id = nameInfo[3][-1]

	values = Read(opts.file[0])

	Load(values, opts.mos_label, ahour, wmo_id, target_param_name, season_id)

if __name__ == "__main__":
	main()
