#include "pch.h"
#include "BeluxPlugin.hpp"
#include <string>
#include <map>
#include <set>
#include <utility>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>


using namespace std;
using namespace EuroScopePlugIn;

using boost::asio::ip::tcp;

//API URL definitions
const string GP_API_HOST = "api.beluxvacc.org/belux-gate-manager-api-develop";
const string GP_API_ENDPOINT = "/get_gate";

// internal ID lists
const int TAG_ITEM_GATE_ASGN = 1;

// Time (in seconds) before we request new information about this flight from the API.
const int DATA_RETENTION_LENGTH = 30;

//faked API responses.
const string GP_API_REPLY_ERR_PREFIX = "[{\"gate\":\"\",\"assigned_to\":\"";
const string GP_API_REPLY_EMPTY_PREFIX = "[{\"gate\":\"\",\"assigned_to\":\"";
const string GP_API_REPLY_SUFFIX = "\"}]";

map<string, BeluxGatePlanner> m_knownFlightInfo;


int liege_QNH = 0;
set<string> knownCallsigns;
set<string> beluxAirports = { "EBBR", "ELLX", "EBOS", "EBAW", "EBLG", "EBKT", "EBCI" };


BeluxPlugin::BeluxPlugin(void) : CPlugIn(EuroScopePlugIn::COMPATIBILITY_CODE, MY_PLUGIN_NAME, MY_PLUGIN_VERSION, MY_PLUGIN_DEVELOPER, MY_PLUGIN_COPYRIGHT)
{
    string loadingMessage = "Version: ";
    loadingMessage += MY_PLUGIN_VERSION;
    loadingMessage += " loaded.";
    DisplayUserMessage("Message", "Belux Plugin", loadingMessage.c_str(), true, true, true, false, false);

    // Initialize m_knownGates with an invalid flightplan.
    BeluxGatePlanner json = BeluxGatePlanner();
    m_knownFlightInfo.insert(pair<string, BeluxGatePlanner>(json.Callsign, json));

    // Register Tag item(s).
    RegisterTagItemType("Assigned Gate", TAG_ITEM_GATE_ASGN);
}

BeluxPlugin::~BeluxPlugin() {}

void BeluxPlugin::OnFlightPlanFlightPlanDataUpdate(CFlightPlan FlightPlan) {
    string ICAO = FlightPlan.GetFlightPlanData().GetOrigin();
    string callsign = FlightPlan.GetCallsign();

    //Only continue with airplanes that are departing from BELUX airports; and are on the ground; and are not moving
    if (beluxAirports.find(ICAO) == beluxAirports.end()                                          // IF Not found in belux airport list
        || FlightPlan.GetCorrelatedRadarTarget().GetGS() > 5                                     // OR Ground speed > 5knots
        || FlightPlan.GetCorrelatedRadarTarget().GetPosition().GetPressureAltitude() > 1500) {   // OR Altitude > 1500 feet
        return;                                                                                  // THEN SKIP 
    }

    //Saftey check: only set CFL for 'new' callsigns
    if (knownCallsigns.find(callsign) == knownCallsigns.end()) {
        int CFL = 0;
        if (ICAO == "EBBR" || ICAO == "EBOS") {
            CFL = 6000;
        }
        else if (ICAO == "ELLX" || ICAO == "EBCI") {
            CFL = 4000;
        }
        else if (ICAO == "EBAW" || ICAO == "EBKT") {
            CFL = 3000;
        }
        else if (ICAO == "EBLG") {
            char buffer[50];
            if (liege_QNH != 0 && liege_QNH < 995) {
                sprintf_s(buffer, "EBLG Q%d < 995. SETTING FL60", liege_QNH);
                DisplayUserMessage("Belux Plugin", "Auto Departure CFL", buffer, true, true, true, false, false);
                CFL = 6000;
            }
            else {
                if (liege_QNH == 0) {
                    DisplayUserMessage("Belux Plugin", "Auto Departure CFL", "EBLG QNH UNKNOWN. SETTING 5000FT", true, true, true, false, false);
                }
                else {
                    sprintf_s(buffer, "EBLG Q%d >= 995. SETTING 5000FT", liege_QNH);
                    DisplayUserMessage("Belux Plugin", "Auto Departure CFL", buffer, true, true, true, false, false);
                }
                CFL = 5000;
            }
        }

        if (CFL > 0 && FlightPlan.GetFlightPlanData().GetFinalAltitude() > CFL) {
            FlightPlan.GetControllerAssignedData().SetClearedAltitude(CFL);
        }
        knownCallsigns.insert(callsign);
    }
}

void BeluxPlugin::OnFlightPlanDisconnect(CFlightPlan FlightPlan) {
    string callsign = FlightPlan.GetCallsign();
    knownCallsigns.erase(callsign);
    m_knownFlightInfo.erase(callsign);
}

void BeluxPlugin::OnNewMetarReceived(const char* sStation, const char* sFullMetar) {
    if (sStation == "EBLG") {
        string metar = sFullMetar;
        try {
            size_t pos = metar.find("Q") + 1;
            liege_QNH = stoi(metar.substr(pos, 4));
            char buffer[50];
            sprintf_s(buffer, "SET EBLG QNH Q%d", liege_QNH);
            DisplayUserMessage("Belux Plugin", "CFL setter", buffer, true, true, true, false, false);
        }
        catch (const exception& e) {}
    }
}

