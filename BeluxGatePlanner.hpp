#pragma once
#include <time.h>
#include <Windows.h>
#include <map>
#include <string>

using namespace std;

class BeluxGatePlanner
{
public:
    BeluxGatePlanner();
    BeluxGatePlanner(string data);
    virtual ~BeluxGatePlanner();
    map<int, string> string_split(string data, char delimiter);
    string Callsign;
    string Gate;
    time_t lastModified;
};
