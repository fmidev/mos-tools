#pragma once

struct Options
{
	int threadCount;
	int startStep;
	int endStep;
	int stepLength;
	int stationId;
	int networkId;

	std::string mosLabel;
	std::string paramName;
	std::string analysisTime;

	bool trace;
	bool disable0125;

	Options()
	    : threadCount(1),
	      startStep(-1),
	      endStep(-1),
	      stepLength(1),
	      stationId(-1),
	      networkId(1),
	      mosLabel(""),
	      paramName(""),
	      analysisTime(""),
	      trace(false),
	      disable0125(false)
	{
	}
};
