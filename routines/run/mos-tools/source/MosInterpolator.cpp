#include "MosInterpolator.h"
#include "NFmiGrib.h"
#include <NFmiLatLonArea.h>
#include <NFmiTimeList.h>
#include <NFmiMetTime.h>
#include <NFmiQueryData.h>
#include <NFmiQueryDataUtil.h>
#include <NFmiStreamQueryData.h>

extern boost::posix_time::ptime ToPtime(const std::string& time, const std::string& timeMask);

datas InterpolateToGrid(NFmiFastQueryInfo& sourceInfo, double distanceBetweenGridPointsInDegrees);
datas ToQueryInfo(const ParamLevel& pl, int step, const std::string& fileName);
double Declination(int step, const std::string& originTime);

MosInterpolator::MosInterpolator()
{
	itsNeonsDB = std::unique_ptr<NFmiNeonsDB> (NFmiNeonsDBPool::Instance()->GetConnection());
}

MosInterpolator::~MosInterpolator()
{
	if (itsNeonsDB)
	{
		NFmiNeonsDBPool::Instance()->Release(itsNeonsDB.get());
	}
}

double MosInterpolator::GetValue(const MosInfo& mosInfo, const Station& station, const ParamLevel& pl, int step)
{
	auto key = Key(pl, step);

	assert(step >= 3);

	if (pl.paramName == "DECLINATION")
	{
		return Declination(step, mosInfo.originTime);
	}
	
	NFmiPoint latlon(station.longitude, station.latitude);
	
	bool isCumulativeParameter =
		(pl.paramName == "EVAP-KGM2" || pl.paramName == "RUNOFF-M" || pl.paramName == "SUBRUNOFF-M");
	
	bool isCumulativeRadiationParameter = 
		(pl.paramName == "FLSEN-JM2" || pl.paramName == "FLLAT-JM2"  || pl.paramName == "RNETSW-WM2"  || pl.paramName == "RADDIRSOLAR-JM2");

	int prevStep = -1;
	
	if (itsDatas.find(key) == itsDatas.end())
	{
		// Intentionally not catching exceptions here: if error
		// occurs, program execution should stop
		
		itsDatas[Key(pl,step)] = GetData(mosInfo, pl, step);
		
		if (isCumulativeParameter || isCumulativeRadiationParameter)
		{
			prevStep = step - 3;

			if (step > 144) prevStep = step - 6;

			if (prevStep != 0)
			{
				itsDatas[Key(pl,prevStep)] = GetData(mosInfo, pl, prevStep);
			}
		}
	}
	
	double value = itsDatas[key].second.InterpolatedValue(latlon);
	assert(value == value);

	if (isCumulativeParameter || isCumulativeRadiationParameter)
	{
		double prevValue = (prevStep == 0) ? 0 : itsDatas[Key(pl, prevStep)].second.InterpolatedValue(latlon);
	
		if (value != kFloatMissing && prevValue != kFloatMissing)
		{
			value -= prevValue;
			
			if (isCumulativeRadiationParameter)
			{
				value /= ((step-prevStep)*3600);
			}
		}
	}
	
	return value;
}