void BeluxPlugin::OnGetTagItem(CFlightPlan FlightPlan, CRadarTarget RadarTarget,
    int ItemCode, int TagData,
    char sItemString[16], int* pColorCode,
    COLORREF* pRGB, double* pFontSize) {
    // Only work on tag items we actually care about.
    switch (ItemCode) {
    case TAG_ITEM_GATE_ASGN:
        string dest = FlightPlan.GetFlightPlanData().GetDestination();
        if (dest != "EBBR") {
            return;
        }

        //---Pre-API Info Gathering------
        string cs = FlightPlan.GetCallsign();
        BeluxGatePlanner res;
        if (m_knownFlightInfo.find(cs) != m_knownFlightInfo.end()) {
            //---Should we get new info?
            time_t lastMod = m_knownFlightInfo[cs].lastModified + DATA_RETENTION_LENGTH;
            time_t now = time(NULL);

            if (now < lastMod && lastMod > 0) { // Greater than 0 to prevent unset values being strange.
                // Information too recent.
                res = m_knownFlightInfo[cs];
            }
            else {
                //---API Info Retrieval------
                res = GetAPIInfo(cs);
                m_knownFlightInfo[cs] = res;
            }
        }
        else {
            res = GetAPIInfo(cs);
            m_knownFlightInfo[cs] = res;
        }

        //---API Info Verification------
        if (cs == res.Callsign) {
            //// Put (new) gate into the ES UI.
            strcpy_s(sItemString, 8 , m_knownFlightInfo[cs].Gate.c_str());
            if (FlightPlan.GetDistanceToDestination() < 15 && FlightPlan.GetCorrelatedRadarTarget().GetPosition().GetPressureAltitude() < 1500) {
                FlightPlan.GetControllerAssignedData().SetScratchPadString(m_knownFlightInfo[cs].Gate.c_str());
            }
        }
        else {
            // Invalid callsign?
            sItemString = "ERR";
        }
    }
}



BeluxGatePlanner BeluxPlugin::GetAPIInfo(string callsign) {
    string data = "";

    // Which API endpoint?
    string uri = GP_API_ENDPOINT;

    //-Here be dragons.-------------------------
    try {
        // Initialize the asio service.
        boost::asio::io_service io_service;
        boost::asio::ssl::context context(boost::asio::ssl::context::sslv23);
        boost::asio::ssl::stream<tcp::socket> ssock(io_service, context);

        // Get a list of endpoints corresponding to the server name.
        tcp::resolver resolver(io_service);
        tcp::resolver::query query(GP_API_HOST, "https");
        auto it = resolver.resolve(query);

        boost::asio::connect(ssock.lowest_layer(), it);
        ssock.handshake(boost::asio::ssl::stream_base::handshake_type::client);

        // Form the request.
        boost::asio::streambuf request;
        ostream request_stream(&request);

        request_stream << "POST " << uri << " HTTP/1.1\r\n";
        request_stream << "Host: " << GP_API_HOST << "\r\n";
        request_stream << "Content-Type: application/x-www-form-urlencoded\r\n";
        request_stream << "Content-Length: "<< 9+ callsign.length() << "\r\n\r\n";
        request_stream << "callsign=" << callsign << "\r\n";

        // Send the request.
        boost::asio::write(ssock, request);
        /*boost::asio::write(socket, request);*/
        // Read the response line.
        boost::asio::streambuf response;
        boost::asio::read_until(ssock, response, "\r\n");
        // Check that response is OK.
        istream response_stream(&response);
        string http_version;
        response_stream >> http_version;
        unsigned int status_code;
        response_stream >> status_code;
        string status_message;
        getline(response_stream, status_message);
        if (!response_stream || http_version.substr(0, 5) != "HTTP/" || status_code != 200) {
            return BeluxGatePlanner(GP_API_REPLY_ERR_PREFIX + callsign + GP_API_REPLY_SUFFIX);
        }
        // Read the response headers, which are terminated by a blank line.
        boost::asio::read_until(ssock, response, "\r\n\r\n");
        // Process the response headers.
        string header;
        while (getline(response_stream, header) && header != "\r")
            continue;
        // Write whatever content we already have to output.
        if (response.size() > 0) {
            istream(&response) >> data;
        }
        string dbgmsg = "received: " + data;
        DisplayUserMessage("Belux Plugin", "Gate assigner", dbgmsg.c_str(), true, true, true, false, false);
        if (data == "[]") {
            return BeluxGatePlanner(GP_API_REPLY_EMPTY_PREFIX + callsign + GP_API_REPLY_SUFFIX);
        }
        //-End of dragons.--------------------------
    }
    catch (exception& e) {
        DisplayUserMessage("Belux Plugin", "Gate planner", e.what(), true, true, true, false, false);
        return BeluxGatePlanner(GP_API_REPLY_ERR_PREFIX + callsign + GP_API_REPLY_SUFFIX);
    }

    // Parse data
    return BeluxGatePlanner(data);
}

BeluxPlugin* gpMyPlugin = NULL;

void    __declspec (dllexport)    EuroScopePlugInInit(EuroScopePlugIn::CPlugIn** ppPlugInInstance)
{

    // create the instance
    *ppPlugInInstance = gpMyPlugin = new BeluxPlugin();
}


//---EuroScopePlugInExit-----------------------------------------------

void    __declspec (dllexport)    EuroScopePlugInExit(void)
{

    // delete the instance
    delete gpMyPlugin;
}