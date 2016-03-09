#include "MosInterpolator.h"
#include "NFmiGrib.h"
#include <NFmiLatLonArea.h>
#include <NFmiRotatedLatLonArea.h>
#include <NFmiTimeList.h>
#include <NFmiMetTime.h>
#include <NFmiQueryData.h>
#include <NFmiQueryDataUtil.h>
#include <NFmiStreamQueryData.h>

extern boost::posix_time::ptime ToPtime(const std::string& time, const std::string& timeMask);

datas InterpolateToGrid(NFmiFastQueryInfo& sourceInfo, double distanceBetweenGridPointsInDegrees);
datas ToQueryInfo(const ParamLevel& pl, int step, const std::string& fileName);
double Declination(int step, const std::string& originTime);
FmiInterpolationMethod InterpolationMethod(const std::string& paramName);

const double PI = 3.14159265359;

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

	if (pl.paramName == "DECLINATION-N")
	{
		return Declination(step, mosInfo.originTime);
	}
	
	NFmiPoint latlon(station.longitude, station.latitude);

	// These are cumulative parameters 
	bool isCumulativeParameter =
		(pl.paramName == "EVAP-KGM2" || pl.paramName == "RUNOFF-M" || pl.paramName == "SUBRUNOFF-M");

	// These are cumulative radiation parameters 
	bool isCumulativeRadiationParameter = 
		(pl.paramName == "FLSEN-JM2" || pl.paramName == "FLLAT-JM2" || pl.paramName == "RNETSW-WM2" || pl.paramName == "RNETLW-WM2" || pl.paramName == "RADDIRSOLAR-JM2" || pl.paramName == "RADLW-WM2" || pl.paramName == "RADGLO-WM2");

	int prevStep = (step > 144) ? step - 6 : step - 3;
	
	assert(prevStep >= 0 && prevStep <= 234);

	if (itsDatas.find(key) == itsDatas.end())
	{
		// Intentionally not catching exceptions here: if error
		// occurs, program execution should stop
		
		itsDatas[Key(pl,step)] = GetData(mosInfo, pl, step);
		
		if ((isCumulativeParameter || isCumulativeRadiationParameter) && prevStep > 0)
		{
			itsDatas[Key(pl,prevStep)] = GetData(mosInfo, pl, prevStep);
		}
	}

	double value = kFloatMissing;

	double scale = 1, base = 0;

	if (pl.paramName == "POTVORT-N" || pl.paramName == "ABSVO-HZ")
	{
		scale = 1000000;
	}
	else if (pl.paramName == "SD-M" || pl.paramName == "EVAP-KGM2" || pl.paramName == "RUNOFF-M" || pl.paramName == "SUBRUNOFF-M" )
	{
		scale = 1000;
	}
	else if (pl.paramName == "ALBEDO-PRCNT" || pl.paramName == "IC-0TO1" || pl.paramName == "LC-0TO1" )
	{
		scale = 100;
	}

	for (datas& d : itsDatas[key])
	{
		value = d.second.InterpolatedValue(latlon);
		assert(value == value);

		if (value == kFloatMissing) continue ; // Try another geometry (if exists)

		if (isCumulativeParameter || isCumulativeRadiationParameter)
		{
			double prevValue = kFloatMissing;

			// analysis hour value = 0
			if (prevStep == 0)
			{
				prevValue = 0;
			}
			else
			{
				for (datas& d2 : itsDatas[Key(pl, prevStep)])
				{
					prevValue = d2.second.InterpolatedValue(latlon);
					if (prevValue == kFloatMissing)
					{
						continue;
					}
					else 
					{
						break;
					}
				}
			}

			assert(prevValue != kFloatMissing);
	
			if (value != kFloatMissing && prevValue != kFloatMissing)
			{
				value -= prevValue;

				if (isCumulativeRadiationParameter)
				{
					value /= (step-prevStep)*3600;
				}
			}
			else
			{
				value = kFloatMissing;
			}
		}

		break;
	}

	if (value != kFloatMissing)
	{
		value = value * scale + base;
	}

	return value;
}

