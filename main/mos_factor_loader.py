#!/usr/bin/env python3

import sys
import os
import psycopg2
import psycopg2.extras
import argparse
import fnmatch
import time
import glob

parameters = {
    # "CSV_PARAMETER_NAME : DATABASE_PARAMETER_NAME/DATABASE_LEVEL_NAME/LEVEL_VALUE/TIME_STEP_ADJUSTMENT"
    "T2_ENSMEAN_MA1": "T-MEAN-K/GROUND/0/0/-1",
    "TA": "T-K/GROUND/0",
    "TAMAX12H": "TMAX12H-K/GROUND/0",
    "TAMIN12H": "TMIN12H-K/GROUND/0",
    "TD": "TD-K/GROUND/0",
    "DECLINATION": "DECLINATION-N/NOLEVEL/0",
    "Intercept": "INTERCEPT-N/NOLEVEL/0",
    "SD": "SD-M/GROUND/0",
    "MSL": "P-PA/MEANSEA/0",
    "T2": "T-K/GROUND/0",
    "T2_M1": "T-K/GROUND/0/-1",
    "D2": "TD-K/GROUND/0",
    "TP": "RR-KGM2/GROUND/0",
    "STR": "RNETLW-WM2/GROUND/0",
    "LCC": "NL-PRCNT/GROUND/0",
    "MCC": "NM-PRCNT/GROUND/0",
    "HCC": "NH-PRCNT/GROUND/0",
    "CIN": "CIN-JKG/GROUND/0",
    "CAPE": "CAPE-JKG/GROUND/0",
    "U10": "U-MS/GROUND/0",
    "V10": "V-MS/GROUND/0",
    "CP": "RRC-KGM2/GROUND/0",
    "LSP": "RRL-KGM2/GROUND/0",
    "SSHF": "FLSEN-JM2/GROUND/0",
    "STRD": "RADLW-WM2/GROUND/0",
    "FG10_3": "FFG3H-MS/GROUND/0",
    "SKT": "SKT-K/GROUND/0",
    "MX2T3": "TMAX3H-K/GROUND/0",
    "MN2T3": "TMIN3H-K/GROUND/0",
    "MX2T": "TMAX-K/GROUND/0",
    "MN2T": "TMIN-K/GROUND/0",
    "TCW": "TOTCW-KGM2/GROUND/0",
    "SLHF": "FLLAT-JM2/GROUND/0",
    "CBH": "CLDBASE-M/GROUND/0",
    "SSR": "RNETSW-WM2/GROUND/0",
    "BLH": "MIXHGT-M/GROUND/0",
    "DEG0": "H0C-M/GROUND/0",
    "GH_950": "Z-M2S2/PRESSURE/950",
    "GH_925": "Z-M2S2/PRESSURE/925",
    "GH_850": "Z-M2S2/PRESSURE/850",
    "GH_700": "Z-M2S2/PRESSURE/700",
    "GH_500": "Z-M2S2/PRESSURE/500",
    "RH_950": "RH-PRCNT/PRESSURE/950",
    "RH_925": "RH-PRCNT/PRESSURE/925",
    "RH_850": "RH-PRCNT/PRESSURE/850",
    "RH_700": "RH-PRCNT/PRESSURE/700",
    "RH_500": "RH-PRCNT/PRESSURE/500",
    "T_950": "T-K/PRESSURE/950",
    "T_950_M1": "T-K/PRESSURE/950/-1",
    "T_925_M1": "T-K/PRESSURE/925/-1",
    "T_925": "T-K/PRESSURE/925",
    "T_850": "T-K/PRESSURE/850",
    "T_700": "T-K/PRESSURE/700",
    "T_500": "T-K/PRESSURE/500",
    "W_950": "VV-PAS/PRESSURE/950",
    "W_925": "VV-PAS/PRESSURE/925",
    "W_850": "VV-PAS/PRESSURE/850",
    "W_700": "VV-PAS/PRESSURE/700",
    "W_500": "VV-PAS/PRESSURE/500",
    "Z_925": "Z-M2S2/PRESSURE/925",
    "Z_850": "Z-M2S2/PRESSURE/850",
    "Z_700": "Z-M2S2/PRESSURE/700",
    "Z_500": "Z-M2S2/PRESSURE/500",
}

mos_version_id = None
station_cache = None
plan_created = False


