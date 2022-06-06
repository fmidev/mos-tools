#pragma once

#include "MosDB.h"
#include <memory>
#include "Result.h"
#include "MosInterpolator.h"

class MosWorker
{
public:
	MosWorker();
	~MosWorker();
	bool Mosh(const MosInfo& mosInfo, int step);
private:
	void Write(const MosInfo& mosInfo, const Results& result);

	MosInterpolator itsMosInterpolator;
	std::unique_ptr<MosDB> itsMosDB;

};
