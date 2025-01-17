/*
 * calendar-xml.cpp
 *   Calendar, weather, and time functions xml functions
 *   ____            _
 *  |  _ \ ___  __ _| |_ __ ___  ___
 *  | |_) / _ \/ _` | | '_ ` _ \/ __|
 *  |  _ <  __/ (_| | | | | | | \__ \
 *  |_| \_\___|\__,_|_|_| |_| |_|___/
 *
 * Permission to use, modify and distribute is granted via the
 *  GNU Affero General Public License v3 or later
 *
 *  Copyright (C) 2007-2021 Jason Mitchell, Randi Mitchell
 *     Contributions by Tim Callahan, Jonathan Hseu
 *  Based on Mordor (C) Brooke Paul, Brett J. Vickers, John P. Freeman
 *
 */

#include <libxml/parser.h>                          // for xmlFreeDoc, xmlCl...
#include <boost/lexical_cast/bad_lexical_cast.hpp>  // for bad_lexical_cast
#include <config.hpp>                               // for Config, gConfig
#include <cstdio>                                   // for sprintf
#include <libxml/xmlstring.h>                       // for BAD_CAST
#include <list>                                     // for list, list<>::con...
#include <memory>                                   // for allocator
#include <ostream>                                  // for basic_ostream::op...

#include "calendar.hpp"                             // for cWeather, Calendar
#include "global.hpp"                               // for FATAL
#include "os.hpp"                                   // for merror
#include "paths.hpp"                                // for PlayerData
#include "proto.hpp"                                // for file_exists
#include "season.hpp"                               // for Season
#include "xml.hpp"                                  // for newStringChild

//*********************************************************************
//                      cWeather
//*********************************************************************

void cWeather::load(xmlNodePtr curNode) {
    xmlNodePtr childNode = curNode->children;

    while(childNode) {
        if(NODE_NAME(childNode, "Sunrise")) xml::copyToString(sunrise, childNode);
        else if(NODE_NAME(childNode, "Sunset")) xml::copyToString(sunset, childNode);
        else if(NODE_NAME(childNode, "EarthTrembles")) xml::copyToString(earthTrembles, childNode);
        else if(NODE_NAME(childNode, "HeavyFog")) xml::copyToString(heavyFog, childNode);
        else if(NODE_NAME(childNode, "BeautifulDay")) xml::copyToString(beautifulDay, childNode);
        else if(NODE_NAME(childNode, "BrightSun")) xml::copyToString(brightSun, childNode);
        else if(NODE_NAME(childNode, "GlaringSun")) xml::copyToString(glaringSun, childNode);
        else if(NODE_NAME(childNode, "Heat")) xml::copyToString(heat, childNode);
        else if(NODE_NAME(childNode, "Still")) xml::copyToString(still, childNode);
        else if(NODE_NAME(childNode, "LightBreeze")) xml::copyToString(lightBreeze, childNode);
        else if(NODE_NAME(childNode, "StrongWind")) xml::copyToString(strongWind, childNode);
        else if(NODE_NAME(childNode, "WindGusts")) xml::copyToString(windGusts, childNode);
        else if(NODE_NAME(childNode, "GaleForce")) xml::copyToString(galeForce, childNode);
        else if(NODE_NAME(childNode, "ClearSkies")) xml::copyToString(clearSkies, childNode);
        else if(NODE_NAME(childNode, "LightClouds")) xml::copyToString(lightClouds, childNode);
        else if(NODE_NAME(childNode, "Thunderheads")) xml::copyToString(thunderheads, childNode);
        else if(NODE_NAME(childNode, "LightRain")) xml::copyToString(lightRain, childNode);
        else if(NODE_NAME(childNode, "HeavyRain")) xml::copyToString(heavyRain, childNode);
        else if(NODE_NAME(childNode, "SheetsRain")) xml::copyToString(sheetsRain, childNode);
        else if(NODE_NAME(childNode, "TorrentRain")) xml::copyToString(torrentRain, childNode);
        else if(NODE_NAME(childNode, "NoMoon")) xml::copyToString(noMoon, childNode);
        else if(NODE_NAME(childNode, "SliverMoon")) xml::copyToString(sliverMoon, childNode);
        else if(NODE_NAME(childNode, "HalfMoon")) xml::copyToString(halfMoon, childNode);
        else if(NODE_NAME(childNode, "WaxingMoon")) xml::copyToString(waxingMoon, childNode);
        else if(NODE_NAME(childNode, "FullMoon")) xml::copyToString(fullMoon, childNode);

        childNode = childNode->next;
    }
}


