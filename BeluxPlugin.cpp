#include "pch.h"
#include "BeluxPlugin.hpp"
#include <string>
#include <map>
#include <set>
#include <utility>
#include <Windows.h>

using namespace std;
using namespace EuroScopePlugIn;

using boost::asio::ip::tcp;

bool DEBUG_print = false;
bool function_fetch_gates = true;
bool function_set_initial_climb = true;

int timeout_value = 1000;

// internal ID lists
const int TAG_ITEM_GATE_ASGN = 1;
const int TAG_FUNCTION_REFRESH_GATE = 2;

// Time (in seconds) before we request new information about this flight from the API.
const int DATA_RETENTION_LENGTH = 60;

set<string>* knownCallsigns;
set<string>* beluxAirports;
BeluxGatePlanner gatePlanner;

int liege_QNH = 0;


BeluxPlugin::BeluxPlugin(void) : CPlugIn(EuroScopePlugIn::COMPATIBILITY_CODE, MY_PLUGIN_NAME, MY_PLUGIN_VERSION, MY_PLUGIN_DEVELOPER, MY_PLUGIN_COPYRIGHT)
{
    string loadingMessage = MY_PLUGIN_VERSION;
    loadingMessage += " loaded.";
 
    DisplayUserMessage("Message", "Belux Plugin", loadingMessage.c_str(), true, true, true, false, false);
    DisplayUserMessage("Belux Plugin", "Plugin version", loadingMessage.c_str(), true, true, true, false, false);
    string latest_version = GetLatestPluginVersion();
    if (latest_version != "S_ERR"){
        if (latest_version != MY_PLUGIN_VERSION) {
            string message = "You are using an older version of the Belux plugin. Updating to " + latest_version + " is adviced. For safety, API-based functions have been disabled";
            function_fetch_gates = false;
            MessageBox(0, message.c_str(), "Belux plugin version", MB_OK | MB_ICONQUESTION);
        }
        else {
            DisplayUserMessage("Belux Plugin", "Plugin version", "You are using the latest version", true, true, true, false, false);
        }
    }
    else {
        DisplayUserMessage("Belux Plugin", "Plugin version", "Failed verifying latest version", true, true, true, false, false);
    }

    beluxAirports = new set<string>({ "EBBR", "ELLX", "EBOS", "EBAW", "EBLG", "EBKT", "EBCI" });
    knownCallsigns = new set<string>();

    // Register Tag item(s).
    RegisterTagItemType("Assigned Gate", TAG_ITEM_GATE_ASGN);
    RegisterTagItemFunction("refresh assigned gate", TAG_FUNCTION_REFRESH_GATE);

    ProcessMETAR("EBLG", GetAirportInfo("EBLG"));
    if(function_fetch_gates)
        gatePlanner.fetch_json(GetGateInfo());
}

BeluxPlugin::~BeluxPlugin() {
    delete beluxAirports;
    delete knownCallsigns;
}

void BeluxPlugin::OnFlightPlanFlightPlanDataUpdate(CFlightPlan FlightPlan) {
    if (function_set_initial_climb) {
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
                if (liege_QNH == 0) 
                    ProcessMETAR("EBLG", GetAirportInfo("EBLG"));
                
                if (liege_QNH != 0 && liege_QNH < 995) {
                    CFL = 6000;
                }else {
                    CFL = 5000;
                }
            }

            if (CFL > 0 && FlightPlan.GetFlightPlanData().GetFinalAltitude() > CFL) {
                FlightPlan.GetControllerAssignedData().SetClearedAltitude(CFL);
            }

            knownCallsigns->insert(callsign);
        }
    }
}

void BeluxPlugin::OnFlightPlanDisconnect(CFlightPlan FlightPlan) {
    if (function_set_initial_climb) {
        string callsign = FlightPlan.GetCallsign();
        knownCallsigns->erase(callsign);
    }
}

void BeluxPlugin::OnNewMetarReceived(const char* sStation, const char* sFullMetar) {
    if (function_set_initial_climb) {
        if (sStation == "EBLG") {
            string metar = sFullMetar;
            ProcessMETAR("EBLG", metar);
        }
    }
}

void BeluxPlugin::ProcessMETAR(string airport, string metar) {
    if (airport == "EBLG") {
        try {
            size_t pos = metar.find("Q") + 1;
            liege_QNH = stoi(metar.substr(pos, 4));
            if (DEBUG_print) {
                char buffer[50];
                sprintf_s(buffer, "SET EBLG QNH Q%d", liege_QNH);
                DisplayUserMessage("Belux Plugin", "CFL setter", buffer, true, true, true, false, false);
            }
        }
        catch (const exception& e) {}
    }
}

void BeluxPlugin::OnFunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area) {
    switch (FunctionId) {
    case TAG_FUNCTION_REFRESH_GATE:
        if (function_fetch_gates)
            gatePlanner.fetch_json(GetGateInfo());
        break;
    }
}

