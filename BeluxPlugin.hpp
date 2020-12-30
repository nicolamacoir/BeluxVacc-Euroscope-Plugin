#pragma once
#include "EuroScopePlugIn.h"
#include "BeluxGatePlanner.hpp"
#include <time.h>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>


#define MY_PLUGIN_NAME      "BeluxPlugin"
#define MY_PLUGIN_VERSION   "0.3.1"
#define MY_PLUGIN_DEVELOPER "Belux vACC - Nicola Macoir"
#define MY_PLUGIN_COPYRIGHT "GPL v3"
#define MY_PLUGIN_VIEW_AVISO  "Belux vACC"

/*
*  Update history:
*  v0.1
*	 - GENERAL	: created and tested the plugin
*    - CFL		: basic implementation of CFL assigner
*	 - CFL		: reads QNH for liege and sets accordingly 5000ft or FL60
*	 - CFL		: some safety measures when multiple controllers are online
* 
* v0.2
*	- GENERAL	: included new tag item: assigned gate
*	- CFL		: extra safety measures to make sure CFL is only set on ground
*	- GATE		: Gate is fetched over HTTPS from API based on callsign
* 
* v0.2.1
*	- GATE		: Notification on gate change 
*	- GATE		: Notification in OP_TEXT when <15NM and <3500ft
* 
*  v0.3.1
*    - GENERAL	: reformated the code to make https request more generalised
*    - CFL		: Added extra checks to not take count for uncorrelated flightplans
*    - GATE		: Aircraft is also taken into account (for GA and MIL)
*    - GATE		: Only fetch gate when i'm tracking the airplane OR i'm observing
* 
*/

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
	//virtual void BeluxPlugin::OnDoubleClickScreenObject(int ObjectType,
	//	const char* sObjectId,
	//	POINT Pt,
	//	RECT Area,
	//	int Button);

protected:
	string BeluxPlugin::GetHttpsRequest(string host, string uri, string request);
	BeluxGatePlanner BeluxPlugin::GetGateInfo(string callsign, string aircraft);
};