void cWeather::save(xmlNodePtr curNode) const {
    xml::newStringChild(curNode, "Sunrise", sunrise);
    xml::newStringChild(curNode, "Sunset", sunset);
    xml::newStringChild(curNode, "EarthTrembles", earthTrembles);
    xml::newStringChild(curNode, "HeavyFog", heavyFog);
    xml::newStringChild(curNode, "BeautifulDay", beautifulDay);
    xml::newStringChild(curNode, "BrightSun", brightSun);
    xml::newStringChild(curNode, "GlaringSun", glaringSun);
    xml::newStringChild(curNode, "Heat", heat);
    xml::newStringChild(curNode, "Still", still);
    xml::newStringChild(curNode, "LightBreeze", lightBreeze);
    xml::newStringChild(curNode, "StrongWind", strongWind);
    xml::newStringChild(curNode, "WindGusts", windGusts);
    xml::newStringChild(curNode, "GaleForce", galeForce);
    xml::newStringChild(curNode, "ClearSkies", clearSkies);
    xml::newStringChild(curNode, "LightClouds", lightClouds);
    xml::newStringChild(curNode, "Thunderheads", thunderheads);
    xml::newStringChild(curNode, "LightRain", lightRain);
    xml::newStringChild(curNode, "HeavyRain", heavyRain);
    xml::newStringChild(curNode, "SheetsRain", sheetsRain);
    xml::newStringChild(curNode, "TorrentRain", torrentRain);
    xml::newStringChild(curNode, "NoMoon", noMoon);
    xml::newStringChild(curNode, "SliverMoon", sliverMoon);
    xml::newStringChild(curNode, "HalfMoon", halfMoon);
    xml::newStringChild(curNode, "WaxingMoon", waxingMoon);
    xml::newStringChild(curNode, "FullMoon", fullMoon);
}


//*********************************************************************
//                      cDay
//*********************************************************************

void cDay::save(xmlNodePtr curNode) const {
    xml::saveNonZeroNum(curNode, "Year", year);
    xml::saveNonZeroNum(curNode, "Month", month);
    xml::saveNonZeroNum(curNode, "Day", day);
}

void cDay::load(xmlNodePtr curNode) {
    xmlNodePtr childNode = curNode->children;

    while(childNode) {
        if(NODE_NAME(childNode, "Day")) xml::copyToNum(day, childNode);
        else if(NODE_NAME(childNode, "Month")) xml::copyToNum(month, childNode);
        else if(NODE_NAME(childNode, "Year")) xml::copyToNum(year, childNode);
        childNode = childNode->next;
    }
}

//*********************************************************************
//                      cMonth
//*********************************************************************

void cMonth::save(xmlNodePtr curNode) const {
    xml::newNumProp(curNode, "id", id);
    xml::saveNonNullString(curNode, "Name", name);
    xml::saveNonZeroNum(curNode, "Days", days);
}

void cMonth::load(xmlNodePtr curNode) {
    xmlNodePtr childNode = curNode->children;

    id = xml::getIntProp(curNode, "id");
    while(childNode) {
        if(NODE_NAME(childNode, "Days")) xml::copyToNum(days, childNode);
        else if(NODE_NAME(childNode, "Name")) xml::copyToString(name, childNode);
        childNode = childNode->next;
    }
}

//*********************************************************************
//                      cSeason
//*********************************************************************


void cSeason::save(xmlNodePtr curNode) const {
    xml::newNumProp(curNode, "id", (int)id);
    xml::saveNonNullString(curNode, "Name", name);
    xml::saveNonZeroNum(curNode, "Month", month);
    xml::saveNonZeroNum(curNode, "Day", day);

    xmlNodePtr childNode = xml::newStringChild(curNode, "Weather");
    weather.save(childNode);
}

void cSeason::load(xmlNodePtr curNode) {
    xmlNodePtr childNode = curNode->children;

    id = (Season)xml::getIntProp(curNode, "id");
    while(childNode) {
        if(NODE_NAME(childNode, "Month")) xml::copyToNum(month, childNode);
        else if(NODE_NAME(childNode, "Day")) xml::copyToNum(day, childNode);
        else if(NODE_NAME(childNode, "Name")) xml::copyToString(name, childNode);
        else if(NODE_NAME(childNode, "Weather")) {
            weather.load(childNode);
        }
        childNode = childNode->next;
    }
}


//*********************************************************************
//                      Calendar
//*********************************************************************


