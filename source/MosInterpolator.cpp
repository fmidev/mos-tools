#include "MosInterpolator.h"
#include "NFmiGrib.h"
#include "Options.h"
#include <NFmiLatLonArea.h>
#include <NFmiMetTime.h>
#include <NFmiQueryData.h>
#include <NFmiQueryDataUtil.h>
#include <NFmiRotatedLatLonArea.h>
#include <NFmiStreamQueryData.h>
#include <NFmiTimeList.h>

extern Options opts;
extern boost::posix_time::ptime ToPtime(const std::string& time, const std::string& timeMask);
extern std::string GetEnv(const std::string& username);

datas InterpolateToGrid(NFmiFastQueryInfo& sourceInfo, double distanceBetweenGridPointsInDegrees);
datas ToQueryInfo(const ParamLevel& pl, int step, const std::string& fileName, const std::string& offset,
                  const std::string& length);
double Declination(int step, const std::string& originTime);
FmiInterpolationMethod InterpolationMethod(const std::string& paramName);

const double PI = 3.14159265359;

static std::once_flag oflag;

MosInterpolator::MosInterpolator()
{
	call_once(
	    oflag,
	    [&]()
	    {
		    const auto pw = GetEnv("RADON_RADONCLIENT_PASSWORD");
		    const auto hostname = GetEnv("RADON_HOSTNAME");

		    if (pw.empty())
		    {
			    throw std::runtime_error("Password should be given with env variable 'RADON_RADONCLIENT_PASSWORD'");
		    }

		    if (hostname.empty())
		    {
			    throw std::runtime_error("Hostname should be given with env variable 'RADON_HOSTNAME'");
		    }

		    NFmiRadonDBPool::Instance()->Username("radon_client");
		    NFmiRadonDBPool::Instance()->Password(pw);
		    NFmiRadonDBPool::Instance()->Database("radon");
		    NFmiRadonDBPool::Instance()->Hostname(hostname);
	    });

	itsRadonDB = std::unique_ptr<NFmiRadonDB>(NFmiRadonDBPool::Instance()->GetConnection());
}

MosInterpolator::~MosInterpolator()
{
	// Return connection to pool
	if (itsRadonDB)
	{
		NFmiRadonDBPool::Instance()->Release(itsRadonDB.get());
	}

	// release unique_ptr ownership without calling destructor
	itsRadonDB.release();
}

