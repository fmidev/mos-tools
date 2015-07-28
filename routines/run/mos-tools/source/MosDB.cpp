#include "MosDB.h"
#include <sstream>
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
		std::string key = keys[i].paramName + "/" + keys[i].levelName + "/" + boost::lexical_cast<std::string> (keys[i].levelValue);
		ret << key << " => " << values[i] << ","; 
	}
	
	std::string str = ret.str();
	str.pop_back(); // remove last comma
	
	return str;
}

MosDB::MosDB()
{
	user_ = "mos_rw";
	password_ = "";
	database_ = "mos";
	hostname_ = "vorlon.fmi.fi";
}

MosDB::MosDB(int theId) : NFmiPostgreSQL(theId)
{
	user_ = "mos_rw";
	password_ = "";
	database_ = "mos";
	hostname_ = "vorlon.fmi.fi";
}

MosDB::~MosDB()
{
	Disconnect();
}

Weights MosDB::GetWeights(const MosInfo& mosInfo, int step, double relativity)
{
	assert(relativity >= 0 && relativity <= 1);

	Weights weights;

	std::stringstream query;

	// get period information

	std::string atime = mosInfo.originTime.substr(0,8);
	
	int year = boost::lexical_cast<int> (mosInfo.originTime.substr(0, 4));
	
	// This is a seemingly complex query that fetches several critical values with one query
	// First we create a list of dates using SQL WITH sub-query. mos_period table only holds
	// months and days for period start/stop, so in this query we create an actual date with year
	// part also so it's easier to compare dates.
	//
	// Since besides the current period we might also be interested in the previous period or
	// next period, we create a union query that includes previous and next year as well.
	//
	// In the actual query part (after WITH) we select previous, current and next values
	// all at once using SQL LEAD() and LAG().
	
	query << "WITH dates AS\n(";
	
	for (int i = -1; i <= 1; i++)
	{
		int nyear = year + i;
	
		query << "SELECT id, "
			<< "to_date('" << nyear << "' || to_char(start_month, 'FM09') || to_char(start_day, 'FM09'), 'yyyymmdd') AS start_date, "
			<< "CASE WHEN CAST(stop_month AS int) < CAST(start_month AS int)"
			<< "THEN to_date(CAST('" << nyear << "' AS int)+1 || to_char(stop_month, 'FM09') || to_char(stop_day, 'FM09'), 'yyyymmdd') "
			<< "ELSE to_date('" << nyear << "' || to_char(stop_month, 'FM09') || to_char(stop_day, 'FM09'), 'yyyymmdd') END AS stop_date "
			<< " FROM mos_period "
			;
	
		if (i < 1)
		{
			query << "\nUNION ALL\n";
		}			
	}
	
	query << " ORDER BY start_date, stop_date)\n"
			<< "SELECT id, to_char(start_date, 'YYYYMMDD'), to_char(stop_date, 'YYYYMMDD'), "
			<< "lag_id, to_char(lag_start_date, 'YYYYMMDD'), to_char(lag_stop_date, 'YYYYMMDD'), "
			<< "lead_id, to_char(lead_start_date, 'YYYYMMDD'), to_char(lead_stop_date, 'YYYYMMDD') "
			<< " FROM "
			<< "(SELECT d.id, d.start_date, d.stop_date,"
			<< "LAG(d.id,1) OVER () AS lag_id, LAG(d.start_date,1) OVER () AS lag_start_date, LAG(d.stop_date,1) OVER () AS lag_stop_date,"
			<< "LEAD(d.id,1) OVER () AS lead_id,LEAD(d.start_date,1) OVER () AS lead_start_date, LEAD(d.stop_date,1) OVER () AS lead_stop_date "
			<< "FROM dates d) ss WHERE "
			<< "start_date <= to_date('" << atime << "', 'yyyymmdd') AND stop_date > to_date('" << atime <<  "', 'yyyymmdd') "
			;
	
	Query(query.str());

	auto row = FetchRow();	
	
	if (row.empty())
	{
		return weights;
	}

	std::string period;
	std::string startDate;
	std::string stopDate;
	
	if (relativity < 0.5)
	{
		period = row[3];
		startDate = row[4];
		stopDate = row[5];
	}
	else if (fabs(relativity - 0.5) < 0.001)
	{
		period = row[0];
		startDate = row[1];
		stopDate = row[2];
	}
	else if (relativity > 0.5)
	{
		period = row[6];
		startDate = row[7];
		stopDate = row[8];
	}
	
	if (period.empty())
	{
		return weights;
	}

	int periodId = boost::lexical_cast<int> (period);
	periodId=1;
	query.str("");
	
	query << "SELECT "
		<< "CAST(akeys(f.weights) AS text) AS weight_keys, " // need to cast hstore to text in order for otl not to truncate the resulting string
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
		<< "analysis_hour = " << mosInfo.originTime.substr(8,2) << " AND "
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
		for (size_t i = 0;i < row.size();i++) std::cout << i << " " << row[i] << std::endl;
#endif

		std::vector<std::string> weightkeysstr, weightvalsstr;
		
		boost::trim_if(row[0], boost::is_any_of("{}")); // remove {} that come from database as column if of type "array"
		boost::trim_if(row[1], boost::is_any_of("{}"));
		
		boost::split(weightkeysstr, row[0], boost::is_any_of(","));
		boost::split(weightvalsstr, row[1], boost::is_any_of(","));

		Weight w;
		Station s;

		w.weights.resize(weightkeysstr.size(), 0);

		int i = 0;
		
		std::for_each(weightvalsstr.begin(), weightvalsstr.end(), [&](const std::string& val){ 

			double dval = boost::lexical_cast<double> (val);
			
			assert(dval == dval); // no NaN
					
			w.weights[i++] = dval;
		});

		std::for_each(weightkeysstr.begin(), weightkeysstr.end(), [&](std::string& val){
			ParamLevel pl(val);
			
			if (pl.paramName == "TD-K")
			{
				pl.paramName = "TD-C";
			}
			else if (pl.paramName == "P-PA" && pl.levelName == "MEANSEA")
			{
				pl.levelName = "GROUND";
			}
			
			w.params.push_back(pl);
		});

		w.step = step;
		w.startDate = startDate;
		w.stopDate = stopDate;
		w.periodId = periodId;

		s.id = boost::lexical_cast<int> (row[6]);
		s.wmoId = boost::lexical_cast<int> (row[2]);
		s.latitude = boost::lexical_cast<double> (row[3]);
		s.longitude = boost::lexical_cast<double> (row[4]);
		s.name = row[5];
		
		assert(w.params.size() == w.weights.size());

		if (!w.params.empty()) weights[s] = w;
	}

