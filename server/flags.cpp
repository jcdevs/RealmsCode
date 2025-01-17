/*
 * flags.cpp
 *   Flags storage file
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

#include <fmt/format.h>                        // for format
#include <libxml/parser.h>                     // for xmlFreeDoc, xmlCleanup...
#include <unistd.h>                            // for link
#include <boost/algorithm/string/replace.hpp>  // for replace_all
#include <boost/iterator/iterator_traits.hpp>  // for iterator_value<>::type
#include <cstdio>                              // for sprintf, snprintf
#include <deque>                               // for _Deque_iterator
#include <fstream>                             // for operator<<, basic_ostream
#include <iomanip>                             // for operator<<, setw
#include <locale>                              // for locale
#include <map>                                 // for operator==, map<>::con...
#include <string>                              // for string, operator<<
#include <string_view>                         // for operator<<, string_view
#include <utility>                             // for pair

#include "config.hpp"                          // for Config, MudFlagMap
#include "paths.hpp"                           // for BuilderHelp, DMHelp, Code
#include "proto.hpp"                           // for wrapText
#include "structs.hpp"                         // for MudFlag
#include "xml.hpp"                             // for NODE_NAME, copyPropToS...

//*********************************************************************
//                      MudFlag
//*********************************************************************

MudFlag::MudFlag() {
    id = 0;
    name = desc = "";
}


//**********************************************************************
//                      flagFileTitle
//**********************************************************************

std::string flagFileTitle(std::string_view title, const char* file, int pad) {
    std::ostringstream oStr;

    // Prepare the text to go into the flag index helpfile
    oStr.setf(std::ios::left, std::ios::adjustfield);
    oStr.imbue(std::locale(""));

    oStr << "  " << std::setw(pad) << fmt::format("{} flags:", title) << " ^W" << file << "s^x\n";
    return(oStr.str());
}

//**********************************************************************
//                      writeFlagFile
//**********************************************************************

std::string writeFlagFile(std::string_view title, const char* file, int pad, bool builderLink, const MudFlagMap &flags) {
    std::ostringstream padding;
    MudFlagMap::const_iterator it;
    const MudFlag *flag=nullptr;
    std::string desc = "", output = "\n";
    char    dmfile[80], dmfileLink[80];
    char    bhfile[80], bhfileLink[80];

    // Figure out pathing information for the flags helpfiles we're working with
    sprintf(dmfile, "%s/%ss.txt", Path::DMHelp, file);
    sprintf(dmfileLink, "%s/%s.txt", Path::DMHelp, file);
    if(builderLink) {
        sprintf(bhfile, "%s/%ss.txt", Path::BuilderHelp, file);
        sprintf(bhfileLink, "%s/%s.txt", Path::BuilderHelp, file);
    }
    std::ofstream out(dmfile);

    // set left aligned
    out.setf(std::ios::left, std::ios::adjustfield);
    out.imbue(std::locale(""));
    out << "^W" << title << " Flags^x\n";

    padding << std::setw(34) << " ";
    output += padding.str();

    // Go through all the flags and add them to the helpfile
    for(it = flags.begin(); it != flags.end(); it++) {
        flag = &(*it).second;
        out << " " << std::setw(10) << flag->id;

        if(flag->name == "Unused") {
            out << "^y" << flag->name << "^x\n";
        } else {
            desc = wrapText(flag->desc, 45);
            boost::replace_all(desc, "\n", output.c_str());

            out << std::setw(20) << flag->name << "   " << desc << "\n";
        }
    }

    // Create the actual helpfiles
    out << "\n";
    out.close();
    link(dmfile, dmfileLink);
    if(builderLink) {
        link(dmfile, bhfile);
        link(dmfile, bhfileLink);
    }

    return(flagFileTitle(title, file, pad));
}

//**********************************************************************
//                      writeFlagFiles
//**********************************************************************

void writeFlagFiles() {
    char    dmfile[100], dmfileLink[100];
    char    bhfile[100], bhfileLink[100];
    int     pad=0;

    // Figure out pathing information for the general flags helpfiles
    sprintf(dmfile, "%s/flags.txt", Path::DMHelp);
    sprintf(dmfileLink, "%s/flag.txt", Path::DMHelp);
    sprintf(bhfile, "%s/flags.txt", Path::BuilderHelp);
    sprintf(bhfileLink, "%s/flag.txt", Path::BuilderHelp);

    pad = 15;

    // Put together the standard flags we're used to
    std::string mflags = writeFlagFile("Monster", "mflag", pad, true, gConfig->mflags);
    std::string oflags = writeFlagFile("Object", "oflag", pad, true, gConfig->oflags);
    std::string rflags = writeFlagFile("Room", "rflag", pad, true, gConfig->rflags);
    std::string pflags = writeFlagFile("Player", "pflag", pad, false, gConfig->pflags);
    std::string xflags = writeFlagFile("Exit", "xflag", pad, true, gConfig->xflags);
    // actual spell help files are written to file in writeSpellFiles
    std::string sflags = flagFileTitle("Spell", "sflag", pad);

    pad = 24;

    // Put together the newer flags we're not used to seeing
    std::string saflags = writeFlagFile("Special Attack", "saflag", pad, true, gConfig->specialFlags);
    std::string stflags = writeFlagFile("Property Storage", "stflag", pad, false, gConfig->propStorFlags);
    std::string shflags = writeFlagFile("Property Shop", "shflag", pad, false, gConfig->propShopFlags);
    std::string hflags = writeFlagFile("Property House", "hflag", pad, false, gConfig->propHouseFlags);
    std::string gflags = writeFlagFile("Property Guild", "gflag", pad, false, gConfig->propGuildFlags);

    // Put together the index of flags that CT+ can see
    std::ofstream out;
    out.open(dmfile);
    out << "Available flag help files:\n";
    out << mflags << oflags << rflags << pflags << xflags << sflags << "\n"
            << saflags << stflags << shflags << hflags << gflags << "\n";
    out.close();

    // Put together the index of flags that builders can see
    out.open(bhfile);
    out << "Available flag help files:\n";
    out << mflags << oflags << rflags << xflags << sflags << "\n" << saflags << "\n";
    out.close();

    link(dmfile, dmfileLink);
    link(bhfile, bhfileLink);
}

//**********************************************************************
//                      loadFlags
//**********************************************************************

bool Config::loadFlags() {
    xmlDocPtr xmlDoc;
    xmlNodePtr curNode, childNode;

    char filename[80];
    snprintf(filename, 80, "%s/flags.xml", Path::Code);
    xmlDoc = xml::loadFile(filename, "Flags");

    if(xmlDoc == nullptr)
        return(false);

    curNode = xmlDocGetRootElement(xmlDoc);

    curNode = curNode->children;
    while(curNode && xmlIsBlankNode(curNode))
        curNode = curNode->next;

    if(curNode == nullptr) {
        xmlFreeDoc(xmlDoc);
        return(false);
    }

    clearFlags();
    while(curNode != nullptr) {
        MudFlagMap* flagmap;
        if(NODE_NAME(curNode, "Rooms")) {
            flagmap = &rflags;
        } else if(NODE_NAME(curNode, "Exits")) {
            flagmap = &xflags;
        } else if(NODE_NAME(curNode, "Players")) {
            flagmap = &pflags;
        } else if(NODE_NAME(curNode, "Monsters")) {
            flagmap = &mflags;
        } else if(NODE_NAME(curNode, "Objects")) {
            flagmap = &oflags;
        } else if(NODE_NAME(curNode, "SpecialAttacks")) {
            flagmap = &specialFlags;
        } else if(NODE_NAME(curNode, "PropStorage")) {
            flagmap = &propStorFlags;
        } else if(NODE_NAME(curNode, "PropShop")) {
            flagmap = &propShopFlags;
        } else if(NODE_NAME(curNode, "PropHouse")) {
            flagmap = &propHouseFlags;
        } else if(NODE_NAME(curNode, "PropGuild")) {
            flagmap = &propGuildFlags;
        } else {
            flagmap = nullptr;
        }

        if(flagmap != nullptr) {
            childNode = curNode->children;
            while(childNode) {
                if(NODE_NAME(childNode, "Flag")) {
                    MudFlag f;
                    f.id = xml::getIntProp(childNode, "id");
                    xml::copyPropToString(f.name, childNode, "Name");
                    xml::copyToString(f.desc, childNode);

                    (*flagmap)[f.id] = f;

                }
                childNode = childNode->next;
            }
        }
        curNode = curNode->next;
    }
    xmlFreeDoc(xmlDoc);
    xmlCleanupParser();

    writeFlagFiles();
    return(true);
}

//**********************************************************************
//                      clearFlags
//**********************************************************************

void Config::clearFlags() {
    rflags.clear();
    xflags.clear();
    pflags.clear();
    mflags.clear();
    oflags.clear();
    specialFlags.clear();
    propStorFlags.clear();
    propShopFlags.clear();
    propHouseFlags.clear();
    propGuildFlags.clear();
}

