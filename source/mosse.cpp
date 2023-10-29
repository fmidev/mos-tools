#include "MosDB.h"
#include "MosWorker.h"
#include "NFmiRadonDB.h"
#include "Options.h"
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/program_options.hpp>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

std::mutex mut;
static std::vector<std::string> params;
std::map<int, std::map<std::string, Weights>> allWeights;

static int step;

Options opts;

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
		("network-id,n", po::value(&opts.networkId), "network id (1=wmo, 5=fmisid, default=1)")
		("parameter,p", po::value(&opts.paramName), "parameter name (radon-style), comma separated list")
		("trace", "write trace information to log and database (default false)")
		("analysis_time,a", po::value(&opts.analysisTime), "specify analysis time (SQL full timestamp, default=latest from database)")
		("disable0125", "disable interpolation to 0.125 degree grid")
		("weights-file", po::value(&opts.weightsFile), "read weights from file")
		("ecmwf-geometry", po::value(&opts.sourceGeom), "source data geometry (default ECGLO0100)")
		("producer-id", po::value(&opts.producerId), "producer id, only when --weights-file is used (default 131)")
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
		std::cout << "  mosse -s 3 -e 6 -l 3 -m MOS_ECMWF_r144 --trace -p T-K" << std::endl;
		std::cout << "  mosse -s 3 -e 6 -l 3 --weights-file weights.csv -m MOS_ECMWF_040422 -p T-K" << std::endl;
		exit(0);
	}

	if (opt.count("trace"))
	{
		opts.trace = true;
	}

	if (opts.weightsFile.empty() == false && opts.trace)
	{
		std::cerr << "Trace option cannot be used with weights file" << std::endl;
		opts.trace = false;
	}

	if (opt.count("disable0125"))
	{
		opts.disable0125 = true;
	}

	if (opts.startStep == -1 || opts.endStep == -1)
	{
		std::cerr << "Start and end steps must be specified" << std::endl;
		std::cerr << desc;
		exit(1);
	}

	if (opts.weightsFile.empty() && opts.mosLabel.empty())
	{
		std::cerr << "Mos label or weights file must be specified" << std::endl;
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

void ReadWeights(const MosInfo& mosInfo, std::istream& in)
{
	auto PeriodIdFromDate = [](const std::string& date)
	{
		std::string atime = date.substr(0, 10);

		int month = std::stoi(date.substr(5, 2));
		int day = std::stoi(date.substr(8, 2));

		if (month == 2 && day == 29)
		{
			// LOL karkauspäivä
			day = 28;
		}

		if (month == 12 || month < 3)
			return 1;
		if (month >= 3 && month < 6)
			return 2;
		if (month >= 6 && month < 9)
			return 3;
		if (month >= 9 && month < 12)
			return 4;

		return 0;  // for compiler
	};

	std::string line, col;

	// yyyy-mm-dd hh:mm:ss
	int atime = std::stoi(mosInfo.originTime.substr(11, 2));
	const int periodId = PeriodIdFromDate(mosInfo.originTime);

	std::vector<int> steps;
	for (int i = opts.startStep; i <= opts.endStep; i += opts.stepLength)
	{
		steps.push_back(i);
	}

	std::cout << std::unitbuf << "Reading weights from file '" << opts.weightsFile << "' ";

	int numlines = 0;
	int numweights = 0;
	while (std::getline(in, line))
	{
		numlines++;
		if (numlines % 100000 == 0)
		{
			std::cout << ".";
		}
		if (line.empty() || line[0] == '#')
		{
			continue;
		}
		std::vector<std::string> cols;
		boost::split(cols, line, boost::is_any_of(","));

		int nperiodId = std::stoi(cols[0]);
		int natime = std::stoi(cols[1]);
		int stationId = std::stoi(cols[2]);
		double lon = std::stod(cols[3]);
		double lat = std::stod(cols[4]);
		int nstep = std::stoi(cols[5]);
		std::string targetParamName = cols[6];

		if (atime != natime || periodId != nperiodId || (opts.stationId != -1 && opts.stationId != stationId) ||
		    (std::find(steps.begin(), steps.end(), nstep) == steps.end()))
		{
			continue;
		}

		numweights++;

		Weight w;
		Station s;
		s.id = stationId;
		s.longitude = lon;
		s.latitude = lat;

		w.weights.resize((cols.size() - 7) / 2, 0);
		w.params.resize(w.weights.size());

		for (size_t i = 7, j = 0; i < cols.size(); i += 2, j++)
		{
			std::string key = cols[i];
			double val = boost::lexical_cast<double>(cols[i + 1]);
			assert(val == val);  // no NaN

			w.weights[j] = val;

			ParamLevel pl(key);
			w.params[j] = pl;
		}

		w.step = nstep;
		w.periodId = periodId;

		assert(w.params.size() == w.weights.size());

		if (!w.params.empty())
		{
			allWeights[nstep][targetParamName][s] = w;
		}
	}

	std::cout << " done. Read " << numlines << " lines and got " << numweights << " weights\n";

	if (numweights == 0)
	{
		exit(1);
	}
}

void ReadWeightsFromFile(const MosInfo& mosInfo)
{
	if (!boost::filesystem::exists(opts.weightsFile))
	{
		std::cerr << "File '" << opts.weightsFile << "' does not exist\n";
		exit(1);
	}

	const auto ext = boost::filesystem::path(opts.weightsFile).extension().string();

	if (ext == ".csv")
	{
		std::ifstream in(opts.weightsFile);
		return ReadWeights(mosInfo, in);
	}
	else if (ext == ".gz")
	{
		try
		{
			std::ifstream file(opts.weightsFile, std::ios_base::in | std::ios_base::binary);
			boost::iostreams::filtering_istream in;
			in.push(boost::iostreams::gzip_decompressor());
			in.push(file);

			return ReadWeights(mosInfo, in);
		}
		catch (const boost::iostreams::gzip_error& e)
		{
			std::cout << e.what() << '\n';
		}
	}
	else
	{
		std::cout << "Unrecognized file extension " << ext << ", should be either .csv or .csv.gz\n";
		exit(1);
	}
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

	std::unique_ptr<MosDB> m;

	MosInfo mosInfo;
	mosInfo.producerId = opts.producerId;

	if (opts.weightsFile.empty())
	{
		m = std::unique_ptr<MosDB>(MosDBPool::Instance()->GetConnection());
		mosInfo = m->GetMosInfo(opts.mosLabel);
	}

	if (opts.analysisTime.empty())
	{
		NFmiRadonDB::Instance().Connect();

		auto prodinfo = NFmiRadonDB::Instance().GetProducerDefinition(mosInfo.producerId);

		if (prodinfo.empty())
		{
			throw std::runtime_error("Unknown producer: " + boost::lexical_cast<std::string>(mosInfo.producerId));
		}

		std::string ref_prod = prodinfo["ref_prod"];

		const auto latest = NFmiRadonDB::Instance().GetLatestTime(ref_prod, opts.sourceGeom, 0);

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

	if (opts.weightsFile.empty() == false)
	{
		ReadWeightsFromFile(mosInfo);
	}

	const std::string ahour = mosInfo.originTime.substr(11, 2);

	if (ahour != "00" && ahour != "12")
	{
		throw std::runtime_error("analysis hour is neither 00 nor 12 (" + mosInfo.originTime + ")");
	}

	mosInfo.traceOutput = opts.trace;
	mosInfo.networkId = opts.networkId;
	mosInfo.stationId = opts.stationId;

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

	if (opts.weightsFile.empty())
	{
		MosDBPool::Instance()->Release(m.get());
		m.release();
	}
}