void Calendar::save() const {
    xmlDocPtr   xmlDoc;
    xmlNodePtr      rootNode, curNode, childNode;
    char            filename[80];

    // we only save if we're up on the main port
    if(gConfig->getPortNum() != 3333)
        return;

    xmlDoc = xmlNewDoc(BAD_CAST "1.0");
    rootNode = xmlNewDocNode(xmlDoc, nullptr, BAD_CAST "Calendar", nullptr);
    xmlDocSetRootElement(xmlDoc, rootNode);

    xml::saveNonZeroNum(rootNode, "TotalDays", totalDays);
    // only save ship updates if we're inaccurate
    //if(shipUpdates != gConfig->expectedShipUpdates())
    //  xml::saveNonZeroInt(rootNode, "ShipUpdates", gConfig->expectedShipUpdates() - shipUpdates);
    xml::newStringChild(rootNode, "LastPirate", lastPirate);

    curNode = xml::newStringChild(rootNode, "Current");
    xml::saveNonZeroNum(curNode, "Year", curYear);
    xml::saveNonZeroNum(curNode, "Month", curMonth);
    xml::saveNonZeroNum(curNode, "Day", curDay);
    xml::saveNonZeroNum(curNode, "Hour", gConfig->currentHour());

    curNode = xml::newStringChild(rootNode, "Seasons");
    std::list<cSeason*>::const_iterator st;
    for(st = seasons.begin() ; st != seasons.end() ; st++) {
        childNode = xml::newStringChild(curNode, "Season");
        (*st)->save(childNode);
    }

    curNode = xml::newStringChild(rootNode, "Months");
    std::list<cMonth*>::const_iterator mt;
    for(mt = months.begin() ; mt != months.end() ; mt++) {
        childNode = xml::newStringChild(curNode, "Month");
        (*mt)->save(childNode);
    }

    sprintf(filename, "%s/calendar.xml", Path::PlayerData);

    xml::saveFile(filename, xmlDoc);
    xmlFreeDoc(xmlDoc);
}


void Calendar::loadCurrent(xmlNodePtr curNode) {
    xmlNodePtr childNode = curNode->children;

    while(childNode) {
        if(NODE_NAME(childNode, "Year")) xml::copyToNum(curYear, childNode);
        else if(NODE_NAME(childNode, "Month")) xml::copyToNum(curMonth, childNode);
        else if(NODE_NAME(childNode, "Day")) xml::copyToNum(curDay, childNode);
        else if(NODE_NAME(childNode, "Hour")) xml::copyToNum(adjHour, childNode);
        childNode = childNode->next;
    }
}

void Calendar::loadSeasons(xmlNodePtr curNode) {
    xmlNodePtr childNode = curNode->children;
    cSeason* season=nullptr;

    while(childNode) {
        if(NODE_NAME(childNode, "Season")) {
            season = new cSeason;
            season->load(childNode);
            seasons.push_back(season);
        }
        childNode = childNode->next;
    }
}

void Calendar::loadMonths(xmlNodePtr curNode) {
    xmlNodePtr childNode = curNode->children;
    cMonth* month=nullptr;

    while(childNode) {
        if(NODE_NAME(childNode, "Month")) {
            month = new cMonth;
            month->load(childNode);
            months.push_back(month);
        }
        childNode = childNode->next;
    }
}


void Calendar::load() {
    xmlDocPtr   xmlDoc;
    xmlNodePtr  rootNode;
    xmlNodePtr  curNode;
    char        filename[80];

    sprintf(filename, "%s/calendar.xml", Path::PlayerData);

    if(!file_exists(filename))
        merror("Unable to find calendar file", FATAL);

    if((xmlDoc = xml::loadFile(filename, "Calendar")) == nullptr)
        merror("Unable to read calendar file", FATAL);

    rootNode = xmlDocGetRootElement(xmlDoc);
    curNode = rootNode->children;


    while(curNode) {
        if(NODE_NAME(curNode, "TotalDays")) xml::copyToNum(totalDays, curNode);
        else if(NODE_NAME(curNode, "ShipUpdates")) xml::copyToNum(shipUpdates, curNode);
        else if(NODE_NAME(curNode, "LastPirate")) {
            xml::copyToString(lastPirate, curNode);
        } else if(NODE_NAME(curNode, "Current"))
            loadCurrent(curNode);
        else if(NODE_NAME(curNode, "Seasons"))
            loadSeasons(curNode);
        else if(NODE_NAME(curNode, "Months"))
            loadMonths(curNode);
        curNode = curNode->next;
    }

    setSeason();

    xmlFreeDoc(xmlDoc);
    xmlCleanupParser();
}



