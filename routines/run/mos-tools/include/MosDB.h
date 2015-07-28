#pragma once

#include <NFmiPostgreSQL.h>
#include "MosInfo.h"
#include <mutex>
#include "Factor.h"
#include "Result.h"

class MosDB : public NFmiPostgreSQL
{
public:
    MosDB();
	MosDB(int theId);
	~MosDB();

	MosInfo GetMosInfo(const std::string& mosLabel);
	Weights GetWeights(const MosInfo& mosInfo, int step, double relativity = 0.5);
	void WriteTrace(const MosInfo& mosInfo, const Result& result, const Station& station, const std::string& run_time);

};

class MosDBPool
{
public:
	static MosDBPool* Instance();
	~MosDBPool();

	MosDB* GetConnection();
	void Release(MosDB *theWorker);

private:
	MosDBPool();

	static MosDBPool* itsInstance;
	
	int itsMaxWorkers;
	
	std::vector<int> itsWorkingList;
	std::vector<MosDB*> itsWorkerList;

	std::mutex itsGetMutex;
	std::mutex itsReleaseMutex;
};