//	std::cout << w.params.size() << " vs " << w.weights.size() << std::endl;
	
	if (mosInfo.traceOutput)
	{
		std::string periodInfo = "current";
	
		if (relativity < 0.5) periodInfo = "previous";
		else if (relativity > 0.5) periodInfo = "next";
	
		std::cout << "Read " << periodInfo << " period weights for " << weights.size() << " stations" << std::endl;
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

	mosInfo.id = boost::lexical_cast<int> (row[0]);
	mosInfo.label = mosLabel;
	mosInfo.producerId = boost::lexical_cast<int> (row[2]);
	
	return mosInfo;
}

void MosDB::WriteTrace(const MosInfo& mosInfo, const Result& result, const Station& station, const std::string& run_time)
{
	std::stringstream query;
	
	query << "INSERT INTO mos_trace "
			<< "(mos_version_id, mos_period_id, mos_other_period_id, analysis_time, station_id, forecast_period, target_param_id, target_level_id, target_level_value, weights, source_values, value, run_time) "
			<< "SELECT "
			<< mosInfo.id << ","
			<< result.weights.periodId << ","
			<< "NULL,"
			<< "to_timestamp('" << mosInfo.originTime << "', 'yyyymmddhh24mi'),"
			<< station.id << ","
			<< result.weights.step << " * interval '1 hour',"
			<< "p.id,"
			<< "l.id,"
			<< "0,"
			<< "'" << ToHstore(result.weights.params, result.weights.weights) << "',"
			<< "'" << ToHstore(result.weights.params, result.weights.values) << "',"
			<< result.value << ","
			<< "to_timestamp('" << run_time << "', 'yyyymmddhh24miss') "
			<< " FROM param p, level l WHERE p.name = '" << mosInfo.paramName << "' AND l.name = 'GROUND'"
			;
	
	Execute(query.str());
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

MosDBPool::MosDBPool()
  : itsMaxWorkers(10)
  , itsWorkingList(itsMaxWorkers, -1)
  , itsWorkerList(itsMaxWorkers, NULL)
{}

MosDBPool::~MosDBPool()
{
  for (unsigned int i = 0; i < itsWorkerList.size(); i++) {
    if (itsWorkerList[i]) {   
      itsWorkerList[i]->Disconnect();
      delete itsWorkerList[i];
    }
  }

  delete itsInstance;
}

MosDB * MosDBPool::GetConnection() {
 
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

  while (true) {

    for (unsigned int i = 0; i < itsWorkingList.size(); i++) {
      // Return connection that has been initialized but is idle
      if (itsWorkingList[i] == 0) {
        itsWorkingList[i] = 1;


#ifdef DEBUG
        std::cout << "DEBUG: Idle worker returned with id " << itsWorkerList[i]->Id() << std::endl;
#endif
        return itsWorkerList[i];
      
      } 
	  else if (itsWorkingList[i] == -1) {
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

    usleep(100000); // 100 ms  

  }   
}

void MosDBPool::Release(MosDB *theWorker) {
  
  std::lock_guard<std::mutex> lock(itsReleaseMutex);

  theWorker->Rollback();
  itsWorkingList[theWorker->Id()] = 0;

#ifdef DEBUG
  std::cout << "DEBUG: Worker released for id " << theWorker->Id() << std::endl;
#endif

}
