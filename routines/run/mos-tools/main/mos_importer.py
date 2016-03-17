#!/usr/bin/env python

import sys
import os
import psycopg2
import argparse
import math

PARTITIONS = []

def ParseCommandLine(argv):
	parser = argparse.ArgumentParser()
	parser.add_argument('file', nargs='+', help='Input file (csv)')

 	args = parser.parse_args()

	return args

def ReadPartitions(cur):
	global PARTITIONS
	sql = "SELECT tablename FROM pg_tables WHERE tablename LIKE 'previ_ecmos_narrow_p%' ORDER BY 1"
	
	cur.execute(sql)

	rows = cur.fetchall()
	
	for row in rows:
		PARTITIONS.append(row[0])
	

def Load(infile_name):
	infile = open (infile_name)

	dsn = "user=%s password=%s host=%s dbname=%s port=%s" % ("mos_rw", "", "vorlon.fmi.fi", "mos", 5432)

	conn = psycopg2.connect(dsn)

	cur = conn.cursor()
	ReadPartitions(cur)
	
	count = 0

	for line in infile:
		line = line.strip()
	
		if line[0] == '#':
			continue
	
		arr = line.split(',')

		#analysis_time,forecast_period,station_id,param_id,level_id,levelvalue,value

		analysis_time = arr[0]
		period = int(arr[1])
		station_id = int(arr[2])
		param_id = int(arr[3])
		level_id = int(arr[4])
		level_value = int(arr[5])

		count = count+1

		partition = None
	
		if station_id % 10 == 0:
			partition = "previ_ecmos_narrow_p" + str(station_id)
		else:
			rup = int(math.floor(station_id / 10.0)) * 10
			partition = "previ_ecmos_narrow_p" + str(rup)

		if not partition in PARTITIONS:
			partition = "previ_ecmos_narrow"
			
		if count % 10000 == 0:
			print "Insert row count: %d" % (count)
			conn.commit()
		
		sql = "INSERT INTO data." + partition + " (station_id, analysis_time, forecast_period, parameter_id, level_id, level_value, value) VALUES (%s, %s, %s, %s, %s, %s, %s)"

		try:
#			print cur.mogrify(sql, (station_id, analysis_time, period, param_id, level_id, level_value, arr[6]))
			cur.execute(sql, (station_id, analysis_time, period, param_id, level_id, level_value, arr[6]))


		except psycopg2.IntegrityError,e:
			if e.pgcode == "23505":
#				sql =""

				print e	
#				sys.exit(1)
			else:
				print e	
				sys.exit(1)
	
	conn.commit()

def main():

	opts = ParseCommandLine(sys.argv)

	wmo_id = None
	ahour = None
	target_param_name = None
	season_id = None


	Load(opts.file[0])

if __name__ == "__main__":
	main()
