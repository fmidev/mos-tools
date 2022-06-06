#pragma once
#include <string>

struct MosInfo
{
	std::string label;
	std::string paramName;  // target parameter
	std::string levelName;
	std::string originTime;  // forecast analysis time

	int networkId;
	int stationId;
	int id;
	int producerId;

	bool traceOutput;
};