def ParseCommandLine(argv):
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "-m", "--mos-label", required=True, help="mos label (version) name"
    )
    parser.add_argument("-a", "--analysis-hour", required=False, help="analysis hour")
    parser.add_argument(
        "-w", "--station_id", required=False, help="id of station in the given network"
    )
    parser.add_argument(
        "-n",
        "--network-id",
        required=False,
        default=1,
        help="id of network, 1=wmo, 5=fmisid (default: 1)",
    )
    parser.add_argument(
        "-p", "--param", required=False, help="radon parameter name of target param"
    )
    parser.add_argument(
        "-s", "--season", required=False, help="season id, 1=winter, 3=summer"
    )
    parser.add_argument(
        "--delete",
        action="store_true",
        help="delete weights specified by the other arguments from database",
    )
    parser.add_argument("file", nargs="*", help="Input file (csv)")

    args = parser.parse_args()

    return args


def Read(infile):
    ret = {}

    f = open(infile)

    i = 0
    periods = None

    for line in f:
        i = i + 1

        line = line.strip()

        if i == 1:
            # header
            periods = line.split(",")
            periods = [int(x.strip().replace('"', "")) for x in periods[1:]]
            assert len(periods) > 0
            continue

        factors = line.split(",")
        factors = [x.strip() for x in factors]
        param = factors[0].replace('"', "")

        # remove first element from list which is the param name
        factors.pop(0)

        assert len(factors) == len(periods)

        elem = None

        try:
            elem = parameters[param]
        except KeyError:
            print("Unknown parameter %s" % (param))
            sys.exit(1)

        for j, factor in enumerate(factors):
            try:
                ret[periods[j]][elem] = factor
            except KeyError:
                ret[periods[j]] = {}
                ret[periods[j]][elem] = factor

    f.close()

    return ret


def GetMosVersionId(cur, mos_label):
    global mos_version_id

    if mos_version_id is None:
        sql = "SELECT id FROM mos_version WHERE label = %s"

        cur.execute(
            sql,
            [
                mos_label,
            ],
        )

        row = cur.fetchone()

        if row == None:
            print("mos version not found for label %s" % (mos_label))
            sys.exit(1)

        mos_version_id = int(row[0])

    return mos_version_id


def GetStationId(cur, network_id, station_id):
    global station_cache

    if station_cache is None:
        print("Fill station cache")
        station_cache = {}
        sql = "SELECT local_station_id,station_id FROM station_network_mapping WHERE network_id = %s"  # AND local_station_id::int = %s"
        cur.execute(sql, [network_id])

        rows = cur.fetchall()
        for row in rows:
            station_cache[row[0]] = row[1]

    return station_cache[station_id]


