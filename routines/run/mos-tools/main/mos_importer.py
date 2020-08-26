#!/usr/bin/env python3

import sys
import os
import psycopg2
import argparse
import math
import io

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

def LoadToDatabase(cur, tablename, buff, colbuff):
	sql = "INSERT INTO data." + tablename + " (station_id, analysis_time, forecast_period, parameter_id, level_id, level_value, value) VALUES (%s, %s, %s, %s, %s, %s, %s)"

	ret = 0

	try:
		cur.execute("SAVEPOINT insert")

		f = io.StringIO("\n".join(buff))

		cur.copy_from(f, tablename, columns=['station_id', 'analysis_time', 'forecast_period', 'parameter_id', 'level_id', 'level_value', 'value'])

		ret = len(buff)
		cur.execute("RELEASE SAVEPOINT insert")

	except psycopg2.IntegrityError as e:
		if e.pgcode == "23505":
			
			print("COPY failed, switch to INSERT")
	#		print cur.mogrify(sql, (station_id, analysis_time, period, param_id, level_id, level_value, arr[6]))
			cur.execute("ROLLBACK TO SAVEPOINT insert")

			for row in colbuff:
				ret = ret+1

				try:
					cur.execute("SAVEPOINT insert")
					cur.execute(sql, row)
					cur.execute("RELEASE SAVEPOINT insert")

				except psycopg2.IntegrityError as e:
					if e.pgcode != "23505":
						print(e)
						sys.exit(1)
					cur.execute("ROLLBACK TO SAVEPOINT insert")
					cur.execute("RELEASE SAVEPOINT insert")
		else:
			print(e)
			sys.exit(1)
	return ret

def Load(infile_name):
	infile = open (infile_name)

	dsn = "user=%s password=%s host=%s dbname=%s port=%s" % ("mos_rw",  os.environ["MOS_MOSRW_PASSWORD"], os.environ["MOS_HOSTNAME"], "mos", 5432)

	conn = psycopg2.connect(dsn)

	cur = conn.cursor()
	ReadPartitions(cur)
	
	lines = 0
	totlines = 0
	totrows = 0
	prevPartition = None
	buff = [] # data to be uploaded with COPY
	colbuff = [] # data to be uploaded with INSERT if COPY fails

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
		value = arr[6]

		partition = None
	
		if station_id % 10 == 0:
			partition = "previ_ecmos_narrow_p" + str(station_id)
		else:
			rup = int(math.floor(station_id / 10.0)) * 10
			partition = "previ_ecmos_narrow_p" + str(rup)

		if not partition in PARTITIONS:
			partition = "previ_ecmos_narrow"

		if prevPartition == None:
			prevPartition = partition

		if partition != prevPartition:
			rows = LoadToDatabase(cur, prevPartition,buff, colbuff)
			prevPartition = partition
			print("Insert row count: %d" % (rows))
			totrows += rows
			totlines += lines
			if rows != lines:
				print("Error: lines read=%d but rows loaded=%d" % (lines, rows))
				sys.exit(1)
			lines = 0
			buff = []
			colbuff = []

		buff.append("%s\t%s\t%s\t%s\t%s\t%s\t%s" % (station_id, analysis_time, period, param_id, level_id, level_value, value))
		colbuff.append([station_id, analysis_time, period, param_id, level_id, level_value, value])

		lines = lines+1

	rows = LoadToDatabase(cur, prevPartition,buff, colbuff)
	totrows += rows
	totlines += lines
	print("total rows: %d loaded to database: %d" % (totlines, totrows))
	conn.commit()

	if totlines != totrows:
		sys.exit(1)

def main():

	opts = ParseCommandLine(sys.argv)

	wmo_id = None
	ahour = None
	target_param_name = None
	season_id = None

	Load(opts.file[0])

if __name__ == "__main__":
	main()
