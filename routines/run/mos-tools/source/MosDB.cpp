#include "MosDB.h"
#include <sstream>
#include <iomanip>
#include <unistd.h>
#include <boost/lexical_cast.hpp>
#include <boost/foreach.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string_regex.hpp>

#ifdef DEBUG
#include <boost/numeric/ublas/io.hpp>
#endif

std::string ToHstore(const std::vector<ParamLevel>& keys, const boost::numeric::ublas::vector<double>& values)
{
	std::stringstream ret;

	assert(keys.size() == values.size());

	for (size_t i = 0; i < keys.size(); i++)
	{
		std::string key = keys[i].paramName + "/" + keys[i].levelName + "/" +
		                  boost::lexical_cast<std::string>(keys[i].levelValue) + "/" +
		                  boost::lexical_cast<std::string>(keys[i].stepAdjustment);
		ret << key << " => " << values[i] << ",";
	}

	std::string str = ret.str();
	str.pop_back();  // remove last comma

	return str;
}

std::string GetPassword(const std::string& username)
{
	const auto pw = getenv(username.c_str());

	if (pw)
	{
		return std::string(pw);
	}
	else
	{
		throw std::runtime_error("Password should be given with env variable " + username);
	}
}

MosDB::MosDB()
{
	user_ = "mos_rw";
	password_ = GetPassword("MOS_MOSRW_PASSWORD");
	database_ = "mos";
	hostname_ = "vorlon.fmi.fi";
}

MosDB::MosDB(int theId) : NFmiPostgreSQL(theId)
{
	user_ = "mos_rw";
	password_ = GetPassword("MOS_MOSRW_PASSWORD");
	database_ = "mos";
	hostname_ = "vorlon.fmi.fi";
}

