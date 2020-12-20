#include "pch.h"
#include <string>
#include "BeluxGatePlanner.hpp"

using namespace std;

//-CGatePlannerJSON definitions----------------------------
BeluxGatePlanner::BeluxGatePlanner() {
    // Act as if we contacted the API and it has no idea what we are talking about.
    BeluxGatePlanner("[{\"gate\":\"ERR\",\"assigned_to\":\"ERR\"}]");
}

BeluxGatePlanner::BeluxGatePlanner(std::string data) {
    //Remove outer {}s
    size_t openCurly = data.find('{'); // Should be 0.
    size_t closeCurly = data.find_last_of('}'); // Should be length-1.
    data = data.substr(openCurly + 1, closeCurly - 1);

    //Split by commas
    std::map<int, std::string> jsonset, pieces = string_split(data, ',');
    std::string key, value;
    size_t startPos, endPos;

    // Loop over each JSON k/v set.
    for (int i = 0; i < sizeof(pieces); i++) {

        // New k/v set.
        jsonset = string_split(pieces[i], ':');

        startPos = jsonset[0].find_first_of('"');
        endPos = jsonset[0].find_last_of('"');
        key = jsonset[0].substr(startPos + 1, endPos - startPos - 1);

        startPos = jsonset[1].find_first_of('"');
        endPos = jsonset[1].find_last_of('"');
        value = jsonset[1].substr(startPos + 1, endPos - startPos - 1);

        // Assign value to the correct member.
        if (key == "assigned_to") {
            this->Callsign = value;
        }
        else if (key == "gate") {
            this->Gate = value;
        }
    } // end for pieces

    // Set the data age.
    this->lastModified = time(NULL);
}

BeluxGatePlanner::~BeluxGatePlanner() {}

std::map<int, std::string> BeluxGatePlanner::string_split(std::string data, char delimiter) {
    std::map<int, std::string> pieces;
    size_t pos = 0, prevpos = 0, c = 0;
    while ((pos = data.find_first_of(delimiter, prevpos)) != std::string::npos) {
        pieces[c] = data.substr(prevpos, pos - prevpos);
        prevpos = pos + 1;
        c++;
    }
    // No comma found doesn't mean we're done, add the last section.
    pieces[c] = data.substr(prevpos);
    return pieces;
}