double MosInterpolator::GetValue(const MosInfo& mosInfo, const Station& station, const ParamLevel& pl, int step)
{
	auto key = Key(pl, step, mosInfo.originTime);

	assert(step >= 0);

	// Special cases

	// Declination is not in database

	if (pl.paramName == "DECLINATION-N")
	{
		return Declination(step, mosInfo.originTime);
	}

	// Following parameters are not defined for step > 144

	if (step > 144 && (pl.paramName == "FFG3H-MS" || pl.paramName == "TMAX3H-K" || pl.paramName == "TMIN3H-K"))
	{
		return kFloatMissing;
	}

	// The factor for T-MEAN-K is zero for leadtimes < 150
	if (step < 150 && pl.paramName == "T-MEAN-K")
	{
		return kFloatMissing;
	}

	NFmiPoint latlon(station.longitude, station.latitude);

	// These are cumulative parameters
	bool isCumulativeParameter =
	    (pl.paramName == "EVAP-KGM2" || pl.paramName == "RUNOFF-M" || pl.paramName == "SUBRUNOFF-M" ||
	     pl.paramName == "RRC-KGM2" || pl.paramName == "RRL-KGM2");

	// These are cumulative radiation parameters
	bool isCumulativeRadiationParameter =
	    (pl.paramName == "FLSEN-JM2" || pl.paramName == "FLLAT-JM2" || pl.paramName == "RNETSW-WM2" ||
	     pl.paramName == "RNETLW-WM2" || pl.paramName == "RADDIRSOLAR-JM2" || pl.paramName == "RADLW-WM2" ||
	     pl.paramName == "RADGLO-WM2");

	int prevStep;

	if (step > 144)
	{
		prevStep = step - 6;
	}
	else if (step > 90)
	{
		prevStep = step - 3;
	}
	else
	{
		prevStep = step - 1;
	}

	if (itsDatas.find(key) == itsDatas.end())
	{
		// Intentionally not catching exceptions here: if error
		// occurs, program execution should stop

		itsDatas[Key(pl, step, mosInfo.originTime)] = GetData(mosInfo, pl, step);

		if ((isCumulativeParameter || isCumulativeRadiationParameter) && prevStep > 0)
		{
			itsDatas[Key(pl, prevStep, mosInfo.originTime)] = GetData(mosInfo, pl, prevStep);
		}
	}

	double value = kFloatMissing;

	double scale = 1, base = 0;

	if (pl.paramName == "POTVORT-N" || pl.paramName == "ABSVO-HZ")
	{
		scale = 1000000;
	}
	else if (pl.paramName == "SD-M" || pl.paramName == "EVAP-KGM2" || pl.paramName == "RUNOFF-M" ||
	         pl.paramName == "SUBRUNOFF-M")
	{
		scale = 1000;
	}
	else if (pl.paramName == "ALBEDO-PRCNT" || pl.paramName == "IC-0TO1" || pl.paramName == "LC-0TO1")
	{
		scale = 100;
	}

	for (datas& d : itsDatas[key])
	{
		value = d.second.InterpolatedValue(latlon);
		assert(value == value);

		if (value == kFloatMissing)
			continue;  // Try another geometry (if exists)

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
				for (datas& d2 : itsDatas[Key(pl, prevStep, mosInfo.originTime)])
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
					value /= ((step - prevStep) * 3600);
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

	// Note! RR, RRC and RRL *need* to be one hour accumulation (source: J.
	// Ylhaisi)!

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

	else if (paramName == "TOTCW-KGM2")
	{
		paramName = "TCW-KGM2";
	}

	// Meansea pressure is at level GROUND in neons (surface (station) pressure is
	// PGR-PA)

	else if (paramName == "P-PA" && levelName == "MEANSEA")
	{
		levelName = "GROUND";
	}

	else if (paramName == "NL-PRCNT")
	{
		paramName = "NL-0TO1";
	}

	else if (paramName == "NM-PRCNT")
	{
		paramName = "NM-0TO1";
	}

	else if (paramName == "NH-PRCNT")
	{
		paramName = "NH-0TO1";
	}

	if (pl.stepAdjustment < 0)
	{
#ifdef DEBUG
		std::cout << "Param " << pl.paramName << "/" << pl.levelName << "/" << pl.levelValue << " at step " << step
		          << " has step adjustment " << pl.stepAdjustment << std::endl;
#endif

		if (step < 0)
		{
			throw std::runtime_error("Previous timestep data requested for time step 0");
		}

		if (mosInfo.label == "MOS_ECMWF_040422" && step <= 90 && paramName != "T-MEAN-K")
		{
			step += 1 * pl.stepAdjustment;
		}
		else if (step <= 144)
		{
			step += 3 * pl.stepAdjustment;
		}
		else
		{
			step += 6 * pl.stepAdjustment;
		}
	}

	auto realOrigin = mosInfo.originTime;

	if (pl.originTimeAdjustment == -1)
	{
#ifdef DEBUG
		std::cout << "Param " << pl.paramName << "/" << pl.levelName << "/" << pl.levelValue << " at step " << step
		          << " has origintime adjustment " << pl.originTimeAdjustment << std::endl;
#endif
		using namespace boost::posix_time;

		ptime time(time_from_string(mosInfo.originTime));
		time = time - hours(12);

		realOrigin = ToSQLTime(time);

		step += 12;
	}

	if (paramName == "T-MEAN-K" && pl.originTimeAdjustment == -1)
	{
		int origStep = step;

		// Fetching T-MEAN-K from previous forecast does not work for leadtime 135/141:
		// leadtime 147/153 does not exist! In this case use data from leadtime 144/150.

		if ((step == 147 || step == 153))
		{
			step -= 3;
			std::cout << "Adjusting T-MEAN-K step from " << origStep << " to " << step << std::endl;
		}

		// T-MEAN-K does not exist for 1h steps
		else if (step <= 90)
		{
			step -= (step % 3);
			std::cout << "Adjusting T-MEAN-K step from " << origStep << " to " << step << std::endl;
		}
	}
	auto prodInfo = itsRadonDB->GetProducerDefinition(producerId);

	assert(prodInfo.size());

	auto gridgeoms = itsRadonDB->GetGridGeoms(prodInfo["ref_prod"], realOrigin);

	assert(gridgeoms.size());

	if (gridgeoms.size() > 1)
	{
		// order so that GLO is first, EUR second
		std::sort(gridgeoms.begin(), gridgeoms.end(),
		          [](const std::vector<std::string>& lhs, const std::vector<std::string>& rhs)
		          {
			          if (lhs[3].find("ECGLO") != std::string::npos && rhs[3].find("ECGLO") == std::string::npos)
			          {
				          return true;
			          }
			          if (lhs[3].find("ECEUR") != std::string::npos &&
			              (rhs[3].find("ECGLO") == std::string::npos && rhs[3].find("ECEUR") == std::string::npos))
			          {
				          return true;
			          }
			          return false;
		          });
	}

	std::vector<datas> ret;

	for (const auto& geom : gridgeoms)
	{
		const std::string tableName = geom[1];

		std::stringstream query;

		query << "SELECT param_name, level_name, level_value, extract(epoch from forecast_period) / 3600, "
		      << "file_location, byte_offset, byte_length "
		      << "FROM " << tableName << "_v "
		      << "WHERE param_name = upper('" << paramName << "') "
		      << "AND level_name = upper('" << levelName << "') "
		      << "AND level_value = " << pl.levelValue << " "
		      << "AND extract(epoch from forecast_period) / 3600 = " << step << " AND analysis_time = '" << realOrigin
		      << "'"
		      << " AND geometry_id = " << geom[0] << " ORDER BY 4,2,3";

		itsRadonDB->Query(query.str());

		const auto row = itsRadonDB->FetchRow();

		if (row.empty())
		{
			continue;
		}

		ret.push_back(ToQueryInfo(pl, step, row[4], row[5], row[6]));
		break;  // stop on first grid found
	}

	if (ret.empty())
	{
		throw std::runtime_error("No data found for " + boost::lexical_cast<std::string>(producerId) + "/" +
		                         Key(pl, step, mosInfo.originTime));
	}

	return ret;
}

datas ToQueryInfo(const ParamLevel& pl, int step, const std::string& fileName, const std::string& offset,
                  const std::string& length)
{
	NFmiGrib reader;

	if (!reader.Open(fileName))
	{
		std::cerr << "File open failed for " << fileName << std::endl;
		throw 1;
	}

	if (offset.empty() && length.empty())
	{
		std::cout << "Reading file '" << fileName << "' (" << pl << ")" << std::endl;
		reader.NextMessage();
	}
	else
	{
		std::cout << "Reading file '" << fileName << "' " << offset << ":" << length << " (" << pl << ")" << std::endl;
		reader.ReadMessage(std::stoi(offset), std::stoi(length));
	}

	long dataDate = reader.Message().DataDate();
	long dataTime = reader.Message().DataTime();

	NFmiTimeList tlist;

	tlist.Add(new NFmiMetTime(boost::lexical_cast<long>(dataDate), boost::lexical_cast<long>(dataTime)));

	NFmiTimeDescriptor tdesc(tlist.FirstTime(), tlist);

	NFmiParamBag pbag;
	NFmiParam p(1, pl.paramName);
	p.InterpolationMethod(InterpolationMethod(pl.paramName));

	pbag.Add(NFmiDataIdent(p));

	NFmiParamDescriptor pdesc(pbag);

	NFmiLevelBag lbag(kFmiAnyLevelType, 0, 0, 0);

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

	if (lx < 0)
	{
		tr.X(lx + 360);
	}

	bl.Y(fy);
	tr.Y(ly);

	size_t len = ni * nj;
	double* ddata = new double[len];
	reader.Message().GetValues(ddata, &len);

	assert(len == static_cast<size_t> (ni * nj));

	if (!jpos)
	{
		bl.Y(ly);
		tr.Y(fy);

		size_t halfSize = static_cast<size_t>(floor(static_cast<double>(nj) / 2));

		for (size_t y = 0; y < halfSize; y++)
		{
			for (size_t x = 0; x < static_cast<size_t>(ni); x++)
			{
				size_t ui = y * ni + x;
				size_t li = (nj - 1 - y) * ni + x;
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

	NFmiGrid grid(area, ni, nj, kBottomLeft, InterpolationMethod(pl.paramName));

	NFmiHPlaceDescriptor hdesc(grid);

	NFmiFastQueryInfo qi(pdesc, tdesc, hdesc, vdesc);

	auto data = std::shared_ptr<NFmiQueryData>(NFmiQueryDataUtil::CreateEmptyData(qi));

	NFmiFastQueryInfo info(data.get());
	info.First();

	int num = 0;
	for (info.ResetLocation(); info.NextLocation(); ++num)
	{
		info.FloatValue(static_cast<float>(ddata[num]));
	}

	delete area;
	delete[] ddata;

	double dx = reader.Message().iDirectionIncrement();
	double dy = reader.Message().jDirectionIncrement();

	const double wantedGridResolution = 0.125;

	if (wantedGridResolution < dx)
	{
		std::cout << "Will not interpolate to a finer grid (" << wantedGridResolution << ") than the source data ("
		          << dx << ")" << std::endl;
		return std::make_pair(data, info);
	}

#ifdef EXTRADEBUG
	NFmiStreamQueryData streamData;
	assert(streamData.WriteData(pl.paramName + "_" + pl.levelName + "_" +
	                                boost::lexical_cast<std::string>(pl.levelValue) + "_" +
	                                boost::lexical_cast<std::string>(step) + ".fqd",
	                            data.get()));
#endif

	if (opts.disable0125 == false && (dx != wantedGridResolution || dy != wantedGridResolution))
	{
#ifdef DEBUG
		std::cout << "Interpolating " << pl << " to " << wantedGridResolution << " degree grid" << std::endl;
#endif
		auto ret = InterpolateToGrid(info, wantedGridResolution);

#ifdef EXTRADEBUG
		assert(streamData.WriteData(pl.paramName + "_" + pl.levelName + "_" +
		                                boost::lexical_cast<std::string>(pl.levelValue) + "_" +
		                                boost::lexical_cast<std::string>(step) + "_" +
		                                boost::lexical_cast<std::string>(wantedGridResolution) + ".fqd",
		                            ret.first.get()));
#endif
		return ret;
	}

	assert(dx <= wantedGridResolution);
	assert(dy <= wantedGridResolution);

	return std::make_pair(data, info);
}

datas InterpolateToGrid(NFmiFastQueryInfo& sourceInfo, double distanceBetweenGridPointsInDegrees)
{
	auto bl = sourceInfo.Area()->BottomLeftLatLon();
	auto tr = sourceInfo.Area()->TopRightLatLon();

	assert(tr.X() > bl.X());
	assert(tr.Y() > bl.Y());

	int ni = static_cast<int>(fabs(tr.X() - bl.X()) / distanceBetweenGridPointsInDegrees);
	int nj = static_cast<int>(fabs(tr.Y() - bl.Y()) / distanceBetweenGridPointsInDegrees);

	NFmiGrid grid(sourceInfo.Area(), ni, nj, kBottomLeft, sourceInfo.Grid()->InterpolationMethod());

	NFmiHPlaceDescriptor hdesc(grid);

	NFmiFastQueryInfo qi(sourceInfo.ParamDescriptor(), sourceInfo.TimeDescriptor(), hdesc,
	                     sourceInfo.VPlaceDescriptor());

	auto data = std::shared_ptr<NFmiQueryData>(NFmiQueryDataUtil::CreateEmptyData(qi));

	NFmiFastQueryInfo info(data.get());
	info.First();

	for (info.ResetLocation(); info.NextLocation();)
	{
		info.FloatValue(sourceInfo.InterpolatedValue(info.LatLon()));
	}

	return std::make_pair(data, info);
}

double Declination(int step, const std::string& originTime)
{
	auto orig = ToPtime(originTime, "%Y-%m-%d %H:%M:00");
	orig += boost::posix_time::seconds(3600 * step);

	tm orig_tm = to_tm(orig);

	// Formula from Jussi Ylhaisi

	const int hour_of_day = orig_tm.tm_hour;

	// Sekä deklinaation että länpötilan vuosisyklin jaksonaika on tasan yksi
	// vuosi, mutta näillä
	// aalloilla on vaihe-ero: Lämpötilan vuosisykli on hieman perässä. Esim.
	// aurinko on on korkeimmillaan
	// juhannuksena kesäkuun lopulla, mutta silti heinäkuu on ilmastollisesti
	// lämpimin kuukausi.
	// Keskimääräiseksi vaihe-eroksi talvi-kesäkausilla tulee kuitenkin n. 32
	// päivää, tällä saadaan aallot synkkaan.
	// Tämä on ihan ok oletus kaikkina vuorokaudenaikoina

	double daydoy = orig_tm.tm_yday + hour_of_day / 24. - 32.;

	if (daydoy < 0)
		daydoy += 365.;

	// 1) Lasketaan auringon deklinaatio (maan akselin ja maan kiertorataa
	// kohtisuoran viivan välinen kulma)

	// Tässä daydoy on vuoden päivämäärä 0...365/366 vuoden alusta lukien. Tunnit
	// luetaan tähän mukaan,
	// eli esim. ajanhetkelle 2.1. klo 15 daydoy=1.625. Huom. päivä ei siis ala
	// indeksistä 1, vaan 0!
	// Ylläolevat laskut antavat ulos asteina deklinaation.

	const double declination = -asin(0.39779 * cos(0.98565 / 360 * 2 * PI * (daydoy + 10) +
	                                               1.914 / 360 * 2 * PI * sin(0.98565 / 360 * 2 * PI * (daydoy - 2)))) *
	                           360 / 2 / PI;

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
