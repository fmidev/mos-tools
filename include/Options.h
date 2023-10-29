#pragma once

struct Options
{
	int threadCount;
	int startStep;
	int endStep;
	int stepLength;
	int stationId;
	int networkId;
	int producerId;

	std::string mosLabel;
	std::string paramName;
	std::string analysisTime;
	std::string weightsFile;
	std::string sourceGeom;

	bool trace;
	bool disable0125;

	Options()
	    : threadCount(1),
	      startStep(-1),
	      endStep(-1),
	      stepLength(1),
	      stationId(-1),
	      networkId(1),
	      producerId(131),
	      mosLabel(""),
	      paramName(""),
	      analysisTime(""),
	      weightsFile(""),
	      sourceGeom("ECGLO0100"),
	      trace(false),
	      disable0125(false)
	{
	}
};
