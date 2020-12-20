#pragma once
#include "EuroScopePlugIn.h"
#include "BeluxGatePlanner.hpp"
#include <time.h>


#define MY_PLUGIN_NAME      "BeluxPlugin"
#define MY_PLUGIN_VERSION   "0.2 beta"
#define MY_PLUGIN_DEVELOPER "Nicola Macoir"
#define MY_PLUGIN_COPYRIGHT "GPL v3"
#define MY_PLUGIN_VIEW_AVISO  "Belux vACC"

using namespace std;
using namespace EuroScopePlugIn;

class BeluxPlugin :
	public EuroScopePlugIn::CPlugIn
{
public:
	BeluxPlugin();
	virtual ~BeluxPlugin();
	virtual void BeluxPlugin::OnNewMetarReceived(const char* sStation, const char* sFullMetar);
	virtual void BeluxPlugin::OnFlightPlanFlightPlanDataUpdate(CFlightPlan FlightPlan);
	virtual void BeluxPlugin::OnFlightPlanDisconnect(CFlightPlan FlightPlan);
	virtual void BeluxPlugin::OnGetTagItem(EuroScopePlugIn::CFlightPlan FlightPlan,
		EuroScopePlugIn::CRadarTarget RadarTarget,
		int ItemCode,
		int TagData,
		char sItemString[16],
		int* pColorCode,
		COLORREF* pRGB,
		double* pFontSize);
	//virtual bool BeluxPlugin::OnCompileCommand(const char* sCommandLine);

protected:
	BeluxGatePlanner BeluxPlugin::GetAPIInfo(string callsign);
};