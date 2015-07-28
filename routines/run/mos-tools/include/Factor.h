#pragma once
#include <vector>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/numeric/ublas/vector.hpp>

struct ParamLevel
{
	std::string paramName;
	std::string levelName;
	double levelValue;
	
	ParamLevel() 
		: paramName("TEST")
		, levelName("TEST")
		, levelValue(32700.)
	{};
	
	ParamLevel(const std::string& str)
	{
		std::vector<std::string> split;
		boost::split(split, str, boost::is_any_of("/"));
		
		paramName = split[0];
		levelName = split[1];
		levelValue = boost::lexical_cast<double> (split[2]);
	}
};

struct Weight
{
	std::vector<ParamLevel> params;
	boost::numeric::ublas::vector<double> weights;
	boost::numeric::ublas::vector<double> values;
	
	int periodId;
	//int startMonth;
	//int startDay;
	//int stopMonth;
	//int stopDay;
	
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