MosDB::~MosDB() { Disconnect(); }
Weights MosDB::GetWeights(const MosInfo& mosInfo, int step)
{
	Weights weights;

	std::stringstream query;

	// get period information

	std::string atime = mosInfo.originTime.substr(0, 10);

	int year = boost::lexical_cast<int>(mosInfo.originTime.substr(0, 4));
	int month = boost::lexical_cast<int>(mosInfo.originTime.substr(5, 2));
	int day = boost::lexical_cast<int>(mosInfo.originTime.substr(8, 2));

	if (month == 2 && day == 29)
	{
		// LOL karkauspäivä
		day = 28;
	}

	// Create a list of dates, like:
	//
	// id |   start    |    stop
	// ----+------------+------------
	// 1 | 2015-12-01 | 2016-02-28
	// 2 | 2016-03-01 | 2016-05-31
	// 3 | 2016-06-01 | 2016-08-31
	// 4 | 2016-09-01 | 2016-11-30
	// 1 | 2016-12-01 | 2017-02-28

	query << "WITH times AS ("
	      << "SELECT id,CASE "
	      << "WHEN id = 1 THEN to_date('" << (year - 1) << "-'||start_month||'-'||start_day , 'yyyy-mm-dd') "
	      << "ELSE to_date('" << year << "-'||start_month||'-'||start_day , 'yyyy-mm-dd') END AS start,"
	      << "to_date('" << year << "-'||stop_month||'-'||stop_day, 'yyyy-mm-dd') AS stop FROM mos_period "
	      << "UNION "
	      << "SELECT id, "
	      << "to_date('" << year << "-'||start_month||'-'||start_day , 'yyyy-mm-dd'),"
	      << "to_date('" << (year + 1) << "-'||stop_month||'-'||stop_day, 'yyyy-mm-dd') FROM mos_period WHERE id = 1) "
	      << "SELECT id FROM times WHERE to_date('" << year << "-" << std::setfill('0') << std::setw(2) << month << "-"
	      << std::setfill('0') << std::setw(2) << day << "', 'yyyy-mm-dd') BETWEEN start AND stop";

	Query(query.str());

	auto row = FetchRow();

	if (row.empty())
	{
		return weights;
	}

	query.str("");

	int periodId = boost::lexical_cast<int>(row[0]);

#ifdef EXTRADEBUG
	std::cout << "Got period id " << periodId << std::endl;
#endif

	query << "SELECT "
	      << "CAST(akeys(f.weights) AS text) AS weight_keys, "  // need to cast hstore to text in order for otl not to
	                                                            // truncate the resulting string
	      << "CAST(avals(f.weights) AS text) AS weight_vals, "
	      << "snm.local_station_id AS wmo_id, "
	      << "st_y(s.position), "
	      << "st_x(s.position), "
	      << "s.name, "
	      << "s.id "
	      << "FROM mos_weight f, mos_version v, station_network_mapping snm, station s, param p, mos_period pe WHERE "
	      << "v.label = '" << mosInfo.label << "' AND "
	      << "p.name = '" << mosInfo.paramName << "' AND "
	      << "p.id = f.target_param_id AND "
	      << "snm.network_id = 1 AND "
	      << "snm.station_id = s.id AND "
	      << "f.station_id = s.id AND "
	      << "f.mos_version_id = v.id AND "
	      << "pe.id = f.mos_period_id AND "
	      << "extract(epoch FROM f.forecast_period)/3600 = " << step << " AND "
	      << "analysis_hour = " << mosInfo.originTime.substr(11, 2) << " AND "
	      << "pe.id = " << periodId << " "
	      << "ORDER BY wmo_id, forecast_period, weight_keys";

	Query(query.str());

	while (true)
	{
		row = FetchRow();

		if (row.empty())
		{
			break;
		}
#ifdef EXTRADEBUG
		for (size_t i = 0; i < row.size(); i++) std::cout << i << " " << row[i] << std::endl;
#endif

		std::vector<std::string> weightkeysstr, weightvalsstr;

		boost::trim_if(row[0],
		               boost::is_any_of("{}"));  // remove {} that come from database as column if of type "array"
		boost::trim_if(row[1], boost::is_any_of("{}"));

		boost::split(weightkeysstr, row[0], boost::is_any_of(","));
		boost::split(weightvalsstr, row[1], boost::is_any_of(","));

		Weight w;
		Station s;

		w.weights.resize(weightkeysstr.size(), 0);
		w.params.resize(weightkeysstr.size());

		assert(weightkeysstr.size() == weightvalsstr.size());

		for (size_t i = 0; i < weightvalsstr.size(); i++)
		{
			double val = boost::lexical_cast<double>(weightvalsstr[i]);
			assert(val == val);  // no NaN

			w.weights[i] = val;

			std::string key = weightkeysstr[i];

			ParamLevel pl(key);

			w.params[i] = pl;
		}

		w.step = step;
		// w.startDate = startDate;
		// w.stopDate = stopDate;
		w.periodId = periodId;

		s.id = boost::lexical_cast<int>(row[6]);
		s.wmoId = boost::lexical_cast<int>(row[2]);
		s.latitude = boost::lexical_cast<double>(row[3]);
		s.longitude = boost::lexical_cast<double>(row[4]);
		s.name = row[5];

		assert(w.params.size() == w.weights.size());

		if (!w.params.empty()) weights[s] = w;
	}

	if (mosInfo.traceOutput)
	{
		std::cout << "Read weights for " << weights.size() << " stations" << std::endl;
	}

	return weights;
}

MosInfo MosDB::GetMosInfo(const std::string& mosLabel)
{
	MosInfo mosInfo;

	std::stringstream s;

	s << "SELECT id,label,producer_id FROM mos_version WHERE label = '" << mosLabel << "'";

	Query(s.str());

	auto row = FetchRow();

	if (row.empty())
	{
		throw std::runtime_error("Mos with label '" + mosLabel + "' not found");
	}

	mosInfo.id = boost::lexical_cast<int>(row[0]);
	mosInfo.label = mosLabel;
	mosInfo.producerId = boost::lexical_cast<int>(row[2]);

	return mosInfo;
}

