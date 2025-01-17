/*
 * watchers.cpp
 *   Watcher commands.
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

#include <cstring>                     // for strcpy
#include <map>                         // for operator==, _Rb_tree_iterator
#include <string>                      // for allocator, string, char_traits
#include <utility>                     // for pair

#include "cmd.hpp"                     // for cmd
#include "commands.hpp"                // for cmdNoExist, cmdNoAuth, cmdProcess
#include "flags.hpp"                   // for R_TYPO, P_WATCHER
#include "mudObjects/players.hpp"      // for Player
#include "mudObjects/rooms.hpp"        // for BaseRoom
#include "mudObjects/uniqueRooms.hpp"  // for UniqueRoom
#include "proto.hpp"                   // for broadcast, up, log_immort, nee...
#include "server.hpp"                  // for Server, gServer, PlayerMap
#include "stats.hpp"                   // for Stat


//*********************************************************************
//                      dmCheckStats
//*********************************************************************

int dmCheckStats(Player* player, cmd* cmnd) {
    Player  *target=nullptr;

    if(!player->isStaff() && !player->isWatcher())
        return(cmdNoExist(player, cmnd));
    if(!player->isWatcher())
        return(cmdNoAuth(player));

    if(cmnd->num < 2) {
        player->print("Check who's stats?\n");
        return(0);
    }

    cmnd->str[1][0]=up(cmnd->str[1][0]);
    target = gServer->findPlayer(cmnd->str[1]);
    if(!target || !player->canSee(target)) {
        player->print("Player not online.\n");
        return(0);
    }

    // Used by watchers to see if somebody is a newbie
    if(target->getLevel() > 7 && !player->isCt()) {
        player->print("Player must be less than level 8.\n");
        return(0);
    }

    player->print("%s's Initial Stats:\n", target->getCName());
    player->print("STR.....%d\n", target->strength.getInitial());
    player->print("DEX.....%d\n", target->dexterity.getInitial());
    player->print("CON.....%d\n", target->constitution.getInitial());
    player->print("INT.....%d\n", target->intelligence.getInitial());
    player->print("PIE.....%d\n", target->piety.getInitial());
    return(0);
}


//*********************************************************************
//                      dmLocatePlayer
//*********************************************************************

int dmLocatePlayer(Player* player, cmd* cmnd) {
    Player  *target=nullptr;


    if(!player->isStaff() && !player->isWatcher())
        return(cmdNoExist(player, cmnd));
    if(!player->isWatcher())
        return(cmdNoAuth(player));

    if(cmnd->num < 2) {
        player->print("Locate who?\n");
        return(0);
    }

    cmnd->str[1][0]=up(cmnd->str[1][0]);
    target = gServer->findPlayer(cmnd->str[1]);
    if(!target || !player->canSee(target)) {
        player->print("Player not online.\n");
        return(0);
    }

    // Used by watchers to see where a newbie is
    if(target->getLevel() > 3 && !player->isCt()) {
        player->print("Player must be less than level 4.\n");
        return(0);
    }

    player->print("%s is in the following room: %s\n", target->getCName(), target->getRoomParent()->fullName().c_str());
    return(0);
}


//*********************************************************************
//                      dmWatcherBroad
//*********************************************************************

int dmWatcherBroad(Player *admin, cmd* cmnd) {
    std::string text = "";
    Player  *watcher=nullptr;
    int     found=0;

    text = getFullstrText(cmnd->fullstr, 1);
    if(text.empty()) {
        admin->print("Broadcast what?\n");
        return(0);
    }

    found = 0;
    Player *ply;
    for(const auto& p : gServer->players) {
        ply = p.second;

        if(!ply->isConnected())
            continue;

        if(ply->flagIsSet(P_WATCHER)) {
            watcher = ply;
            found++;
            break;
        }
    }

    if(!found) {
        admin->print("No watchers were found to broadcast your message.\n");
        return(0);
    }
    broadcast(isDm, "^g*** %s forced %s to broadcast", admin->getCName(), watcher->getCName());

    text = "broadcast " + text;
    strcpy(cmnd->str[0], "broadcast");
    cmnd->fullstr = text;
    cmdProcess(watcher, cmnd);

    log_immort(true, admin, "%s made %s broadcast \"%s\"\n", admin->getCName(), watcher->getCName(), text.c_str());
    return(0);
}


//*********************************************************************
//                      reportTypo
//*********************************************************************

int reportTypo(Player* player, cmd* cmnd) {
    if(player->isStaff()) {
        player->printColor("^YDon't report typos - fix them!\n");
        return(0);
    }

    if(!player->isWatcher())
        return(cmdNoExist(player, cmnd));
    if(!needUniqueRoom(player))
        return(0);

    if(player->getConstRoomParent()->flagIsSet(R_TYPO)) {
        player->printColor("^YA typo has already been reported in this room.\n");
    } else {
        player->getUniqueRoomParent()->setFlag(R_TYPO);
        player->printColor("^YTypo reported!\n");
        broadcast(isCt, "^Y*** %s reported a typo in room %s.", player->getCName(), player->getRoomParent()->fullName().c_str());
    }
    return(0);
}
