#pragma once
#include <vector>
#include <map>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/numeric/ublas/vector.hpp>

struct ParamLevel
{
	std::string paramName;
	std::string levelName;
	double levelValue;
	int stepAdjustment;
	
	ParamLevel() 
		: paramName("TEST")
		, levelName("TEST")
		, levelValue(32700.)
		, stepAdjustment(0)
	{};
	
	ParamLevel(const std::string& str)
	{
		std::vector<std::string> split;
		boost::split(split, str, boost::is_any_of("/"));
		
		paramName = split[0];
		levelName = split[1];
		levelValue = boost::lexical_cast<double> (split[2]);
		stepAdjustment = 0;
		
		if (split.size() > 3 && split[3].size() > 0)
		{
			stepAdjustment = boost::lexical_cast<int> (split[3]);
			
			if (stepAdjustment > 0)
			{
				throw std::runtime_error("No support for next time step data");
			}
		}
	}
	
};

inline
std::ostream& operator<<(std::ostream& file, const ParamLevel& ob)
{
	file << ob.paramName << "/" << ob.levelName << "/" << ob.levelValue;

	if (ob.stepAdjustment != 0) file << "/" << ob.stepAdjustment;

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
std::string Key(const ParamLevel& pl, int step) 
{
	step = (step < 150) ? step - pl.stepAdjustment * 3 : step - pl.stepAdjustment * 6;
	
	return pl.paramName
			+ "/"
			+ pl.levelName
			+ "/"
			+ boost::lexical_cast<std::string> (pl.levelValue)
			+ "@" 
			+ boost::lexical_cast<std::string> (step); 
}