void BeluxPlugin::OnTimer(int Counter) {
    if (function_fetch_gates && (Counter % DATA_RETENTION_LENGTH == 0)) {
        gatePlanner.fetch_json(GetGateInfo());
    }
}

void BeluxPlugin::OnGetTagItem(CFlightPlan FlightPlan, CRadarTarget RadarTarget,
    int ItemCode, int TagData,
    char sItemString[16], int* pColorCode,
    COLORREF* pRGB, double* pFontSize) {
    // Only work on tag items we actually care about.
    switch (ItemCode) {
    case TAG_ITEM_GATE_ASGN:
        if (function_fetch_gates) {
            string dest = FlightPlan.GetFlightPlanData().GetDestination();

            //Only continue and fetch gate when: flies to EBBR/ELLX
            if (dest != "EBBR" && dest != "ELLX") { 
                return;
            }

            string cs = FlightPlan.GetCallsign();
            if (gatePlanner.gate_list.find(cs) != gatePlanner.gate_list.end()) {
                if (gatePlanner.gate_list[cs].isFetched) {
                    if (gatePlanner.gate_list[cs].gate_has_changed) {
                        //---GATE Change detected------
                        string message = cs + " ==> " + gatePlanner.gate_list[cs].gate;
                        DisplayUserMessage("Belux Plugin", "GATE CHANGE", message.c_str(), true, true, true, true, true);
                        gatePlanner.gate_list[cs].color = RGB(50, 205, 50);
                    }
                    //When closer than 15NM and below 3500ft, insert into OP_TEXT
                    if ((gatePlanner.gate_list[cs].airport == "EBBR" && FlightPlan.GetDistanceToDestination() < 15 && FlightPlan.GetCorrelatedRadarTarget().GetPosition().GetPressureAltitude() < 3500)
                        || (gatePlanner.gate_list[cs].airport == "ELLX" && FlightPlan.GetDistanceToDestination() < 10 && FlightPlan.GetCorrelatedRadarTarget().GetPosition().GetPressureAltitude() < 4500)) {
                        FlightPlan.GetControllerAssignedData().SetScratchPadString(gatePlanner.gate_list[cs].gate.c_str());
                    }
                    gatePlanner.gate_list[cs].isFetched = false;
                }
                if (gatePlanner.gate_list[cs].color != NULL) {
                    (*pColorCode) = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
                    (*pRGB) = gatePlanner.gate_list[cs].color;
                }

                string gateItem = gatePlanner.gate_list[cs].gate + (gatePlanner.gate_list[cs].suggest25R ? "*" : "");
                strcpy_s(sItemString, 8, gateItem.c_str());
            }
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

    string response = GetHttpsRequest(host, uri, request.str(), false);
    return response;
}

void BeluxPlugin::printDebugMessage(string message) {
    DisplayUserMessage("Belux Plugin", "DEBUG", message.c_str(), true, true, true, true, true);
}

string BeluxPlugin::GetLatestPluginVersion() {
    const string host = "api.beluxvacc.org";
    const string uri = "/belux-gate-manager-api/version/plugin";

    // Form the request.
    std::stringstream request;
    request << "GET " << uri << " HTTP/1.1\r\n";;
    request << "Host: " << host << "\r\n\r\n";

    string response = GetHttpsRequest(host, uri, request.str(), false);
    response = response.substr(response.length() - 2 - 5, 5);
    return response;
}


std::string BeluxPlugin::GetGateInfo() {
    const string host = "api.beluxvacc.org";
    const string uri = "/belux-gate-manager-api/get_all_assigned_gates/";

    // Form the request.
    std::stringstream request;
    request << "GET " << uri << " HTTP/1.1\r\n";;
    request << "Host: " << host << "\r\n\r\n";

    string response = GetHttpsRequest(host, uri, request.str(), true);
    return response;
}

string BeluxPlugin::SwapGate(string callsign, string gate) {
    const string host = "api.beluxvacc.org";
    const string uri = "/belux-gate-manager-api/swap_gate/";

    // Form the request.
    std::stringstream request;
    request << "POST " << uri << " HTTP/1.1\r\n";;
    request << "Host: " << host << "\r\n";
    request << "Content-Type: application/x-www-form-urlencoded\r\n";
    request << "Content-Length: " << (18 + callsign.length() + gate.length()) << "\r\n\r\n" ;
    request << "callsign=" + callsign + "&gate_id=" + gate << "\r\n";

    string response = GetHttpsRequest(host, uri, request.str(), false);
    return response;
}

string BeluxPlugin::GetHttpsRequest(string host, string uri, string request_string, bool expect_long_json) {
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
        ssock.lowest_layer().set_option(boost::asio::detail::socket_option::integer<SOL_SOCKET, SO_RCVTIMEO>{ timeout_value });
        ssock.lowest_layer().set_option(boost::asio::detail::socket_option::integer<SOL_SOCKET, SO_SNDTIMEO>{ timeout_value });

        // Do the SSL handshake
        ssock.handshake(boost::asio::ssl::stream_base::handshake_type::client);

        // Send the request.
        boost::asio::streambuf request;
        ostream request_stream(&request);
        request_stream << request_string;
        boost::asio::write(ssock, request);

        // Read the response line.
        boost::asio::streambuf response;
        response.prepare(4 * 1024 * 1024); //4MB
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
        ostringstream stream;
        if (response.size() > 0) {
            stream << &response;
        }

        if (expect_long_json && stream.str().back() != ']') {
            // Read until EOF, writing data to output as we go.
            boost::system::error_code error;
            while (boost::asio::read(ssock, response,
                boost::asio::transfer_at_least(1), error)) {
                stream << &response;
                if (stream.str().back() == ']')
                    break;
            }
        }
        data = stream.str();
        string dbgmsg = "received: " + data;
        if (DEBUG_print) {
            printDebugMessage(dbgmsg);
        }

        return data;
    }
    catch (exception& e) {
        if (DEBUG_print) {
            DisplayUserMessage("Belux Plugin", "HTTPS error", e.what(), true, true, true, false, false);
        }
        return "HTTPS_ERROR";
    }
}

bool BeluxPlugin::OnCompileCommand(const char* sCommandLine) {
    if (boost::algorithm::starts_with(sCommandLine, ".belux setgate"))
    {
        if (ControllerMyself().GetFacility() >= 3 || DEBUG_print) {
            string buffer{ sCommandLine };
            buffer.erase(0, 15);
            string gate = buffer;
            string selected_callsign = RadarTargetSelectASEL().GetCallsign();
            if (selected_callsign != "" && gate != "") {
                string result = SwapGate(selected_callsign, gate);
                string message;
                if (result != "HTTPS_ERROR") {
                    message = selected_callsign + " succesfully assigned to gate " + gate;
                    gatePlanner.gate_list[selected_callsign].gate = gate;
                    gatePlanner.gate_list[selected_callsign].isFetched = true;
                }
                else {
                    message = "Something went wrong when trying to assign gate " + gate + "  to " + selected_callsign;
                }
                DisplayUserMessage("Belux Plugin", "Gate assignment", message.c_str(), true, true, true, false, false);
                return true;
            }
        }
        return false;
    }
    if (boost::algorithm::starts_with(sCommandLine, ".belux gateon"))
    {
        function_fetch_gates = true;
        DisplayUserMessage("Belux Plugin", "Functions", "Enabled gate assigner", true, true, true, false, false);
        return true;
    }
    if (boost::algorithm::starts_with(sCommandLine, ".belux gateoff"))
    {
        function_fetch_gates = false;
        DisplayUserMessage("Belux Plugin", "Functions", "Disabled gate assigner", true, true, true, false, false);
        return true;
    }
    if (boost::algorithm::starts_with(sCommandLine, ".belux climbon"))
    {
        function_set_initial_climb = true;
        DisplayUserMessage("Belux Plugin", "Functions", "Enabled auto initial climb", true, true, true, false, false);
        return true;
    }
    if (boost::algorithm::starts_with(sCommandLine, ".belux climboff")) {
            function_set_initial_climb = false;
            DisplayUserMessage("Belux Plugin", "Functions", "Disabled auto initial climb", true, true, true, false, false);
            return true;
    }
    if (boost::algorithm::starts_with(sCommandLine, ".belux debugon") || boost::algorithm::starts_with(sCommandLine, ".dbgon"))
    {
        DEBUG_print = true;
        DisplayUserMessage("Belux Plugin", "Functions", "Enabled debug mode", true, true, true, false, false);            
        return true;
    }
    if (boost::algorithm::starts_with(sCommandLine, ".belux debugoff") || boost::algorithm::starts_with(sCommandLine, ".dbgoff")) {
        DEBUG_print = false;
        DisplayUserMessage("Belux Plugin", "Functions", "Disabled debug mode", true, true, true, false, false);
        return true;
    }
    if (boost::algorithm::starts_with(sCommandLine, ".belux timeout")) {
        string buffer{ sCommandLine };
        buffer.erase(0, 15);
        try {
            timeout_value = stoi(buffer);
            string message = "Setted timeout to " + std::to_string(timeout_value);
            DisplayUserMessage("Belux Plugin", "Functions", message.c_str(), true, true, true, false, false);
            return true;
        }
        catch (exception& e) {
            return false;
        }
    }
    if (boost::algorithm::starts_with(".belux refreshgates", sCommandLine) || boost::algorithm::starts_with(sCommandLine, ".rf"))
    {
        if (function_fetch_gates)
            gatePlanner.fetch_json(GetGateInfo());
        return true;
    }
    return false;
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