#include "pch.h"
#include "BeluxPlugin.hpp"
#include <string>
#include <map>
#include <set>
#include <utility>

using namespace std;
using namespace EuroScopePlugIn;

using boost::asio::ip::tcp;

bool DEBUG = false;

// internal ID lists
const int TAG_ITEM_GATE_ASGN = 1;
const int TAG_FUNCTION_REFRESH_GATE = 2;

// Time (in seconds) before we request new information about this flight from the API.
const int DATA_RETENTION_LENGTH = 60;

map<string, BeluxGatePlanner>* m_knownFlightInfo;
set<string>* knownCallsigns;
set<string>* beluxAirports;

int liege_QNH = 0;


BeluxPlugin::BeluxPlugin(void) : CPlugIn(EuroScopePlugIn::COMPATIBILITY_CODE, MY_PLUGIN_NAME, MY_PLUGIN_VERSION, MY_PLUGIN_DEVELOPER, MY_PLUGIN_COPYRIGHT)
{
    string loadingMessage = "Version: ";
    loadingMessage += MY_PLUGIN_VERSION;
    loadingMessage += " loaded.";
    DisplayUserMessage("Message", "Belux Plugin", loadingMessage.c_str(), true, true, true, false, false);

    beluxAirports = new set<string>({ "EBBR", "ELLX", "EBOS", "EBAW", "EBLG", "EBKT", "EBCI" });
    m_knownFlightInfo = new map<string, BeluxGatePlanner>();
    knownCallsigns = new set<string>();

    // Initialize m_knownGates with an invalid flightplan.
    BeluxGatePlanner json = BeluxGatePlanner();
    m_knownFlightInfo->insert(pair<string, BeluxGatePlanner>(json.Callsign, json));

    // Register Tag item(s).
    RegisterTagItemType("Assigned Gate", TAG_ITEM_GATE_ASGN);
    RegisterTagItemFunction("refresh assigned gate", TAG_FUNCTION_REFRESH_GATE);

    ProcessMETAR("EBLG", GetAirportInfo("EBLG"));
}

BeluxPlugin::~BeluxPlugin() {
    delete beluxAirports;
    delete m_knownFlightInfo;
    delete knownCallsigns;
}

void BeluxPlugin::OnFunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area) {
    switch (FunctionId) {
    case TAG_FUNCTION_REFRESH_GATE:
        map<string, BeluxGatePlanner>::iterator it;
        for (it = m_knownFlightInfo->begin(); it != m_knownFlightInfo->end(); it++)
        {
            it->second.lastModified -= DATA_RETENTION_LENGTH;
        }
        break;
    }
}

void BeluxPlugin::OnFlightPlanFlightPlanDataUpdate(CFlightPlan FlightPlan) {
    string ICAO = FlightPlan.GetFlightPlanData().GetOrigin();
    string callsign = FlightPlan.GetCallsign();

    if (beluxAirports->find(ICAO) == beluxAirports->end()                                          // IF Not found in belux airport list
        || !FlightPlan.IsValid() || !FlightPlan.GetCorrelatedRadarTarget().IsValid()             // OR flightplan has not been loaded/correleted correctly?
        || FlightPlan.GetCorrelatedRadarTarget().GetGS() > 5                                     // OR moving: Ground speed > 5knots
        || FlightPlan.GetCorrelatedRadarTarget().GetPosition().GetPressureAltitude() > 1500      // OR flying: Altitude > 1500 feet
        || FlightPlan.GetCorrelatedRadarTarget().GetPosition().GetPressureAltitude() == 0) {     // OR altitude == 0 -> uncorrelated? alitude should never be zero
        return;         // THEN SKIP 
    }

    //Saftey check: only set CFL once
    if (knownCallsigns->find(callsign) == knownCallsigns->end()) {
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
            if (liege_QNH == 0) {
                ProcessMETAR("EBLG", GetAirportInfo("EBLG"));
            }

            if (liege_QNH != 0 && liege_QNH < 995) {
                sprintf_s(buffer, "EBLG Q%d < 995. SETTING FL60", liege_QNH);
                DisplayUserMessage("Belux Plugin", "Auto Departure CFL", buffer, true, true, true, false, false);
                CFL = 6000;
            }
            else {
                if (liege_QNH == 0) {
                    DisplayUserMessage("Belux Plugin", "Auto Departure CFL", "EBLG QNH UNKNOWN. SETTING FL50", true, true, true, false, false);
                }
                else {
                    sprintf_s(buffer, "EBLG Q%d >= 995. SETTING FL50", liege_QNH);
                    DisplayUserMessage("Belux Plugin", "Auto Departure CFL", buffer, true, true, true, false, false);
                }
                CFL = 5000;
            }
        }

        if (CFL > 0 && FlightPlan.GetFlightPlanData().GetFinalAltitude() > CFL) {
            FlightPlan.GetControllerAssignedData().SetClearedAltitude(CFL);
        }
        knownCallsigns->insert(callsign);
    }
}