datas MosInterpolator::GetData(const MosInfo& mosInfo, const ParamLevel& pl, int step)
{
	int producerId = mosInfo.producerId;
	std::string levelName = pl.levelName;
	std::string paramName = pl.paramName;

	// Perform parameter transformation, to make sure we get the same data
	// mos was used to train
	
	// Radiation is used as power (W/m2)

	if (paramName == "RNETLW-WM2")
	{
		producerId = 240;
		levelName = "HEIGHT";
	}
	else if (paramName == "RADGLO-WM2")
	{
		producerId = 240;
		levelName = "HEIGHT";
	}	
	else if (paramName == "RADLW-WM2")
	{
		producerId = 240;
		levelName = "HEIGHT";
	}
	
	// Precipitation is also *not* cumulative from the forecast start
	
	else if (paramName == "RR-KGM2")
	{
		producerId = 240;
		levelName = "HEIGHT";
		paramName = "RRR-KGM2";
	}
	else if (paramName == "RRC-KGM2")
	{
		producerId = 240;
		levelName = "HEIGHT";
		paramName = "RRRC-KGM2";
	}
	else if (paramName == "RRL-KGM2")
	{
		producerId = 240;
		levelName = "HEIGHT";
		paramName = "RRRL-KGM2";
	}
	
	// Erroneus metadata in neons
	
	else if (paramName == "TD-K")
	{
		paramName = "TD-C";
	}
	
	// Meansea pressure is at level GROUND in neons (surface (station) pressure is PGR-PA)
	
	else if (paramName == "P-PA" && levelName == "MEANSEA")
	{
		levelName = "GROUND";
	}
			
	// T, T925 and T950 can have previous timestep values

	if (pl.stepAdjustment < 0)
	{
		if (step <= 3)
		{
			throw std::runtime_error("Previous timestep data requested for time step 0 or 3");
		}

		if (step < 144)
		{
			step += 3 * pl.stepAdjustment;
		}
		else
		{
			step += 6 * pl.stepAdjustment;
		}
	}
	
	auto prodInfo = itsNeonsDB->GetProducerDefinition(producerId);
	
	assert(prodInfo.size());

	auto gridgeoms = itsNeonsDB->GetGridGeoms(prodInfo["ref_prod"], mosInfo.originTime);
	
	assert(gridgeoms.size());

	std::string tableName = "default";
	
	for(const auto& geom : gridgeoms)
	{
		if (pl.paramName == "FFG-MS")
		{
			// Gust needs to be taken from this geometry
			// since the global one does not have FG10_3 but FG10_1
			if (geom[0] == "ECEUR0125") tableName = geom[1];
		}
		else
		{
			if (geom[0] == "ECGLO0125") tableName = geom[1];
		}
	}

	std::stringstream query;

	query << "SELECT parm_name, lvl_type, lvl1_lvl2, fcst_per, file_location "
			   "FROM " << tableName << " "
			   "WHERE parm_name = upper('" << paramName << "') "
			   "AND lvl_type = upper('" << levelName << "') "
			   "AND lvl1_lvl2 = " << pl.levelValue << " "
			   "AND fcst_per = " << step << " "
			   "ORDER BY dset_id, fcst_per, lvl_type, lvl1_lvl2";

	itsNeonsDB->Query(query.str());

	auto row = itsNeonsDB->FetchRow();

	if (row.empty())
	{
		std::cerr << "No data found for " << producerId << "/" << paramName << "/" << levelName << "/" << pl.levelValue << " step " << step << std::endl;
		throw 1;
	}

	try
	{
		return ToQueryInfo(pl, step, row[4]);
	}
	catch (...)
	{
		std::cerr << "Reading file '" << row[4] << "' failed" << std::endl;
		throw 1;
	}
}

datas ToQueryInfo(const ParamLevel& pl, int step, const std::string& fileName)
{

	NFmiGrib reader;

	if (!reader.Open(fileName))
	{
		throw 1;
	}

	std::cout << "Reading file '" << fileName << "' for parameter " << pl << std::endl;
	
	reader.NextMessage();
	
	long dataDate = reader.Message().DataDate();
	long dataTime = reader.Message().DataTime();

	NFmiTimeList tlist;
	
	tlist.Add(new NFmiMetTime(boost::lexical_cast<long> (dataDate), boost::lexical_cast<long> (dataTime)));

	NFmiTimeDescriptor tdesc(tlist.FirstTime(), tlist);
	
	NFmiParamBag pbag;

	pbag.Add(NFmiDataIdent(NFmiParam(1, "ASDF")));

	NFmiParamDescriptor pdesc(pbag);
	
	NFmiLevelBag lbag(kFmiAnyLevelType,0,0,0);
  
	NFmiVPlaceDescriptor vdesc(lbag);

	long gridType = reader.Message().GridType();
	
	if (gridType != 0)
	{
		throw std::runtime_error("Supporting only latlon areas");
	}

	long ni = reader.Message().SizeX();
	long nj = reader.Message().SizeY();
			
	double fx = reader.Message().X0();
	double fy = reader.Message().Y0();

	double lx = reader.Message().X1();
	double ly = reader.Message().Y1();

	bool jpos = reader.Message().JScansPositively();

	NFmiPoint bl, tr;

	bl.X(fx);
	tr.X(lx+360); // TODO FIX THIS

	bl.Y(fy);
	tr.Y(ly);

	double* ddata = reader.Message().Values();

	if (!jpos)
	{
		bl.Y(ly);
		tr.Y(fy);

		size_t halfSize = static_cast<size_t> (floor(nj/2));

		for (size_t y = 0; y < halfSize; y++)
		{
			for (size_t x = 0; x < static_cast<size_t> (ni); x++)
			{
				size_t ui = y * ni + x;
				size_t li = (nj-1-y) * ni + x;
				double upper = ddata[ui];
				double lower = ddata[li];

				ddata[ui] = lower;
				ddata[li] = upper;
			}
		}
	}

	NFmiArea* area = new NFmiLatLonArea(bl, tr);

	NFmiGrid grid (area, ni, nj , kBottomLeft, kLinearly);
	
	NFmiHPlaceDescriptor hdesc(grid);
	
	NFmiFastQueryInfo qi(pdesc, tdesc, hdesc, vdesc);
	
	auto data = std::shared_ptr<NFmiQueryData> (NFmiQueryDataUtil::CreateEmptyData(qi));
	
	NFmiFastQueryInfo info (data.get());
	info.First();
	
	int num = 0;
	for (info.ResetLocation(); info.NextLocation(); ++num)
	{
		info.FloatValue(static_cast<float> (ddata[num]));
	}

	delete area;
	free(ddata);
	
	double dx = reader.Message().iDirectionIncrement();
	double dy = reader.Message().jDirectionIncrement();

	double wantedGridResolution = 0.125;
	
	if (wantedGridResolution < dx)
	{
		throw std::runtime_error("Will not interpolate to a finer grid than the source data");
	}

#ifdef EXTRADEBUG
	NFmiStreamQueryData streamData;
	assert(streamData.WriteData(pl.paramName + "_" + pl.levelName + "_" + boost::lexical_cast<std::string> (pl.levelValue) + "_" + boost::lexical_cast<std::string> (step) + ".fqd", data.get()));
#endif	

	if (dx != wantedGridResolution || dy != wantedGridResolution)
	{
		std::cout << "Interpolating " << pl << " to a grid with " << wantedGridResolution << " degrees separation" << std::endl;
		
		auto ret = InterpolateToGrid(info, wantedGridResolution);
		
#ifdef EXTRADEBUG
		assert(streamData.WriteData(pl.paramName + "_" + pl.levelName + "_" + boost::lexical_cast<std::string> (pl.levelValue) + "_" + boost::lexical_cast<std::string> (step) + "_" + boost::lexical_cast<std::string> (wantedGridResolution) + ".fqd", ret.first.get()));
#endif
		return ret;
	}

	assert(dx <= wantedGridResolution);
	assert(dy <= wantedGridResolution);

	return std::make_pair(data,info);

}

