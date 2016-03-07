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

double XPosition(const MosInfo& mosInfo, const Weight& weight)
{
	// position of current origin time within season

	using namespace boost::posix_time;
	
	ptime epoch = time_from_string("1970-01-01 00:00:00.000");
	
	auto periodStart = ToPtime(weight.startDate, "%Y%m%d");
	auto periodStop = ToPtime(weight.stopDate, "%Y%m%d");

	auto periodPosition = ToPtime(mosInfo.originTime.substr(0,8), "%Y%m%d");
	
	long long start = (periodStart - epoch).total_milliseconds() / 1000;
	long long stop = (periodStop - epoch).total_milliseconds() / 1000;
	long long pos = (periodPosition - epoch).total_milliseconds() / 1000;
	
	assert(pos >= start && pos < stop);

	double xposition = (pos - start) / static_cast<double> ((stop - start));
	
	assert (xposition >= 0 && xposition <= 1);

	if (mosInfo.traceOutput)
	{
		std::cout << "Period start: " << ToString(periodStart, "%Y%m%d") << " Period stop: " << ToString(periodStop, "%Y%m%d") << " Current date: " << ToString(periodPosition, "%Y%m%d") << std::endl;
		std::cout << "Relative position of current forecast within period [0..1]: " << xposition << std::endl;
	}

	return xposition;
}

Weights AdjustWeights(const MosInfo& mosInfo, const Weights& weights, const Weights& otherWeights, double xposition)
{
	// using sin(x) to adjust weights between current season and other season
	
	assert(weights.size() == otherWeights.size());

	// x = 0 --> 0
	// x = 0.5 --> 1
	// x = 1 --> 0
	
	double yposition = sin(3.1415 * xposition) * 0.5;

	if (mosInfo.traceOutput)
	{
		std::cout << "Current period has a weight of [0..1]: " << yposition << std::endl;
	}
	
	for(const auto& it : weights)
	{
		const auto station = it.first;

		if (otherWeights.find(station) == otherWeights.end())
		{
			// no other weight for this station
			continue; 
		}
		
		const auto weight = it.second;
		const auto otherWeight = otherWeights.at(station);
		
		for (size_t i = 0; i < otherWeight.weights.size(); i++)
		{
			double w = weight.weights[i];
			double ow = otherWeight.weights[i];
			double nw = w;

			if (w != ow)
			{
				nw = yposition * w + (1 - yposition) * nw ;
				
				if (mosInfo.traceOutput)
				{
					std::cout << "Current period weight: " << w << "\tOther period weight: " << ow << "\tNew weight: " << nw << std::endl;
				}
			}
		}
	}

	return weights;
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
	std::string fileName = "mos_" + boost::lexical_cast<std::string> (results.begin()->second.step) + ".txt";

	outfile.open (fileName);
	const int paramId = 153; // T-K
	const int levelId = 1;
	const double levelValue = 0;

	outfile << "#producer_id,analysis_time,station_id,param_id,forecast_period,level_id,level_value,value" << std::endl;
	
#endif
	for(const auto& it : results)
	{
		const auto station = it.first;
		const auto result = it.second;

		boost::posix_time::ptime originTime = ToPtime(mosInfo.originTime, "%Y%m%d%H%M");

#ifdef LEGACY_MODE

		boost::posix_time::hours adjustment (result.step);
		boost::posix_time::ptime leadTime = originTime + adjustment;

		std::string fileName = "mos_" + boost::lexical_cast<std::string> (station.wmoId) + "_" + boost::lexical_cast<std::string> (result.step) + ".txt";
		
		std::ofstream outfile;
		outfile.open (fileName);
		
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
		outfile << mosInfo.producerId 
				<< "," 
				<< ToString(originTime, "%Y-%m-%d %H:%M:%S") 
				<< "," 
				<< station.wmoId 
				<< "," 
				<< paramId 
				<< "," 
				<< result.step 
				<< "," 
				<< levelId 
				<< "," 
				<< levelValue
				<< ","
				<< result.value
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

MosWorker::MosWorker()
{
	itsMosDB = std::unique_ptr<MosDB> (MosDBPool::Instance()->GetConnection());
}

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
		std::cerr << "No weights for step " << step << std::endl;
		return false;
	}
	
	Weights otherWeights;
	
	if (mosInfo.sineWaveTransition)
	{
		double xpos = XPosition(mosInfo, weights.begin()->second);

		otherWeights = itsMosDB->GetWeights(mosInfo, step, xpos);
		
		if (otherWeights.empty())
		{
			std::cerr << "No weights for season outside current, not adjusting weights" << std::endl;
		}
		else
		{
			weights = AdjustWeights(mosInfo, weights, otherWeights, xpos);
		}
	}
	
	// 2. Get raw forecasts

	std::cout << "Fetching source data for step " << step << std::endl;

	for(auto& it : weights)
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

			double value;

			if (otherWeights.size() == 0 && it.second.weights[i] == 0)
			{
				value = 0;
			}
			else
			{

				value = 1;
			
				if (pl.paramName != "INTERCEPT-N")
				{
					value = itsMosInterpolator.GetValue(mosInfo, station, pl, step);
				}
			}

			if (value == kFloatMissing)
			{
				std::cout << "Missing value for station " << station.id << " " << station.name << " " << Key(pl, step) << std::endl;
				weights.erase(station);
				break;
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

	for(const auto& it : weights)
	{
		Station station = it.first;
	
		Result r;
	
#ifdef DEBUG
		//if (station.id==1024)
		std::cout 	//<< it.second.params << "\n"
				<< it.second.values << "\n"
				<< it.second.weights << "\n"
				<< boost::numeric::ublas::inner_prod(it.second.values, it.second.weights) << std::endl;
#endif
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