void BeluxPlugin::OnFlightPlanDisconnect(CFlightPlan FlightPlan) {
    string callsign = FlightPlan.GetCallsign();
    knownCallsigns->erase(callsign);
    m_knownFlightInfo->erase(callsign);
}

void BeluxPlugin::OnNewMetarReceived(const char* sStation, const char* sFullMetar) {
    if (sStation == "EBLG") {
        string metar = sFullMetar;
        ProcessMETAR("EBLG", metar);
    }
}

void BeluxPlugin::ProcessMETAR(string airport, string metar) {
    if (airport == "EBLG") {
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

        //Only continue and fetch gate when: flies to brussels AND i'm tracking the airplane OR i'm observing
        if (dest != "EBBR"){ // || (!FlightPlan.GetTrackingControllerIsMe() && ControllerMyself().IsController())) {
            return;
        }

        string cs = FlightPlan.GetCallsign();
        BeluxGatePlanner res;
        if (m_knownFlightInfo->find(cs) != m_knownFlightInfo->end()) {
            //---Should we get new info?
            time_t lastMod = (*m_knownFlightInfo)[cs].lastModified + DATA_RETENTION_LENGTH;
            time_t now = time(NULL);

            if (now < lastMod && lastMod > 0) { // Greater than 0 to prevent unset values being strange.
                // Information too recent.
                res = (*m_knownFlightInfo)[cs];
            }
            else {
                //---API Info Retrieval------
                string old = (*m_knownFlightInfo)[cs].Gate;
                res = GetGateInfo(cs);
                (*m_knownFlightInfo)[cs] = res;

                if (old != res.Gate && old != "" && res.Gate != "") {
                    //---GATE Change detected------
                    char buffer[70];
                    sprintf_s(buffer, "FOR %s: %s ==> %s", cs.c_str(), old.c_str(), res.Gate.c_str());
                    DisplayUserMessage("Belux Plugin", "GATE CHANGE", buffer, true, true, true, true, true);        
                    (*m_knownFlightInfo)[cs].color = RGB(50, 205, 50);
                }
            }
        }
        else {
            //----Initial fetch------
            res = GetGateInfo(cs);
            (*m_knownFlightInfo)[cs] = res;
        }

        //---API Info Verification------
        if (cs == res.Callsign) {
            //----- Put (new) gate into the ES UI.
            if ((*m_knownFlightInfo)[cs].color != NULL) {
                (*pColorCode) = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
                (*pRGB) = (*m_knownFlightInfo)[cs].color;
            }

            string gateItem = (*m_knownFlightInfo)[cs].Gate + ((*m_knownFlightInfo)[cs].suggest25R ? "*" : "");
            strcpy_s(sItemString, 8 , gateItem.c_str());

            //When closer than 15NM and below 3500ft, insert into OP_TEXT
            if (FlightPlan.GetDistanceToDestination() < 15 && FlightPlan.GetCorrelatedRadarTarget().GetPosition().GetPressureAltitude() < 3500) {
                FlightPlan.GetControllerAssignedData().SetScratchPadString((*m_knownFlightInfo)[cs].Gate.c_str());
            }
        }
        else {
            sItemString = "ERR";
        }
    }
}

