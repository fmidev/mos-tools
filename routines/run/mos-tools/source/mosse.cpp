#include <boost/program_options.hpp>
#include <thread>
#include <mutex>
#include <string>
#include <vector>
#include <iostream>
#include "MosDB.h"
#include "NFmiRadonDB.h"
#include "MosWorker.h"

std::mutex mut;
static std::vector<std::string> params;
static int step;

struct Options
{
	int threadCount;
	int startStep;
	int endStep;
	int stepLength;
	int stationId;

	std::string mosLabel;
	std::string paramName;
	std::string analysisTime;

	bool trace;

	Options()
	    : threadCount(1),
	      startStep(-1),
	      endStep(-1),
	      stepLength(1),
	      stationId(-1),
	      mosLabel(""),
	      paramName(""),
	      analysisTime(""),
	      trace(false)
	{
	}
};

static Options opts;

void ParseCommandLine(int argc, char** argv)
{
	namespace po = boost::program_options;

	po::options_description desc("Allowed options");

	// clang-format off
	desc.add_options()
		("help,h", "print out help message")
		("mos-label,m", po::value<std::string>(&opts.mosLabel), "mos label (required)")
		("threads,j", po::value(&opts.threadCount), "number of started threads")
		("start-step,s", po::value(&opts.startStep), "start step")
		("end-step,e", po::value(&opts.endStep), "end step")
		("step-length,l", po::value(&opts.stepLength), "step length")
		("station-id,S", po::value(&opts.stationId), "station id, comma separated list")
		("parameter,p", po::value(&opts.paramName), "parameter name (radon-style), comma separated list")
		("trace", "write trace information to log and database (default false)")
		("analysis_time,a", po::value(&opts.analysisTime), "specify analysis time (SQL full timestamp, default=latest from database)")
		;
	// clang-format on

	po::positional_options_description p;
	p.add("auxiliary-files", -1);

	po::variables_map opt;
	po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), opt);

	po::notify(opt);

	if (opt.count("help"))
	{
		std::cout << "usage: mosse [ options ]" << std::endl;
		std::cout << desc;
		std::cout << std::endl << "Examples:" << std::endl;
		std::cout << "  mosse -s 3 -e 6 -l 3 -m MOS_ECMWF_r144 --trace" << std::endl;
		exit(1);
	}

	if (opt.count("trace"))
	{
		opts.trace = true;
	}

	if (opts.startStep == -1 || opts.endStep == -1)
	{
		std::cerr << "Start and end steps must be specified" << std::endl;
		std::cerr << desc;
		exit(1);
	}

	if (opts.mosLabel.empty())
	{
		std::cerr << "Mos label must be specified" << std::endl;
		std::cerr << desc;
		exit(1);
	}

	if (opts.paramName.empty())
	{
		std::cerr << "Parameter name must be specified" << std::endl;
		std::cerr << desc;
		exit(1);
	}
}

bool DistributeWork(int& curstep)
{
	std::lock_guard<std::mutex> lock(mut);

	if (step <= opts.endStep)
	{
		curstep = step;
		step += opts.stepLength;
		return true;
	}

	return false;
}

void Run(MosInfo mosInfo, int threadId)
{
	printf("Thread %d started\n", threadId);

	MosWorker mosher;

	int curstep = -1;  // Will change at DistributeWork

	while (DistributeWork(curstep))
	{
		for (const auto& p : params)
		{
			mosInfo.paramName = p;
			printf("Thread %d processing param %s step %d\n", threadId, mosInfo.paramName.c_str(), curstep);
			mosher.Mosh(mosInfo, curstep);
		}
	}

	printf("Thread %d stopped\n", threadId);
}

int main(int argc, char** argv)
{
	ParseCommandLine(argc, argv);

	std::unique_ptr<MosDB> m = std::unique_ptr<MosDB>(MosDBPool::Instance()->GetConnection());

	MosInfo mosInfo = m->GetMosInfo(opts.mosLabel);

	if (opts.analysisTime.empty())
	{
		NFmiRadonDB::Instance().Connect();

		auto prodinfo = NFmiRadonDB::Instance().GetProducerDefinition(mosInfo.producerId);

		if (prodinfo.empty())
		{
			throw std::runtime_error("Unknown producer: " + boost::lexical_cast<std::string>(mosInfo.producerId));
		}

		std::string ref_prod = prodinfo["ref_prod"];

		const auto latest = NFmiRadonDB::Instance().GetLatestTime(ref_prod, "ECGLO0100", 0);

		if (latest.empty())
		{
			throw std::runtime_error("Data not found from radon for ref_prod " + ref_prod);
		}

		mosInfo.originTime = latest;
	}
	else
	{
		mosInfo.originTime = opts.analysisTime;
	}

	mosInfo.traceOutput = opts.trace;

#ifdef DEBUG
	std::cout << "Analysis time: " << mosInfo.originTime << std::endl;
#endif

	NFmiRadonDBPool::Instance()->MaxWorkers(opts.threadCount + 1);

	boost::split(params, opts.paramName, boost::is_any_of(","));

	step = opts.startStep;

	std::vector<std::thread> threadGroup;

	for (int i = 0; i < opts.threadCount; i++)
	{
		threadGroup.push_back(std::thread(Run, mosInfo, i));
	}

	for (auto& t : threadGroup)
	{
		t.join();
	}

	MosDBPool::Instance()->Release(m.get());
	m.release();
}