datas InterpolateToGrid(NFmiFastQueryInfo& sourceInfo, double distanceBetweenGridPointsInDegrees)
{

	auto bl = sourceInfo.Area()->BottomLeftLatLon();
	auto tr = sourceInfo.Area()->TopRightLatLon();
	
	assert(tr.X() > bl.X());
	assert(tr.Y() > bl.Y());
	
	int ni = static_cast<int> (fabs(tr.X() - bl.X()) / distanceBetweenGridPointsInDegrees);
	int nj = static_cast<int> (fabs(tr.Y() - bl.Y()) / distanceBetweenGridPointsInDegrees);

	NFmiGrid grid (sourceInfo.Area(), ni, nj , kBottomLeft, kLinearly);

	NFmiHPlaceDescriptor hdesc(grid);
	
	NFmiFastQueryInfo qi(sourceInfo.ParamDescriptor(), sourceInfo.TimeDescriptor(), hdesc, sourceInfo.VPlaceDescriptor());
	
	auto data = std::shared_ptr<NFmiQueryData> (NFmiQueryDataUtil::CreateEmptyData(qi));

	NFmiFastQueryInfo info (data.get());
	info.First();
	
	for (info.ResetLocation(); info.NextLocation(); )
	{
		info.FloatValue(sourceInfo.InterpolatedValue(info.LatLon()));
		assert(info.FloatValue() != kFloatMissing);
	}

	return std::make_pair(data, info);
}

double Declination(int step, const std::string& originTime)
{
	auto orig = ToPtime(originTime, "%Y%m%d%H%M");
	orig += boost::posix_time::seconds(3600 * step);
	
	tm orig_tm = to_tm(orig);

	// Formula from Jussi Ylhaisi
	
	const int hour_of_day = orig_tm.tm_hour;

	// Sekä deklinaation että länpötilan vuosisyklin jaksonaika on tasan yksi vuosi, mutta näillä 
	// aalloilla on vaihe-ero: Lämpötilan vuosisykli on hieman perässä. Esim. aurinko on on korkeimmillaan 
	// juhannuksena kesäkuun lopulla, mutta silti heinäkuu on ilmastollisesti lämpimin kuukausi.
	// Keskimääräiseksi vaihe-eroksi talvi-kesäkausilla tulee kuitenkin n. 32 päivää, tällä saadaan aallot synkkaan. 
	// Tämä on ihan ok oletus kaikkina vuorokaudenaikoina

	double daydoy = orig_tm.tm_yday + hour_of_day / 24. - 32.;
	
	if (daydoy < 0) daydoy += 365.;

	// 1) Lasketaan auringon deklinaatio (maan akselin ja maan kiertorataa kohtisuoran viivan välinen kulma)
	
	// Tässä daydoy on vuoden päivämäärä 0...365/366 vuoden alusta lukien. Tunnit luetaan tähän mukaan, 
	// eli esim. ajanhetkelle 2.1. klo 15 daydoy=1.625. Huom. päivä ei siis ala indeksistä 1, vaan 0! 
	// Ylläolevat laskut antavat ulos asteina deklinaation.

	const double declination = -asin(0.39779 * cos(0.98565 * ((daydoy + 10) + 1.914 * sin(0.98565 * (daydoy - 2)))));

	return declination;
	
}