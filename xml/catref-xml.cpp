/*
 * catRefInfo-xml.h
 *   Category Reference info - xml
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
#include <cstdio>                                   // for snprintf, sprintf
#include <libxml/xmlstring.h>                       // for BAD_CAST
#include <list>                                     // for list, operator==
#include <map>                                      // for map, map<>::const...
#include <ostream>                                  // for basic_ostream::op...
#include <string>                                   // for allocator, string
#include <utility>                                  // for pair

#include "calendar.hpp"                             // for cSeason
#include "catRefInfo.hpp"                           // for CatRefInfo
#include "config.hpp"                               // for Config, gConfig
#include "paths.hpp"                                // for Game
#include "season.hpp"                               // for Season
#include "xml.hpp"                                  // for copyToNum, copyTo...


//*********************************************************************
//                      load
//*********************************************************************

void CatRefInfo::load(xmlNodePtr rootNode) {
    xmlNodePtr curNode = rootNode->children;

    id = xml::getIntProp(rootNode, "id");
    while(curNode) {
        if(NODE_NAME(curNode, "Area")) xml::copyToString(area, curNode);
        else if(NODE_NAME(curNode, "Name")) xml::copyToString(name, curNode);
        else if(NODE_NAME(curNode, "Fishing")) xml::copyToString(fishing, curNode);
        else if(NODE_NAME(curNode, "WorldName")) xml::copyToString(worldName, curNode);
        else if(NODE_NAME(curNode, "YearsSince")) xml::copyToString(yearsSince, curNode);
        else if(NODE_NAME(curNode, "Parent")) xml::copyToString(parent, curNode);
        else if(NODE_NAME(curNode, "Limbo")) xml::copyToNum(limbo, curNode);
        else if(NODE_NAME(curNode, "YearOffset")) xml::copyToNum(yearOffset, curNode);
        else if(NODE_NAME(curNode, "Recall")) xml::copyToNum(recall, curNode);
        else if(NODE_NAME(curNode, "TeleportWeight")) xml::copyToNum(teleportWeight, curNode);
        else if(NODE_NAME(curNode, "TeleportZone")) xml::copyToNum(teleportZone, curNode);
        else if(NODE_NAME(curNode, "TrackZone")) xml::copyToNum(trackZone, curNode);
        else if(NODE_NAME(curNode, "Seasons")) loadSeasons(curNode);
        curNode = curNode->next;
    }

    // this would make an infinite loop
    if(parent == area)
        parent = "";
}

//*********************************************************************
//                      save
//*********************************************************************

void CatRefInfo::save(xmlNodePtr curNode) const {
    xmlNodePtr deepNode,subNode,childNode = xml::newStringChild(curNode, "Info");
    xml::newNumProp(childNode, "id", id);

    xml::saveNonNullString(childNode, "Area", area);
    xml::saveNonNullString(childNode, "Name", name);
    xml::saveNonNullString(childNode, "WorldName", worldName);
    xml::saveNonNullString(childNode, "YearsSince", yearsSince);
    xml::saveNonNullString(childNode, "Parent", parent);
    xml::saveNonZeroNum(childNode, "Limbo", limbo);
    xml::saveNonZeroNum(childNode, "YearOffset", yearOffset);
    xml::saveNonZeroNum(childNode, "Recall", recall);
    xml::saveNonZeroNum(childNode, "TeleportWeight", teleportWeight);
    xml::saveNonZeroNum(childNode, "TeleportZone", teleportZone);
    xml::saveNonZeroNum(childNode, "Track", trackZone);

    subNode = xml::newStringChild(childNode, "Seasons");
    std::map<Season,cSeason*>::const_iterator st;
    for(st = seasons.begin() ; st != seasons.end() ; st++) {
        deepNode = xml::newStringChild(subNode, "Season");
        (*st).second->save(deepNode);
    }
}

//*********************************************************************
//                      loadSeasons
//*********************************************************************

void CatRefInfo::loadSeasons(xmlNodePtr curNode) {
    xmlNodePtr childNode = curNode->children;
    cSeason* season=nullptr;

    while(childNode) {
        if(NODE_NAME(childNode, "Season")) {
            season = new cSeason;
            season->load(childNode);
            if(seasons[season->getId()])
                delete seasons[season->getId()];
            seasons[season->getId()] = season;
        }
        childNode = childNode->next;
    }
}


//*********************************************************************
//                      loadCatRefInfo
//*********************************************************************

bool Config::loadCatRefInfo() {
    xmlDocPtr xmlDoc;
    xmlNodePtr curNode;
    CatRefInfo *cri=nullptr;
    const CatRefInfo *parent=nullptr;

    char filename[80];
    snprintf(filename, 80, "%s/catRefInfo.xml", Path::Game);
    xmlDoc = xml::loadFile(filename, "CatRefInfo");

    if(xmlDoc == nullptr)
        return(false);

    curNode = xmlDocGetRootElement(xmlDoc);

    std::string d = "";
    xml::copyPropToString(d, curNode, "default");
    if(!d.empty())
        gConfig->setDefaultArea(d);

    curNode = curNode->children;
    while(curNode && xmlIsBlankNode(curNode))
        curNode = curNode->next;

    if(curNode == nullptr) {
        xmlFreeDoc(xmlDoc);
        return(false);
    }

    clearCatRefInfo();
    while(curNode != nullptr) {
        if(NODE_NAME(curNode, "Info")) {
            cri = new CatRefInfo;
            cri->load(curNode);
            catRefInfo.push_back(cri);
        }
        curNode = curNode->next;
    }
    xmlFreeDoc(xmlDoc);
    xmlCleanupParser();

    // propogate: this happens once, so if we ever need to know info from the
    // parent, we never need to look for it
    for(const auto& crj : catRefInfo) {
        if(!crj->getParent().empty()) {
            parent = getCatRefInfo(crj->getParent(), 0, true);
            if(parent)
                crj->copyVars(parent);
        }
    }
    return(true);
}


//*********************************************************************
//                      saveCatRefInfo
//*********************************************************************

void Config::saveCatRefInfo() const {
    std::list<CatRefInfo*>::const_iterator it;
    xmlDocPtr   xmlDoc;
    xmlNodePtr  rootNode;
    char            filename[80];

    xmlDoc = xmlNewDoc(BAD_CAST "1.0");
    rootNode = xmlNewDocNode(xmlDoc, nullptr, BAD_CAST "CatRefInfo", nullptr);
    xmlDocSetRootElement(xmlDoc, rootNode);
    xml::newProp(rootNode, "default", defaultArea.c_str());

    for(it = catRefInfo.begin() ; it != catRefInfo.end() ; it++)
        (*it)->save(rootNode);

    sprintf(filename, "%s/catRefInfo.xml", Path::Game);
    xml::saveFile(filename, xmlDoc);
    xmlFreeDoc(xmlDoc);
}


