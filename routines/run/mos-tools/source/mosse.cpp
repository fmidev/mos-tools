#include <boost/program_options.hpp>
#include <thread>
#include <mutex>
#include <string>
#include <vector>
#include <iostream>
#include "MosDB.h"
#include "NFmiNeonsDB.h"
#include "MosWorker.h"

std::mutex mut;
static int step = -1;

struct Options
{
	int threadCount;
	int startStep;
	int endStep;
	int stepLength;
	int stationId;

	std::string mosLabel;
	std::string paramName;
	
	bool sineWaveTransition;
	bool trace;

	Options()
		: threadCount(1)
		, startStep(-1)
		, endStep(-1)
		, stepLength(1)
		, stationId(-1)
		, mosLabel("")
		, paramName("")
		, sineWaveTransition(false)
		, trace(false)
	{}
};

static Options opts;

void ParseCommandLine(int argc, char ** argv)
{
	namespace po = boost::program_options;

	po::options_description desc("Allowed options");

	desc.add_options()
	("help,h", "print out help message")
	("mos-label,m", po::value<std::string>(&opts.mosLabel), "mos label (required)")
	("threads,j", po::value(&opts.threadCount), "number of started threads")
	("start-step,s", po::value(&opts.startStep), "start step")
	("end-step,e", po::value(&opts.endStep), "end step")
	("step-length,l", po::value(&opts.stepLength), "step length")
	("station-id,S", po::value(&opts.stationId), "station id, comma separated list")
	("parameter,p", po::value(&opts.paramName), "parameter name (neons-style)")
	("sine", "apply weight transition between periods using sine wave factor (default false)")
	("trace", "write trace information to log and database (default false)")
	;

	po::positional_options_description p;
	p.add("auxiliary-files", -1);

	po::variables_map opt;
	po::store(po::command_line_parser(argc, argv)
			  .options(desc)
			  .positional(p)
			  .run(),
			  opt);

	po::notify(opt);

	if (opt.count("help"))
	{
		std::cout << "usage: mosher [ options ]" << std::endl;
		std::cout << desc;
		std::cout << std::endl << "Examples:" << std::endl;
		std::cout << "  mosse -s 3 -e 6 -l 3 -m MOS_ECMWF_r144 --trace" << std::endl;
		exit(1);
	}

	if (opt.count("sine"))
	{
		opts.sineWaveTransition = true;
	}
	
	if (opt.count("trace"))
	{
		opts.trace = true;
	}

	if (opts.startStep == -1 || opts.endStep == -1)
	{
		std::cerr << "Start and end steps must be specified" << std::endl;
		std::cout << desc;
		exit(1);
	}

	if (opts.mosLabel.empty())
	{
		std::cerr << "Mos label must be specified" << std::endl;
		std::cout << desc;
		exit(1);	
	}

}

bool DistributeWork(int& curstep)
{
    std::lock_guard<std::mutex> lock(mut);

    step += opts.stepLength;

    if (step <= opts.endStep)
    {
    	curstep = step;
    	return true;
    }

    return false;
}

void Run(const MosInfo& mosInfo, int threadId)
{
	printf("Thread %d started\n", threadId);
	MosWorker mosher;

	std::unique_ptr<MosDB> m = std::unique_ptr<MosDB> (MosDBPool::Instance()->GetConnection());
	std::unique_ptr<NFmiNeonsDB> n = std::unique_ptr<NFmiNeonsDB> (NFmiNeonsDBPool::Instance()->GetConnection());

	mosher.itsMosDB = std::move(m);
	mosher.itsNeonsDB = std::move(n);

	int curstep = -1;

	while (DistributeWork(curstep))
	{
		mosher.Mosh(mosInfo, curstep);
	}
}

int main(int argc, char ** argv)
{

	ParseCommandLine(argc, argv);

	step = opts.startStep - opts.stepLength;

	std::unique_ptr<MosDB> m = std::unique_ptr<MosDB> (MosDBPool::Instance()->GetConnection());

	MosInfo mosInfo = m->GetMosInfo(opts.mosLabel);

	mosInfo.paramName = "T-K";

	NFmiNeonsDB::Instance().Instance().Connect(1);

	auto prodinfo = NFmiNeonsDB::Instance().GetProducerDefinition(mosInfo.producerId);
	
	if (prodinfo.empty())
	{
		throw std::runtime_error("Unknown producer: " + boost::lexical_cast<std::string> (mosInfo.producerId));
	}

	std::string ref_prod = prodinfo["ref_prod"];

	NFmiNeonsDB::Instance().Query("SELECT max(base_date) FROM as_grid WHERE model_type = '" + ref_prod + "' and rec_cnt_dset > 0 AND geom_name = 'ECGLO0125' AND to_char(base_date, 'HH24') IN ('00','12')");
	
	auto row = NFmiNeonsDB::Instance().FetchRow();
	
	if (row.empty() || row[0].empty())
	{
		throw std::runtime_error("Data not found from neons for ref_prod " + ref_prod);
	}

	mosInfo.originTime = row[0];
	mosInfo.sineWaveTransition = opts.sineWaveTransition;
	mosInfo.traceOutput = opts.trace;

#ifdef DEBUG
	std::cout << "Analysis time: " << mosInfo.originTime << std::endl;
#endif
	
	std::vector<std::thread> threadGroup;

	NFmiNeonsDBPool::Instance()->MaxWorkers(opts.threadCount+1);

	for (int i = 0; i < opts.threadCount; i++)
	{
		threadGroup.push_back(std::thread(Run, mosInfo, i)); 
	}

	std::for_each(threadGroup.begin(), threadGroup.end(), [](std::thread& t){ t.join(); });

	MosDBPool::Instance()->Release(m.get());
	m.release();

}