string BeluxPlugin::GetAirportInfo(string airport) {
    const string host = "metar.vatsim.net";
    const string uri = "/search_metar.php?id=" + airport;

    // Form the request.
    std::stringstream request;
    request << "GET " << uri << " HTTP/1.1\r\n";;
    request << "Host: " << host << "\r\n\r\n";

    string response = GetHttpsRequest(host, uri, request.str());
    return response;
}

void BeluxPlugin::printDebugMessage(string message) {
    DisplayUserMessage("Belux Plugin", "DEBUG", message.c_str(), true, true, true, true, true);
}


BeluxGatePlanner BeluxPlugin::GetGateInfo(string callsign) {
    const string host = "api.beluxvacc.org";
    const string uri = "/belux-gate-manager-api-develop/get_gate/";

    // Form the request.
    std::stringstream request;
    request << "POST " << uri << " HTTP/1.1\r\n";;
    request << "Host: " << host << "\r\n";
    request << "Content-Type: application/x-www-form-urlencoded\r\n";
    request << "Content-Length: " << (9 + callsign.length()) << "\r\n\r\n";
    request << "callsign=" << callsign << "\r\n";

    string response = GetHttpsRequest(host, uri, request.str());
    if (response == "HTTPS_ERROR") {
        return BeluxGatePlanner("[{\"gate\":\"ERR\",\"assigned_to\":\"" + callsign + "\"}]");
    }
    else if (response == "[]") {
        return BeluxGatePlanner("[{\"gate\":\"\",\"assigned_to\":\"" + callsign + "\"}]");
    }
    else {
        return BeluxGatePlanner(response);
    }
}

string BeluxPlugin::GetHttpsRequest(string host, string uri, string request_string) {
    string data = "";
    try {
        // Initialize the asio service.
        boost::asio::io_service io_service;
        boost::asio::ssl::context context(boost::asio::ssl::context::sslv23);
        boost::asio::ssl::stream<tcp::socket> ssock(io_service, context);

        if (!SSL_set_tlsext_host_name(ssock.native_handle(), host.c_str()))
        {
            boost::system::error_code ec{ static_cast<int>(::ERR_get_error()), boost::asio::error::get_ssl_category() };
            throw boost::system::system_error{ ec };
        }

        // Get a list of endpoints corresponding to the server name.
        tcp::resolver resolver(io_service);
        tcp::resolver::query query(host, "https");
        auto it = resolver.resolve(query);
        boost::asio::connect(ssock.lowest_layer(), it);
        ssock.handshake(boost::asio::ssl::stream_base::handshake_type::client);


        // Send the request.
        boost::asio::streambuf request;
        ostream request_stream(&request);
        request_stream << request_string;
        boost::asio::write(ssock, request);
   
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
            throw exception("HTTPS status: " + status_code);
        }

        // Read the response headers, which are terminated by a blank line.
        boost::asio::read_until(ssock, response, "\r\n\r\n");
        string header;
        while (getline(response_stream, header) && header != "\r")
            continue;

        // Write whatever content we already have to output.
        if (response.size() > 0) {
            getline(istream(&response), data);
        }

        string dbgmsg = "received: " + data;
        if (DEBUG) {
            printDebugMessage(dbgmsg);
        }

        return data;
    }
    catch (exception& e) {
        DisplayUserMessage("Belux Plugin", "HTTPS error", e.what(), true, true, true, false, false);
        return "HTTPS_ERROR";
    }
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