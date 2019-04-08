#pragma once
#include <vector>
#include <map>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/numeric/ublas/vector.hpp>
#include "boost/date_time/posix_time/posix_time.hpp"

struct ParamLevel
{
	std::string paramName;
	std::string levelName;
	double levelValue;
	int stepAdjustment;
	int originTimeAdjustment;
	
	ParamLevel() 
		: paramName("TEST")
		, levelName("TEST")
		, levelValue(32700.)
		, stepAdjustment(0)
	        , originTimeAdjustment(0)
	{};
	
	ParamLevel(const std::string& str)
	{
		std::vector<std::string> split;
		boost::split(split, str, boost::is_any_of("/"));
		
		paramName = split[0];
		levelName = split[1];
		levelValue = std::stod(split[2]);
		stepAdjustment = 0;
		originTimeAdjustment = 0;
		
		if (split.size() >= 4 && split[3].size() > 0)
		{
			stepAdjustment = std::stoi(split[3]);
			
			if (stepAdjustment > 0)
			{
				throw std::runtime_error("No support for next time step data");
			}
		}
		if (split.size() == 5 && split[4].size() > 0)
		{
			originTimeAdjustment = std::stoi(split[4]);

			if (stepAdjustment != -1 && stepAdjustment != 0)
			{
				throw std::runtime_error("No support for other analysis time adjustment but 0 or -1");
			}
		}
	}
	
};

inline
std::ostream& operator<<(std::ostream& file, const ParamLevel& ob)
{
	file << ob.paramName << "/" << ob.levelName << "/" << ob.levelValue;

	if (ob.stepAdjustment != 0) file << "/" << ob.stepAdjustment;
	if (ob.originTimeAdjustment != 0) file << "/" << ob.originTimeAdjustment;

	return file;
}

struct Weight
{
	std::vector<ParamLevel> params;
	boost::numeric::ublas::vector<double> weights;
	boost::numeric::ublas::vector<double> values;
	
	int periodId;	
	int step;

	std::string startDate;
	std::string stopDate;
	std::string mosLabel;
	
};

struct Station
{
	int id;
	int wmoId;
	std::string name;
	
	double latitude;
	double longitude;

	bool operator==(const Station& other) const { return wmoId == other.wmoId; }
	bool operator<(const Station& other) const { return wmoId < other.wmoId; }

};

typedef std::map<Station, Weight> Weights;

inline
std::string Key(const ParamLevel& pl, int step, const std::string& originTime) 
{
	using namespace boost::posix_time;

	auto realOrigin = originTime;

	step = (step < 150) ? step + pl.stepAdjustment * 3 : step + pl.stepAdjustment * 6;

	if (pl.originTimeAdjustment == -1)
	{
		ptime time(time_from_string(originTime));
		time = time - hours(12);
	
		realOrigin = to_simple_string(time);
	}
	
	return pl.paramName
			+ "/"
			+ pl.levelName
			+ "/"
			+ std::to_string (pl.levelValue)
			+ "@" 
			+ std::to_string (step)
			+ " from "
			+ realOrigin; 
}