std::vector<datas> MosInterpolator::GetData(const MosInfo& mosInfo, const ParamLevel& pl, int step)
{
	int producerId = mosInfo.producerId;
	std::string levelName = pl.levelName;
	std::string paramName = pl.paramName;

	// Perform parameter transformation, to make sure we get the same data
	// mos was used to train
	
	// Radiation is used as power (W/m2)

/*	if (paramName == "RNETLW-WM2")
	{
		producerId = 240;
		levelName = "HEIGHT";
	}
	else 
	if (paramName == "RADGLO-WM2")
	{
		producerId = 240;
		levelName = "HEIGHT";
	}	
	else if (paramName == "RADLW-WM2")
	{
		producerId = 240;
		levelName = "HEIGHT";
	}
*/	
	// Precipitation is also *not* cumulative from the forecast start,
	// it is transformed to one hour precipitation

	if (paramName == "RR-KGM2")
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
#ifdef DEBUG
		std::cout << "Param " << pl.paramName << "/" << pl.levelName << "/" << pl.levelValue << " at step " << step << " has step adjustment " << pl.stepAdjustment << std::endl;
#endif

		if (step < 3)
		{
			throw std::runtime_error("Previous timestep data requested for time step 0");
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

	if (gridgeoms.size() > 1)
	{
		// order so that GLO is first
		std::vector<std::vector<std::string>> newgeoms(1);

		for (const auto& geom : gridgeoms)
		{
			std::string geomName = geom[0];
			if (geomName == "ECGLO0125" || geomName == "ECGLO0100")
			{
				newgeoms[0] = geom;
			}
			else
			{
				newgeoms.push_back(geom);
			}
		}
		gridgeoms = newgeoms;
	}

	std::vector<datas> ret;

	for(const auto& geom : gridgeoms)
	{
	
		std::string tableName = geom[1];

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
			continue;
		}

		ret.push_back(ToQueryInfo(pl, step, row[4]));
		break;
	}

	if (ret.empty())
	{
		throw std::runtime_error("No data found for " + boost::lexical_cast<std::string> (producerId) + "/" + Key(pl, step));
	}

	return ret;
}

datas ToQueryInfo(const ParamLevel& pl, int step, const std::string& fileName)
{

	NFmiGrib reader;

	if (!reader.Open(fileName))
	{
		std::cerr << "File open failed" << std::endl;
		throw 1;
	}

	std::cout << "Reading file '" << fileName << "'" << std::endl;
	reader.NextMessage();
	
	long dataDate = reader.Message().DataDate();
	long dataTime = reader.Message().DataTime();

	NFmiTimeList tlist;
	
	tlist.Add(new NFmiMetTime(boost::lexical_cast<long> (dataDate), boost::lexical_cast<long> (dataTime)));

	NFmiTimeDescriptor tdesc(tlist.FirstTime(), tlist);
	
	NFmiParamBag pbag;
	NFmiParam p(1, pl.paramName);
	p.InterpolationMethod(InterpolationMethod(pl.paramName));
	
	pbag.Add(NFmiDataIdent(p));

	NFmiParamDescriptor pdesc(pbag);
	
	NFmiLevelBag lbag(kFmiAnyLevelType,0,0,0);
  
	NFmiVPlaceDescriptor vdesc(lbag);

	long gridType = reader.Message().GridType();
	
	if (gridType != 0 && gridType != 10)
	{
		throw std::runtime_error("Invalid area");
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
	tr.X(lx);

	if (lx == -0.125)
	{
		tr.X(lx+360);
	}

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
	
	NFmiArea* area = 0;

	if (gridType == 0)
	{
		area = new NFmiLatLonArea(bl, tr);
	}
	else if (gridType == 10)
	{
		double spx = reader.Message().SouthPoleX();
		double spy = reader.Message().SouthPoleY();

		area = new NFmiRotatedLatLonArea(bl, tr, NFmiPoint(spx, spy), NFmiPoint(0, 0), NFmiPoint(1, 1), true);
	}

	NFmiGrid grid (area, ni, nj, kBottomLeft, InterpolationMethod(pl.paramName));
	
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

	NFmiGrid grid (sourceInfo.Area(), ni, nj , kBottomLeft, sourceInfo.Grid()->InterpolationMethod());

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

	const double declination = -asin(0.39779 * cos(0.98565 / 360 * 2 * PI * ((daydoy + 10) + 1.914 / 360 * 2 * PI * sin(0.98565 / 360 * 2 * PI * (daydoy - 2))))) * 360 / 2 / PI;

	return declination;
	
}

FmiInterpolationMethod InterpolationMethod(const std::string& paramName)
{
	FmiInterpolationMethod method = kLinearly;
	
	if (paramName == "RR-KGM2" || paramName == "RRL-KGM2" || paramName == "RRC-KGM2")
	{
		method = kNearestPoint;
	}
	
	return method;
}
