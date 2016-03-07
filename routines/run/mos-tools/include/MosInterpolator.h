#pragma once

#include "MosInfo.h"
#include "Factor.h"
#include "Result.h"
#include <NFmiFastQueryInfo.h>
#include "NFmiNeonsDB.h"

#include <map>

typedef std::pair<std::shared_ptr<NFmiQueryData>, NFmiFastQueryInfo> datas;

class MosInterpolator
{
public:
	MosInterpolator();
	~MosInterpolator();
	
	double GetValue(const MosInfo& mosInfo, const Station& station, const ParamLevel& pl, int step);

private:
	std::vector<datas> GetData(const MosInfo& mosInfo, const ParamLevel& pl, int step);	

	std::map<std::string, std::vector<datas>> itsDatas;
	std::unique_ptr<NFmiNeonsDB> itsNeonsDB;

};