void MosDB::WriteTrace(const MosInfo& mosInfo, const Results& results, const std::string& run_time)
{
	std::stringstream query;

	BOOST_FOREACH (const auto& it, results)
	{
		const auto station = it.first;
		const auto result = it.second;

		query.str("");

		query << "INSERT INTO mos_trace "
		      << "(mos_version_id, mos_period_id, mos_other_period_id, analysis_time, station_id, forecast_period, "
		         "target_param_id, target_level_id, target_level_value, weights, source_values, value, run_time) "
		      << "SELECT " << mosInfo.id << "," << result.weights.periodId << ","
		      << "NULL,"
		      << "to_timestamp('" << mosInfo.originTime << "', 'yyyy-mm-dd hh24:mi:ss')," << station.id << ","
		      << result.weights.step << " * interval '1 hour',"
		      << "p.id,"
		      << "l.id,"
		      << "0,"
		      << "'" << ToHstore(result.weights.params, result.weights.weights) << "',"
		      << "'" << ToHstore(result.weights.params, result.weights.values) << "'," << result.value << ","
		      << "to_timestamp('" << run_time << "', 'yyyy-mm-dd hh24:mi:ss') "
		      << " FROM param p, level l WHERE p.name = '" << mosInfo.paramName << "' AND l.name = 'GROUND'";

		Execute(query.str());
	}

	Commit();
}

MosDBPool* MosDBPool::itsInstance = NULL;

MosDBPool* MosDBPool::Instance()
{
	if (!itsInstance)
	{
		itsInstance = new MosDBPool();
	}

	return itsInstance;
}

MosDBPool::MosDBPool() : itsMaxWorkers(10), itsWorkingList(itsMaxWorkers, -1), itsWorkerList(itsMaxWorkers, NULL) {}
MosDBPool::~MosDBPool()
{
	for (unsigned int i = 0; i < itsWorkerList.size(); i++)
	{
		if (itsWorkerList[i])
		{
			itsWorkerList[i]->Disconnect();
			delete itsWorkerList[i];
		}
	}

	delete itsInstance;
}

MosDB* MosDBPool::GetConnection()
{
	/*
	 *  1 --> active
	 *  0 --> inactive
	 * -1 --> uninitialized
	 *
	 * Logic of returning connections:
	 *
	 * 1. Check if worker is idle, if so return that worker.
	 * 2. Check if worker is uninitialized, if so create worker and return that.
	 * 3. Sleep and start over
	 */

	std::lock_guard<std::mutex> lock(itsGetMutex);

	while (true)
	{
		for (unsigned int i = 0; i < itsWorkingList.size(); i++)
		{
			// Return connection that has been initialized but is idle
			if (itsWorkingList[i] == 0)
			{
				itsWorkingList[i] = 1;

#ifdef DEBUG
				std::cout << "DEBUG: Idle worker returned with id " << itsWorkerList[i]->Id() << std::endl;
#endif
				return itsWorkerList[i];
			}
			else if (itsWorkingList[i] == -1)
			{
				// Create new connection
				itsWorkerList[i] = new MosDB(i);
				itsWorkerList[i]->Connect();

				itsWorkingList[i] = 1;

#ifdef DEBUG
				std::cout << "DEBUG: New worker returned with id " << itsWorkerList[i]->Id() << std::endl;
#endif
				return itsWorkerList[i];
			}
		}

// All threads active
#ifdef DEBUG
		std::cout << "DEBUG: Waiting for worker release" << std::endl;
#endif

		usleep(100000);  // 100 ms
	}
}

void MosDBPool::Release(MosDB* theWorker)
{
	std::lock_guard<std::mutex> lock(itsReleaseMutex);

	theWorker->Rollback();
	itsWorkingList[theWorker->Id()] = 0;

#ifdef DEBUG
	std::cout << "DEBUG: Worker released for id " << theWorker->Id() << std::endl;
#endif
}
