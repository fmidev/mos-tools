#pragma once

#include "MosDB.h"
#include "NFmiNeonsDB.h"

#include <memory>
#include <NFmiFastQueryInfo.h>
#include "Result.h"

typedef std::pair<std::shared_ptr<NFmiQueryData>, NFmiFastQueryInfo> datas;

class MosWorker
{
public:
	~MosWorker();
	bool Mosh(const MosInfo& mosInfo, int step);
	std::unique_ptr<MosDB> itsMosDB;
	std::unique_ptr<NFmiNeonsDB> itsNeonsDB;
private:
	bool ToQueryInfo(const MosInfo& mosInfo, const ParamLevel& pl, const std::string& fileName, int step);
	double GetData(const MosInfo& mosInfo, const Station& station, const ParamLevel& pl, int step);
	void Write(const MosInfo& mosInfo, const Results& result);

	std::map<std::string, datas> itsDatas;

};