def Load(
    cur,
    values,
    mos_label,
    analysis_hour,
    network_id,
    station_id,
    target_param_name,
    season_id,
):
    start = time.process_time()

    global plan_created
    dbparam = parameters[target_param_name].split("/")[0]

    sql = "SELECT id FROM param WHERE name = %s"

    cur.execute(
        sql,
        [
            dbparam,
        ],
    )

    row = cur.fetchone()

    if row == None:
        print("Parameter id not found for name %s" % (target_param_name))
        sys.exit(1)

    target_param_id = int(row[0])

    mos_version_id = GetMosVersionId(cur, mos_label)

    try:
        db_station_id = GetStationId(cur, network_id, station_id)
    except KeyError as e:
        print("Unrecognized station {}".format(station_id))
        return

    count = 0

    if plan_created is False:
        inssql = """
PREPARE insertplan AS INSERT INTO 
  mos_weight (mos_version_id, mos_period_id, analysis_hour, station_id, forecast_period, target_param_id, target_level_id, target_level_value, weights)
VALUES ($1, $2, $3, $4, $5 * interval '1 hour', $6, 1, 0, $7)
"""

        cur.execute(inssql)
        plan_created = True

    for forecast_period, weightlist in list(values.items()):
        count = count + 1

        if set(weightlist.values()) == set(["0"]):
            # all weights were zero --> this step is not supported and it will not
            # be inserted to database table
            continue

        str = '"'
        for k, v in list(weightlist.items()):
            str += k + " => " + v + ","

        str = str[:-1] + '"'

        # print ("%s,%s,%s,%s,%s,%s,%s" % (mos_version_id, season_id, analysis_hour, network_id, db_station_id, forecast_period, target_param_id))

        try:
            cur.execute(
                "EXECUTE insertplan (%s,%s,%s,%s,%s,%s,%s)",
                [
                    mos_version_id,
                    season_id,
                    analysis_hour,
                    db_station_id,
                    forecast_period,
                    target_param_id,
                    weightlist,
                ],
            )
        except psycopg2.IntegrityError as e:
            if e.pgcode == "23505":
                sql = """
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
                # print (cur.mogrify(sql, (factor, mos_version_id, PRODUCER_ID, int(station_id), int(period), target_param_id, param_id, level_id, level_value)))
                cur.execute(
                    sql,
                    (
                        weightlist,
                        mos_version_id,
                        analysis_hour,
                        db_station_id,
                        int(forecast_period),
                        target_param_id,
                    ),
                )
            else:
                print(e)
                sys.exit(1)
    print("Inserted {} rows in {:.2f} sec".format(count, (time.process_time() - start)))


def meta_from_name(filename, opts):
    # Filename example:
    # station_8579_12_season1_TA_lm_MOS_constant_maxvars14.csv
    sys.stderr.write(filename + "\n")
    nameInfo = filename.split("_")
    ret = {}

    if opts.station_id is not None:
        ret["station_id"] = opts.station_id
    else:
        ret["station_id"] = nameInfo[1]

    if opts.analysis_hour is not None:
        ret["ahour"] = opts.analysis_hour
    else:
        ret["ahour"] = nameInfo[2]

    if opts.param is not None:
        ret["target_param_name"] = opts.param
    else:
        ret["target_param_name"] = nameInfo[4]

    if opts.season is not None:
        ret["season_id"] = opts.param
    else:
        ret["season_id"] = nameInfo[3][-1]

    ret["network_id"] = opts.network_id

    return ret


def delete_weights(cur, opts):
    query = "DELETE FROM mos_weight WHERE mos_version_id = (SELECT id FROM mos_version WHERE label = %s)"
    args = (opts.mos_label,)

    if opts.analysis_hour is not None:
        query += " AND analysis_hour = %s"
        args += (opts.analysis_hour,)
    if opts.station_id is not None:
        nid = 1
        if opts.network_id is not None:
            nid = opts.network_id

        query += " AND station_id = (SELECT id FROM station_network_mapping WHERE network_id = %s AND local_station_id = %s)"
        args += (nid, opts.station_id)
    if opts.param is not None:
        query += " AND target_param_id = (SELECT id FROM param WHERE name = %s)"
        args += (opts.param,)
    if opts.season is not None:
        query += " AND mos_period_id = %s"
        args += (opts.season,)

    print("Deleting all weights from database matching the following conditions:")
    print(
        "MOS label: {}\nanalysis_hour: {}\nstation: {}\nparam: {}\nseason: {}\n\n'None' means all values are accepted\n".format(
            opts.mos_label, opts.analysis_hour, opts.station_id, opts.param, opts.season
        )
    )
    confirm = input("To continue type yes: ")
    if confirm == "yes":
        cur.execute(query, args)
        print("{} rows deleted".format(cur.rowcount))
    else:
        print("Aborting")


def main():
    opts = ParseCommandLine(sys.argv)

    try:
        password = os.environ["MOS_MOSRW_PASSWORD"]
    except:
        print("password should be given with env variable MOS_MOSRW_PASSWORD")
        sys.exit(1)

    dsn = "user=%s password=%s host=%s dbname=%s port=%s" % (
        "mos_rw",
        password,
        os.environ["MOS_HOSTNAME"],
        "mos",
        5432,
    )

    conn = psycopg2.connect(dsn)
    cur = conn.cursor()
    conn.autocommit = 1

    if opts.delete:
        delete_weights(cur, opts)
        return

    psycopg2.extras.register_hstore(conn)

    if os.path.isdir(opts.file[0]):
        count = 0
        files = glob.glob("{}/station*.csv".format(opts.file[0]))
        for filename in files:
            count = count + 1
            nameInfo = meta_from_name(os.path.basename(filename), opts)
            values = Read(filename)
            Load(
                cur,
                values,
                opts.mos_label,
                nameInfo["ahour"],
                nameInfo["network_id"],
                nameInfo["station_id"],
                nameInfo["target_param_name"],
                nameInfo["season_id"],
            )

            if count % 1000 == 0:
                print("Committing after {} files".format(count))
                conn.commit()
    else:
        nameInfo = meta_from_name(os.path.basename(opts.file[0]), opts)

        values = Read(opts.file[0])
        Load(
            cur,
            values,
            opts.mos_label,
            nameInfo["ahour"],
            nameInfo["network_id"],
            nameInfo["station_id"],
            nameInfo["target_param_name"],
            nameInfo["season_id"],
        )

    conn.commit()


if __name__ == "__main__":
    main()
