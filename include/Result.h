#pragma once
#include <vector>

struct Result
{
	int step;
	double value;
	
	Weight weights;
};

typedef std::map<Station, Result> Results;
