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

#ifndef LEGACY_MODE
	std::ofstream outfile;
	std::string fileName = "mos_" + boost::lexical_cast<std::string>(results.begin()->second.step) + ".txt";

	outfile.open(fileName);
	const int paramId = 153;  // T-K
	const int levelId = 1;
	const double levelValue = 0;

	outfile << "#producer_id,analysis_time,station_id,param_id,forecast_period,"
	           "level_id,level_value,value"
	        << std::endl;

#endif
	for (const auto& it : results)
	{
		const auto station = it.first;
		const auto result = it.second;

		boost::posix_time::ptime originTime = ToPtime(mosInfo.originTime, "%Y%m%d%H%M");

#ifdef LEGACY_MODE

		boost::posix_time::hours adjustment(result.step);
		boost::posix_time::ptime leadTime = originTime + adjustment;

		std::string fileName = "mos_" + boost::lexical_cast<std::string>(station.wmoId) + "_" +
		                       boost::lexical_cast<std::string>(result.step) + ".txt";

		std::ofstream outfile;
		outfile.open(fileName);

		outfile << "PreviVersion2" << std::endl;
		outfile << "ECMOS" << std::endl;
		outfile << "2140" << std::endl;
		outfile << "1" << std::endl;
		outfile << result.step << std::endl;
		outfile << "-999" << std::endl;
		outfile << station.wmoId << std::endl;
		outfile << mosInfo.originTime << " " << nowstr << std::endl;
		outfile << ToString(leadTime, "%Y%m%d%H%M") << " " << result.value << std::endl;

		outfile.close();
		std::cout << "Wrote file '" << fileName << "'" << std::endl;
#else
		outfile << mosInfo.producerId << "," << ToString(originTime, "%Y-%m-%d %H:%M:%S") << "," << station.wmoId << ","
		        << paramId << "," << result.step << "," << levelId << "," << levelValue << "," << result.value
		        << std::endl;
#endif
	}

	if (mosInfo.traceOutput)
	{
		std::cout << "Writing trace" << std::endl;
		itsMosDB->WriteTrace(mosInfo, results, nowstr);
	}

#ifndef LEGACY_MODE
	outfile.close();
	std::cout << "Wrote file '" << fileName << "'" << std::endl;
#endif
}

MosWorker::MosWorker() { itsMosDB = std::unique_ptr<MosDB>(MosDBPool::Instance()->GetConnection()); }
MosWorker::~MosWorker()
{
	if (itsMosDB)
	{
		MosDBPool::Instance()->Release(itsMosDB.get());
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
