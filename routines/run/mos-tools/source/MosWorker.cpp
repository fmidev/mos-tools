#include "MosWorker.h"
#include <sstream>
#include <fstream>

#include "Result.h"
#include <boost/numeric/ublas/vector.hpp>
#include <boost/numeric/ublas/matrix.hpp>

#ifdef DEBUG
#include <boost/numeric/ublas/io.hpp>
#endif

boost::posix_time::ptime ToPtime(const std::string& time, const std::string& timeMask);

std::string ToSQLInterval(int step)
{
	char ret[11];

	snprintf(ret, 11, "%02d:00:00", step);

	return std::string(ret);
}

std::string ToString(boost::posix_time::ptime p, const std::string& timeMask)
{
	assert(p != boost::date_time::not_a_date_time);

	std::stringstream s;
	std::locale l(s.getloc(), new boost::posix_time::time_facet(timeMask.c_str()));

	s.imbue(l);

	s << p;

	s.flush();

	return s.str();
}

boost::posix_time::ptime ToPtime(const std::string& time, const std::string& timeMask)
{
	std::stringstream s(time);
	std::locale l(s.getloc(), new boost::posix_time::time_input_facet(timeMask.c_str()));

	s.imbue(l);

	boost::posix_time::ptime p;

	s >> p;

	if (p == boost::date_time::not_a_date_time)
	{
		throw std::runtime_error("Unable to create time from '" + time + "' with mask '" + timeMask + "'");
	}

	return p;
}

void MosWorker::Write(const MosInfo& mosInfo, const Results& results)
{
	// Current time

	const boost::posix_time::ptime now = boost::posix_time::second_clock::local_time();
	std::string nowstr = ToString(now, "%Y%m%d%H%M%S");

	// leadtime

	if (results.empty())
	{
		std::cerr << "No results to write" << std::endl;
		return;
	}

	std::ofstream outfile;
	std::stringstream fileName;
	fileName << "mos_" 
			<< mosInfo.paramName 
			<< "_" 
			<< std::setw(3) << std::setfill('0')
			<< results.begin()->second.step
			<< ".txt";

	outfile.open(fileName.str());

	int paramId;

	// this could be replaced with a database lookup
	if (mosInfo.paramName == "T-K")
	{
		paramId = 153;
	}
	else if (mosInfo.paramName == "TMAX12H-K")
	{
		paramId = 738;
	}
	else if (mosInfo.paramName == "TMIN12H-K")
	{
		paramId = 739;
	}
	else
	{
		throw std::runtime_error("Unable to find id for parameter: " + mosInfo.paramName);
	}

	const int levelId = 1;
	const double levelValue = 0;

	outfile << "# "
	           "producer_id,analysis_time,station_id,param_id,level_id,level_value,level_value2,forecast_period,"
	           "forecast_type_id,forecast_type_value,value"
	        << std::endl;

	for (const auto& it : results)
	{
		const auto station = it.first;
		const auto result = it.second;

		boost::posix_time::ptime originTime = ToPtime(mosInfo.originTime, "%Y%m%d%H%M");

		outfile << mosInfo.producerId << "," << ToString(originTime, "%Y-%m-%d %H:%M:%S") << "," << station.wmoId << ","
		        << paramId << "," << levelId << "," << levelValue << ",-1," << ToSQLInterval(result.step) << ",1,-1,"
		        << result.value << std::endl;
	}

	if (mosInfo.traceOutput)
	{
		std::cout << "Writing trace" << std::endl;
		itsMosDB->WriteTrace(mosInfo, results, nowstr);
	}

	outfile.close();
	std::cout << "Wrote file '" << fileName.str() << "'" << std::endl;
}

MosWorker::MosWorker() { itsMosDB = std::unique_ptr<MosDB>(MosDBPool::Instance()->GetConnection()); }
MosWorker::~MosWorker()
{
	if (itsMosDB.get())
	{
		MosDBPool::Instance()->Release(itsMosDB.release());
	}
}

bool MosWorker::Mosh(const MosInfo& mosInfo, int step)
{
	// 1. Get weights

	std::cout << "Fetching weights" << std::endl;

	auto weights = itsMosDB->GetWeights(mosInfo, step);

	if (weights.empty())
	{
		std::cerr << "No weights for analysis time " << mosInfo.originTime << " step " << step << std::endl;
		return false;
	}

	// 2. Get raw forecasts

	std::cout << "Fetching source data for step " << step << std::endl;

	for (auto& it : weights)
	{
		Station station = it.first;
#ifdef DEBUG
		if (mosInfo.traceOutput)
		{
			std::cout << station.id << " " << station.name << " " << it.second.params.size() << " weights" << std::endl;
		}
#endif

		it.second.values.resize(it.second.weights.size(), 0);

		for (size_t i = 0; i < it.second.params.size(); i++)
		{
			ParamLevel pl = it.second.params[i];

			double value = kFloatMissing;

			if (pl.paramName == "INTERCEPT-N")
			{
				value = 1;
			}
			else
			{
				value = itsMosInterpolator.GetValue(mosInfo, station, pl, step);
			}

			if (value == kFloatMissing && it.second.weights[i] != 0)
			{
				if (pl.paramName == "CLDBASE-M")
				{
					it.second.values[i] = 20000;  // Number comes from J. Ylhaisi
					std::cout << "Missing value for station " << station.id << " " << station.name << " "
					          << Key(pl, step) << ", setting value to 20000" << std::endl;
				}
				else
				{
					it.second.weights[i] = 0;
					std::cout << "Missing value for station " << station.id << " " << station.name << " "
					          << Key(pl, step) << ", setting weight to zero" << std::endl;
				}
			}
			else
			{
				it.second.values[i] = value;
			}
		}
	}

	// 3. Apply

	std::cout << "Applying weights" << std::endl;

	Results results;

	for (const auto& it : weights)
	{
		Station station = it.first;

		Result r;

		r.value = boost::numeric::ublas::inner_prod(it.second.values, it.second.weights);
		r.weights = it.second;
		r.step = step;

		results[station] = r;
	}

	// 4. Write to file

	std::cout << "Writing results" << std::endl;

	Write(mosInfo, results);

	return true;
}
