/*
 * swap.cpp
 *   Code to swap rooms/monsters/objects between areas
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

#include <dirent.h>                            // for dirent, opendir, readdir
#include <fmt/format.h>                        // for format
#include <unistd.h>                            // for close, read, unlink
#include <boost/algorithm/string/trim.hpp>     // for trim
#include <boost/iterator/iterator_facade.hpp>  // for operator!=, operator++
#include <boost/token_functions.hpp>           // for char_separator
#include <boost/token_iterator.hpp>            // for token_iterator
#include <boost/tokenizer.hpp>                 // for tokenizer<>::iter
#include <cctype>                              // for isupper
#include <csignal>                             // for kill
#include <cstdio>                              // for printf, size_t
#include <cstdlib>                             // for atoi, exit
#include <cstring>                             // for memset, strlen
#include <list>                                // for list, operator==, list...
#include <map>                                 // for operator==, map, _Rb_t...
#include <set>                                 // for set
#include <string>                              // for string, basic_string
#include <string_view>                         // for string_view, operator==
#include <utility>                             // for pair, move

#include "anchor.hpp"                          // for Anchor
#include "area.hpp"                            // for Area, AreaZone, MapMarker
#include "async.hpp"                           // for Async, AsyncExternal
#include "catRef.hpp"                          // for CatRef
#include "catRefInfo.hpp"                      // for CatRefInfo, CatRefInfo...
#include "cmd.hpp"                             // for cmd
#include "commands.hpp"                        // for getFullstrText, getFul...
#include "config.hpp"                          // for Config, gConfig
#include "dm.hpp"                              // for findNextEmpty, dmMobSwap
#include "enums/loadType.hpp"                  // for LoadType, LoadType::LS...
#include "flags.hpp"                           // for R_SHOP, X_LOCKABLE
#include "free_crt.hpp"                        // for free_crt
#include "global.hpp"                          // for CreatureClass, Creatur...
#include "hooks.hpp"                           // for Hooks
#include "location.hpp"                        // for Location
#include "mud.hpp"                             // for BODYPART_OBJ, CORPSE_OBJ
#include "mudObjects/areaRooms.hpp"            // for AreaRoom
#include "mudObjects/container.hpp"            // for MonsterSet
#include "mudObjects/exits.hpp"                // for Exit
#include "mudObjects/monsters.hpp"             // for Monster
#include "mudObjects/mudObject.hpp"            // for MudObject
#include "mudObjects/objects.hpp"              // for Object
#include "mudObjects/players.hpp"              // for Player
#include "mudObjects/rooms.hpp"                // for ExitList
#include "mudObjects/uniqueRooms.hpp"          // for UniqueRoom
#include "paths.hpp"                           // for Monster, UniqueRoom
#include "proc.hpp"                            // for childProcess, ChildType
#include "proto.hpp"                           // for getCatRef, broadcast
#include "server.hpp"                          // for Server, gServer, RoomC...
#include "ships.hpp"                           // for ShipExit, ShipRaid
#include "startlocs.hpp"                       // for StartLoc
#include "swap.hpp"                            // for Swap, SwapRoom, SwapOb...
#include "tokenizer.hpp"                       // for charTokenizer
#include "utils.hpp"                           // for MAX
#include "xml.hpp"                             // for loadPlayer, loadRoom


const char* sepType = " ";
#define SWAP_QUEUE_LIMIT    100

//*********************************************************************
//                          Swap
//*********************************************************************
// The swap process works like this.
//      1. Player requests to swap a mud object with another.
//          a. Checks to make sure the mud object can be swapped.
//      2. If an area is specified, fork and find the next available slot in that area.
//      3. Checks to make sure target mud object can be swapped.
//      4. Fork to find all affected mud objects.
//          a. The fork sends the info to the main mud as soon as it finds it.
//          b. On the main mud, any mud object that is saved or modified from start to
//             finish of this process will be inspected and put in the queue if they're
//             interesting.
//      5. As the main mud receives info from the fork, it puts it into the queue.
//          a. Mud objects received will be loaded into memory immediately to prevent
//             many mud objects loading at once at the end of the process.
//      6. Once the fork finishes its search, it loops through everything found as well
//         as everything already in memory:
//          a. Areas (zones)
//          b. StartLocs
//          c. Ships
//          d. CatRefInfo
//         Everything is updated and saved to disk.
//         Property is not examined for room swap because all rooms belong to restricted ranges.
//
//  If another swap is attempted, it will enter a queue. Once the first process is
//  complete, the next one will execute.
//

//*********************************************************************
//                          swapName
//*********************************************************************

std::string swapName(SwapType type) {
    if(type == SwapRoom)
        return("room");
    if(type == SwapObject)
        return("object");
    if(type == SwapMonster)
        return("monster");
    return("");
}

//*********************************************************************
//                          swap
//*********************************************************************

void swap(Player* player, cmd* cmnd, SwapType type) {
    std::string str = getFullstrText(cmnd->fullstr, 1);
    std::string name="";
    std::string cmd = " *swap";
    char padding[50];
    int queueSize = gConfig->swapQueueSize();

    Swap s;
    s.player = player->getName();
    s.type = type;

    std::string swapType = "";
    if(type == SwapRoom) {
        swapType = "room";
        cmd = "*rswap";
    } else if(type == SwapObject) {
        swapType = "object";
        cmd = "*oswap";
    } else if(type == SwapMonster) {
        swapType = "monster";
        cmd = "*mswap";
    }
    swapType = swapName(type);

    if(str.empty()) {
        std::string prefix = "  ^e";
        prefix += cmd;

        if(type == SwapObject) {
            prefix += " [object area.id]";
        } else if(type == SwapMonster) {
            prefix += " [monster area.id]";
        }

        player->print("Commands:\n");
        if(type == SwapNone) {
            player->printColor("  ^e*rswap [area]      ^x- swap rooms\n");
            player->printColor("  ^e*oswap [area]      ^x- swap objects\n");
            player->printColor("  ^e*mswap [area]      ^x- swap monsters\n");
        } else {
            player->printColor("%s [area]      ^x- move to next open %s in [area].\n", prefix.c_str(), swapType.c_str());
            player->printColor("%s [area.id]   ^x- move to specific %s (will swap if %s exists).\n", prefix.c_str(), swapType.c_str(), swapType.c_str());
            player->printColor("%s [id]        ^x- move to specific %s (will swap if %s exists).\n", prefix.c_str(), swapType.c_str(), swapType.c_str());
            player->printColor("  ^e%s [^x-range source target loop^e]\n", cmd.c_str());
            player->printColor("         ^e[^x-range source.low:high target^e]\n");
            memset(padding, ' ', sizeof(padding));
            padding[16 + prefix.length() - 2] = '\0';
            player->print("%s- swap a range of %ss.\n", padding, swapType.c_str());
        }
        player->printColor("%s [^x-info^e]     ^x- print swap info.\n", prefix.c_str());
        player->printColor("%s [^x-cancel #^e] ^x- cancel a swap job, # is 1 by default.\n", prefix.c_str());
        player->printColor("%s [^x-abort^e]    ^x- abort all swap jobs (empty queue).\n", prefix.c_str());
        return;
    } else if(str == "-info") {
        gConfig->swapInfo(player);
        gServer->swapInfo(player);
        return;
    } else if(str.starts_with("-range") && type != SwapNone) {
        std::string o = getFullstrText(cmnd->fullstr, 2);
        getCatRef(o, &s.origin, nullptr);
        getCatRef(getFullstrText(cmnd->fullstr, 3), &s.target, nullptr);

        player->printColor("^YRS: ^xLooking to swap %s ranges starting at %s to %s.\n", swapType.c_str(),
            s.origin.rstr().c_str(), s.target.id == -1 ? s.target.area.c_str() : s.target.rstr().c_str());

        int loop=0;
        std::string::size_type pos = o.find(":");
        if(pos != std::string::npos) {
            // *rswap -range misc.100:120 test.1
            loop = atoi(o.substr(pos+1).c_str()) - s.origin.id;
        } else {
            // *rswap -range misc.100 test.1 20
            loop = atoi(getFullstrText(cmnd->fullstr, 4).c_str());
        }
        loop = MAX(0, loop);

        if(!loop) {
            player->printColor("^YRS: ^RError: ^xNo upper bound found: which %ss would you like to loop over?\n", swapType.c_str());
            return;
        }

        if(!gConfig->swapChecks(player, s))
            return;

        loop++;
        player->printColor("^YRS: ^xAdding %d job%s to the queue... ", loop, loop != 1 ? "s" : "");

        if(loop + queueSize > SWAP_QUEUE_LIMIT) {
            player->printColor("\n^YRS: ^RError: ^xThe queue has a limit of %d; you may only add %d more.\n",
                SWAP_QUEUE_LIMIT, SWAP_QUEUE_LIMIT - queueSize);
            return;
        }

        loop = s.origin.id + loop;
        for(; s.origin.id < loop; s.origin.id++) {
            gConfig->swapAddQueue(s);
            if(s.target.id != -1)
                s.target.id++;
        }
        player->print("done.\n");

        // begin if nothing has started!
        if(!gConfig->isSwapping())
            gConfig->swapNextInQueue();
        return;
    }

    if(gConfig->isSwapping()) {
        name = gServer->swapName();
        if(str == "-abort") {
            broadcast(isStaff, "^YRS: Swap queue has been cleared.");
            gConfig->swapAbort();
            return;
        } if(str.starts_with("-cancel")) {
            int id = atoi(getFullstrText(cmnd->fullstr, 2).c_str());
            if(id <= 1)
                id = 1;
            player->printColor("^YRS: ^eSwap canceled.\n");
            if(name != player->getName() && name != "Someone") {
                Player* p = gServer->findPlayer(name.c_str());
                if(p)
                    player->printColor("^RRS: ^eSwap canceled by %s.\n", player->getCName());
            }
            if(id==1)
                gServer->endSwap();
            gConfig->endSwap(id);
            return;
        }
    }

    if(str.at(0) == '-') {
        player->printColor("^YRS: ^RError: ^xFlag not understood.\n");
        return;
    }

    if(type == SwapRoom) {
        if(!needUniqueRoom(player))
            return;
        if(!player->checkBuilder(player->getUniqueRoomParent())) {
            player->printColor("^YRS: ^RError: ^xRoom number not inside any of your alotted ranges.\n");
            return;
        }

        s.origin = player->getConstUniqueRoomParent()->info;
    } else {
        getCatRef(str, &s.origin, player);
        str = getFullstrText(str, 1);
    }

    if(!s.origin.id) {
        player->printColor("^YRS: ^RError: ^xUnable to locate something to swap.\n");
        return;
    }

    getCatRef(str, &s.target, player);

    // these checks done here for convenience of user,
    // not to ensure data validity: this is why the checks
    // are run again in Server::swap
    if(!gConfig->swapChecks(player, s))
        return;

    if(type == SwapRoom) {
        if(!s.target.isArea(s.origin.area) && getFullstrText(cmnd->fullstr, 2) != "confirm") {
            for(Exit* ext : player->getConstUniqueRoomParent()->exits) {
                if(ext->flagIsSet(X_LOCKABLE) && ext->getKey()) {
                    player->printColor("^YRS: ^RError: ^xthis room contains a lockable exit and is being moved to a different area.\n");
                    player->printColor("   To continue, type ^W*rswap %s confirm^x. Make sure all keys work correctly.\n", s.target.area.c_str());
                    return;
                }
            }
        }

        if(player->getConstUniqueRoomParent()->flagIsSet(R_SHOP))
            player->printColor("^YRS: ^GThis room is a shop - don't forget to swap the storage room: %s.\n",
                shopStorageRoom(player->getConstUniqueRoomParent()).rstr().c_str());
        else if(player->getConstUniqueRoomParent()->getTrapExit().id)
            player->printColor("^YRS: ^GThis room has a trap exit set: %s.\n",
                player->getConstUniqueRoomParent()->getTrapExit().rstr().c_str());
    }

    if(gConfig->isSwapping()) {
        player->printColor("^YRS: ^x%s is currently running the swap utility.\n", name.c_str());
        if(!gConfig->inSwapQueue(s.origin, s.type)) {
            if(queueSize >= SWAP_QUEUE_LIMIT) {
                player->printColor("^YRS: ^RError: ^xThe queue has a limit of %d; you cannot add any more.\n",
                    SWAP_QUEUE_LIMIT);
                return;
            }
            player->printColor("^YRS: Your request has been added to the queue.\n");
            gConfig->swapAddQueue(s);
        } else
            player->printColor("^YRS: ^RError: ^xThat %s is already in the queue.\n", swapType.c_str());
    } else
        gServer->swap(s);

}

int dmSwap(Player* player, cmd* cmnd) {
    swap(player, cmnd, SwapNone);
    return(0);
}
int dmRoomSwap(Player* player, cmd* cmnd) {
    swap(player, cmnd, SwapRoom);
    return(0);
}
int dmObjSwap(Player* player, cmd* cmnd) {
    swap(player, cmnd, SwapObject);
    return(0);
}
int dmMobSwap(Player* player, cmd* cmnd) {
    swap(player, cmnd, SwapMonster);
    return(0);
}


//*********************************************************************
//                          swap
//*********************************************************************

bool Server::swap(const Swap& s) {
    Player *player = gServer->findPlayer(s.player.c_str());
    gConfig->setMovingRoom(s.origin, s.target);
    std::string output;

    // check validity here
    if(!gConfig->swapChecks(player, s)) {
        gConfig->endSwap();
        return(false);
    }

    // find the room ourselves!
    if(s.target.id == -1) {
        // only one forked process at a time
        if(swapName() != "Someone")
            return(false);


        Async async;
        if(async.branch(player, ChildType::SWAP_FIND) == AsyncExternal) {
            output = findNextEmpty("room", s.target.area).rstr();
            printf("%s", output.c_str());
            exit(0);
        } else {
            if(player)
                player->printColor("^YRS: ^eBeginning search for next empty %s in area \"%s\".\n", ::swapName(s.type).c_str(), s.target.area.c_str());
        }

    } else
        gConfig->finishSwap(s.player);

    return(true);
}

//*********************************************************************
//                          simpleChildRead
//*********************************************************************

std::string Server::simpleChildRead(childProcess &child) {
    char tmpBuf[4096];
    std::string toProcess;
    size_t n;
    for(;;) {
        // read in all of the data
        memset(tmpBuf, '\0', sizeof(tmpBuf));
        n = read(child.fd, tmpBuf, sizeof(tmpBuf)-1);
        if(n <= 0)
            break;
        toProcess += tmpBuf;
    }
    boost::trim(toProcess);
    return(toProcess);
}

//*********************************************************************
//                          findNextEmpty
//*********************************************************************
// gets output from findNextEmpty

void Config::findNextEmpty(childProcess &child, bool onReap) {
    if(!isSwapping())
        return;

    Player* player = gServer->findPlayer(child.extra);

    // a target has not yet been found
    if(currentSwap.target.id == -1) {
        std::string toProcess = gServer->simpleChildRead(child);

        if(!toProcess.empty()) {
            getCatRef(toProcess, &currentSwap.target, nullptr);
            if(player)
                player->printColor("^YRS: ^eRoom found: %s.\n", currentSwap.target.rstr().c_str());
        }
    }

    if(onReap) {
        if(currentSwap.target.id == -1) {
            if(player)
                player->printColor("^YRS: No empty rooms were found!\n");
            endSwap();
        } else {
            UniqueRoom* uRoom=nullptr;

            // did we actually get a room where we were expecting none?
            if(loadRoom(currentSwap.target, &uRoom)) {
                if(roomSearchFailure) {
                    // we're only allowed 1 failure per go
                    if(player)
                        player->printColor("^RRS: ^xMove search failed; aborting this job.\n");
                    endSwap();
                } else {
                    roomSearchFailure = true;

                    if(player) {
                        player->printColor("^RRS: ^xMove search failed; this room is not empty.\n");
                        player->printColor("^RRS: ^xAttempting room search again.\n");
                    }
                    currentSwap.target.id = -1;
                    uRoom->saveToFile(0);
                    gServer->swap(currentSwap);
                }
                return;
            }

            finishSwap(child.extra);
        }
    }
}

//*********************************************************************
//                          finishSwap
//*********************************************************************

void Config::finishSwap(const std::string &mover) {
    Player* player = gServer->findPlayer(mover);
    bool online=true;

    if(!player) {
        if(!loadPlayer(mover, &player)) {
            broadcast(isStaff, fmt::format("^YRS: ^RError: ^xNon-existent player {} attempting to move rooms.\n", mover).c_str());
            endSwap();
            return;
        }
        online=false;
        player->fd = -1;
    }

    // this range is restricted
    if(moveRoomRestrictedArea(currentSwap.target.area)) {
        if(online)
            player->printColor("^YRS: ^RError: ^x""%s"" is a restricted range. You cannot swap unique rooms into or out that area.\n", currentSwap.target.area.c_str());
        else
            free_crt(player);
        endSwap();
        return;
    }

    if(!player->checkBuilder(currentSwap.target)) {
        if(online)
            player->printColor("^YRS: ^RError: ^xRoom number not inside any of your alotted ranges.\n");
        else
            free_crt(player);
        endSwap();
        return;
    }

    // no moving the builder waiting room!
    if(currentSwap.origin.isArea("test") && currentSwap.origin.id == 1) {
        if(online)
            player->printColor("^YRS: ^RError: ^xSorry, you cannot swap this room (%s) (Builder Waiting Room).\n", currentSwap.origin.str().c_str());
        else
            free_crt(player);
        endSwap();
        return;
    }
    if(currentSwap.target.isArea("test") && currentSwap.target.id == 1) {
        if(online)
            player->printColor("^YRS: ^RError: ^xSorry, you cannot swap that room (%s) (Builder Waiting Room).\n", currentSwap.target.str().c_str());
        else
            free_crt(player);
        endSwap();
        return;
    }

    // no moving special rooms out of their areas!
    if(!currentSwap.target.isArea(currentSwap.origin.area)) {
        // endSwap() is handled inside these functions
        if(!checkSpecialArea(currentSwap.origin, currentSwap.target, &CatRefInfo::recall, player, online, "Recall"))
            return;
        if(!checkSpecialArea(currentSwap.origin, currentSwap.target, &CatRefInfo::limbo, player, online, "Limbo"))
            return;
        if(!checkSpecialArea(currentSwap.target, currentSwap.origin, &CatRefInfo::recall, player, online, "Recall"))
            return;
        if(!checkSpecialArea(currentSwap.target, currentSwap.origin, &CatRefInfo::limbo, player, online, "Limbo"))
            return;
    }

    player->printColor("^YRS: ^eSwapping %s %s with %s %s.\n",
        swapName(currentSwap.type).c_str(), currentSwap.origin.str().c_str(),
        swapName(currentSwap.type).c_str(), currentSwap.target.str().c_str());
    gServer->finishSwap(player, online, currentSwap.origin, currentSwap.target);
}

void Server::finishSwap(Player* player, bool online, const CatRef& origin, const CatRef& target) {
    // only one forked process at a time
    if(gServer->swapName() != "Someone") {
        if(!online)
            free_crt(player);
        return;
    }

    log_immort(true, player, "%s has begun swapping %s with %s.\n", player->getCName(), origin.str().c_str(), target.str().c_str());


    Async async;
    if(async.branch(player, ChildType::SWAP_FINISH) == AsyncExternal) {
        gConfig->offlineSwap();
        exit(0);
    } else {
        if(online) {
            player->printColor("^YRS: ^eBeginning offline search sequence.\n");
            player->printColor("^YRS: ^eThis may take several minutes.\n");
        } else
            free_crt(player);
        gConfig->swapLog((std::string)"r" + origin.rstr(), false);
        gConfig->swapLog((std::string)"r" + target.rstr(), false);
    }
}

//*********************************************************************
//                          offlineSwap
//*********************************************************************
// the offline search function

void Config::offlineSwap() {
    std::list<Area*>::iterator aIt;
    std::map<std::string, AreaRoom*>::iterator rIt;
    struct  dirent *dirp=nullptr, *dirq=nullptr;
    DIR     *dir=nullptr, *subdir=nullptr;
    std::string filename = "";
    std::string output = "";

    UniqueRoom* uRoom=nullptr;
    AreaRoom* aRoom=nullptr;
    Player* player=nullptr;
    Monster* monster=nullptr;
    // id = -1 tells the loadFromFile functions to rely on the monster/room
    CatRef placeholder;
    placeholder.id = -1;

    // get a list of all players that need updating
    if((dir = opendir(Path::Player)) != nullptr) {

        while((dirp = readdir(dir)) != nullptr) {
            if(dirp->d_name[0] == '.')
                continue;
            if(!isupper(dirp->d_name[0]))
                continue;

            dirp->d_name[strlen(dirp->d_name)-4] = 0;

            if(!loadPlayer(dirp->d_name, &player))
                continue;

            if(player->swap(currentSwap))
                printf("p%s%s", player->getCName(), sepType);

            free_crt(player);
        }
    }

    // check player backups
    if((dir = opendir(Path::PlayerBackup)) != nullptr) {

        while((dirp = readdir(dir)) != nullptr) {
            if(dirp->d_name[0] == '.')
                continue;
            if(!isupper(dirp->d_name[0]))
                continue;

            dirp->d_name[strlen(dirp->d_name)-8] = 0;

            if(!loadPlayer(dirp->d_name, &player, LoadType::LS_BACKUP))
                continue;

            if(player->swap(currentSwap))
                printf("b%s%s", player->getCName(), sepType);

            free_crt(player);
        }
    }

    // get a list of all unique rooms
    if((dir = opendir(Path::UniqueRoom)) != nullptr) {

        while((dirp = readdir(dir)) != nullptr) {
            if(dirp->d_name[0] == '.')
                continue;

            filename = Path::UniqueRoom;
            filename += dirp->d_name;
            filename += "/";

            if((subdir = opendir(filename.c_str())) != nullptr) {
                while((dirq = readdir(subdir)) != nullptr) {
                    if(dirq->d_name[0] != 'r')
                        continue;

                    filename = Path::UniqueRoom;
                    filename += dirp->d_name;
                    filename += "/";
                    filename += dirq->d_name;

                    if(!loadRoomFromFile(placeholder, &uRoom, filename))
                        continue;

                    // we check origin and target already, so forget about it here
                    if( uRoom->info != currentSwap.origin &&
                        uRoom->info != currentSwap.target &&
                        uRoom->swap(currentSwap)
                    ) {
                        output = uRoom->info.rstr();
                        printf("r%s%s", output.c_str(), sepType);
                    }

                }
            }
        }
    }

    // get a list of all monsters
    if((dir = opendir(Path::Monster)) != nullptr) {

        while((dirp = readdir(dir)) != nullptr) {
            if(dirp->d_name[0] == '.')
                continue;

            filename = Path::Monster;
            filename += dirp->d_name;
            filename += "/";

            if((subdir = opendir(filename.c_str())) != nullptr) {
                while((dirq = readdir(subdir)) != nullptr) {
                    if(dirq->d_name[0] != 'r')
                        continue;

                    filename = Path::Monster;
                    filename += dirp->d_name;
                    filename += "/";
                    filename += dirq->d_name;

                    if(!loadMonsterFromFile(placeholder, &monster, filename))
                        continue;

                    // we check origin and target already, so forget about it here
                    if(monster->swap(currentSwap)) {
                        output = monster->info.rstr();
                        printf("m%s%s", output.c_str(), sepType);
                    }

                    free_crt(monster);
                }
            }
        }
    }

    // get a list of all area rooms
    for(aIt = gServer->areas.begin(); aIt != gServer->areas.end() ; aIt++) {
        for(rIt = (*aIt)->rooms.begin(); rIt != (*aIt)->rooms.end() ; rIt++) {
            aRoom = (*rIt).second;
            if(aRoom->swap(currentSwap)) {
                output = aRoom->mapmarker.str();
                printf("a%s%s", output.c_str(), sepType);
            }
        }
    }
}

// gets output from offlineSwap
void Config::offlineSwap(childProcess &child, bool onReap) {
    if(!isSwapping())
        return;
    Player* player = gServer->findPlayer(child.extra.c_str());
    std::string toProcess = gServer->simpleChildRead(child);

    if(!toProcess.empty()) {
        UniqueRoom *uRoom=nullptr;
        Monster *monster=nullptr;
        CatRef cr;
        std::string input;

        boost::char_separator<char> sep(sepType);
        charTokenizer tok(toProcess, sep);

        // load the rooms as we go
        // last one will always be loaded in target
        for(auto it = tok.begin() ; it != tok.end() ; it++) {
            input = *it;

            swapLog(input);

            if(input.at(0) == 'r') {
                getCatRef(input.substr(1), &cr, nullptr);
                // this will put rooms in the queue
                loadRoom(cr, &uRoom);
            } else if(input.at(0) == 'm') {
                getCatRef(input.substr(1), &cr, nullptr);
                // this will put monsters in the queue
                if(loadMonster(cr, &monster))
                    free_crt(monster);
            }
        }
    }
    if(onReap)
        swap(player, child.extra);
}

//*********************************************************************
//                          swap
//*********************************************************************

void Config::swap(Player* player, std::string_view name) {
    std::list<std::string>::iterator bIt;
    std::list<Swap>::iterator qIt;
    UniqueRoom *uOrigin=nullptr, *uTarget=nullptr;
    bool found;

    if(player)
        player->printColor("^YRS: ^eSearch complete. Beginning final sequence.\n");

    // clear this now or it interferes with saving below
    swapping = false;

    // loop through each area
    found = false;
    std::list<Area*>::iterator aIt;
    for(aIt = gServer->areas.begin() ; aIt != gServer->areas.end() ; aIt++) {
        if((*aIt)->swap(currentSwap))
            found = true;
    }
    if(found)
        gServer->saveAreas(false);


    // loop through each starting location
    found = false;
    std::map<std::string, StartLoc*>::iterator lIt;
    for(lIt = start.begin() ; lIt != start.end() ; lIt++) {
        if((*lIt).second->swap(currentSwap)) {
            if(player)
                player->printColor("^GRS: Be sure to update the starting locations file for %s.\n", (*lIt).second->getName().c_str());
            found = true;
        }
    }
    if(found)
        saveStartLocs();


    // loop through each ship
    // TODO: Dom: midnight ship file?
    found = false;
    std::list<Ship*>::iterator sIt;
    for(sIt = ships.begin() ; sIt != ships.end() ; sIt++) {
        if((*sIt)->swap(currentSwap)) {
            if(player)
                player->printColor("^GRS: Be sure to update the midnight ship file for %s.\n", (*sIt)->name.c_str());
            found = true;
        }
    }
    if(found)
        saveShips();


    // loop through catrefinfo
    found = false;
    std::list<CatRefInfo*>::iterator cIt;
    for(cIt = catRefInfo.begin() ; cIt != catRefInfo.end() ; cIt++) {
        if((*cIt)->swap(currentSwap)) {
            if(player)
                player->printColor("^GRS: Be sure to update the catrefinfo file for %s.\n", (*cIt)->getArea().c_str());
            found = true;
        }
    }
    if(found)
        saveCatRefInfo();


    // check all players online
    Player* ply=nullptr;
    for(const auto& p : gServer->players) {
        ply = p.second;
        if(ply->swap(currentSwap))
            ply->save(true);
        swapList.remove((std::string)"p" + ply->getName());
    }

    // remove the swapped rooms from the queue
    if(gServer->roomCache.contains(currentSwap.origin)) {
        gServer->roomCache.fetch(currentSwap.origin, &uOrigin, true);
        gServer->roomCache.remove(currentSwap.origin);
    }
    if(gServer->roomCache.contains(currentSwap.target)) {
		gServer->roomCache.fetch(currentSwap.target, &uTarget, true);
		gServer->roomCache.remove(currentSwap.target);
	} else {
        // the original room won't exist anymore
        unlink(roomPath(currentSwap.origin));
    }

    // readd them to the queue under their new names
    // DO NOT update now: the loop just after this will do it
    if(uOrigin)
    	gServer->roomCache.insert(currentSwap.target, &uOrigin);
    if(uTarget)
    	gServer->roomCache.insert(currentSwap.origin, &uTarget);

    // go through players, rooms, and arearooms saved while the offline search
    // was running, also includes results of the offline search
    for(bIt = swapList.begin() ; bIt != swapList.end() ; bIt++)
        swap(*bIt);

    for(qIt = swapQueue.begin() ; qIt != swapQueue.end() ; qIt++)
        (*qIt).match(currentSwap.origin, currentSwap.target);

    found = false;
    if(player) {
        if(player->currentLocation.room == currentSwap.origin || player->currentLocation.room == currentSwap.target)
            display_rom(player);
        player->printColor("^YRS: Room swap complete.\n");
        found = true;
    } else
        loadPlayer(name, &player);

    if(player)
        log_immort(true, player, fmt::format("{} has finished swapping {} with {}.\n", name, currentSwap.origin.str(), currentSwap.target.str()).c_str());
    else
        broadcast(isStaff, fmt::format("^y{} has finished swapping {} with {}.", name, currentSwap.origin.str(), currentSwap.target.str()).c_str());

    if(!found && player)
        free_crt(player);

    endSwap();
}

//*********************************************************************
//                          Swap
//*********************************************************************

Swap::Swap() { player = ""; }

void Swap::set(std::string_view mover, const CatRef& swapOrigin, const CatRef& swapTarget, SwapType swapType) {
    player = mover;
    origin = swapOrigin;
    target = swapTarget;
    type = swapType;
}

bool Swap::match(const CatRef& o, const CatRef& t) {
    bool found=false;

    if(origin == o) {
        origin = t;
        found = true;
    } else if(origin == t) {
        origin = t;
        found = true;
    }

    if(target == o) {
        target = t;
        found = true;
    } else if(target == t) {
        target = t;
        found = true;
    }

    return(found);
}


//*********************************************************************
//                          swapChecks
//*********************************************************************

bool Config::swapChecks(const Player* player, const Swap& s) {
    if(s.type == SwapNone) {
        player->printColor("^YRS: ^RError: ^xInvalid swap type.\n");
        return(false);
    }

    if(s.type == SwapRoom) {
        if(moveRoomRestrictedArea(s.origin.area)) {
            if(player)
                player->printColor("^YRS: ^RError: ^x""%s"" is a restricted range. You cannot swap unique rooms into or out of that area.\n", s.origin.area.c_str());
            return(false);
        }
        if(moveRoomRestrictedArea(s.target.area)) {
            if(player)
                player->printColor("^YRS: ^RError: ^x""%s"" is a restricted range. You cannot swap unique rooms into or out of that area.\n", s.target.area.c_str());
            return(false);
        }
    } else if(s.type == SwapObject) {
        if(moveObjectRestricted(s.origin)) {
            if(player)
                player->printColor("^YRS: ^RError: ^x""%s"" is a hardcoded object and cannot be swapped.\n", s.origin.rstr().c_str());
            return(false);
        }
        if(moveObjectRestricted(s.target)) {
            if(player)
                player->printColor("^YRS: ^RError: ^x""%s"" is a hardcoded object and cannot be swapped.\n", s.target.rstr().c_str());
            return(false);
        }
    }

    if(s.target == s.origin) {
        if(player)
            player->printColor("^YRS: ^RError: ^xYou cannot swap a room with itself!\n");
        return(false);
    }
    if(!s.origin.id) {
        if(player)
            player->printColor("^YRS: ^RError: ^xYou cannot move The Void (room 0).\n");
        return(false);
    }
    if(!s.target.id) {
        if(player)
            player->printColor("^YRS: ^RError: ^xYou cannot swap a room with The Void (room 0).\n");
        return(false);
    }

    return(true);
}

//*********************************************************************
//                          moveObjectRestricted
//*********************************************************************

bool Config::moveObjectRestricted(const CatRef& cr) const {
    if(cr.isArea("misc")) {
        if( cr.id == SHIT_OBJ ||
            cr.id == CORPSE_OBJ ||
            cr.id == BODYPART_OBJ ||
            cr.id == STATUE_OBJ ||
            cr.id == TICKET_OBJ
        ) {
            return(true);
        }
    }

    return(false);
}

//*********************************************************************
//                          moveRoomRestrictedArea
//*********************************************************************

bool Config::moveRoomRestrictedArea(std::string_view area) const {
    return(area == "area" || area == "stor" || area == "shop" || area == "guild");
}


//*********************************************************************
//                          checkSpecialArea
//*********************************************************************

bool Config::checkSpecialArea(const CatRef& origin, const CatRef& target, int (CatRefInfo::*toCheck), Player* player, bool online, std::string_view type) {
    Location l = getSpecialArea(toCheck, origin);
    bool t = origin == l.room;
    if(t || target == l.room) {
        if(online) {
            player->bPrint(fmt::format("^YRS: ^RError: ^xRoom ({}) room is set as a {} Room under CatRefInfo.\n",
                t ? origin.str() : target.str(), type));
            player->print("It cannot be moved out of its area.\n");
        } else
            free_crt(player);
        endSwap();
        return(false);
    }
    return(true);
}


//*********************************************************************
//                          swap
//*********************************************************************

void Config::swap(std::string str) {
    char type = str.at(0);
    str = str.substr(1);
    if(type == 'p' || type == 'b') {
        // at this point, the player will always be offline
        Player* player=nullptr;
        LoadType saveType = type == 'p' ? LoadType::LS_NORMAL : LoadType::LS_BACKUP;

        if(!loadPlayer(str.c_str(), &player, saveType))
            return;

        if(player->swap(currentSwap))
            player->save(false, saveType);
        free_crt(player);
    } else if(type == 'm') {
        // the monster should have been loaded into the queue by now
        Monster* monster=nullptr;
        CatRef cr;
        getCatRef(str, &cr, nullptr);

        if(!loadMonster(cr, &monster))
            return;

        if(monster->swap(currentSwap))
            monster->saveToFile();
        free_crt(monster);
    } else if(type == 'r') {
        // the room should have been loaded into the queue by now
        UniqueRoom* uRoom=nullptr;
        CatRef cr;
        getCatRef(str, &cr, nullptr);

        if(!loadRoom(cr, &uRoom))
            return;

        if(uRoom->swap(currentSwap))
            uRoom->saveToFile(0);
    } else if(type == 'a') {
        // arearooms are always in memory
        Area *area=nullptr;
        AreaRoom* aRoom=nullptr;
        MapMarker m;

        m.load(str);
        area = gServer->getArea(m.getArea());
        if(!area)
            return;
        aRoom = area->loadRoom(nullptr, &m, false);
        if(!aRoom)
            return;

        if(aRoom->swap(currentSwap))
            aRoom->save();
    }
}

//*********************************************************************
//                          swapLog
//*********************************************************************
// record players and rooms that are saved during roomMove

void Config::swapLog(const std::string& log, bool external) {
    char type = log.at(0);
    if(type != 'b' && type != 'p' && type != 'm' && type != 'r' && type != 'a')
        return;

    swapList.remove(log);
    swapList.push_back(log);

    Player* player = gServer->findPlayer(gServer->swapName().c_str());

    if(player) {
        std::string mvName = log.substr(1);

        // are they allowed to see this?
        if(type == 'p' || type == 'b') {
            if(player->getClass() == CreatureClass::BUILDER)
                return;
            if(player->getClass() == CreatureClass::CARETAKER && isdm(mvName))
                return;
        }

        player->printColor("^%cRS: ^eReceiving %s%s%s.\n", external ? 'M' : 'Y',
            type == 'm' ? "Monster " : "",
            mvName.c_str(),
            type == 'b' ? " (backup)" : "");
    }
}

//*********************************************************************
//                          isSwapping functions
//*********************************************************************

bool Config::isSwapping() const { return(swapping); }
void Config::setMovingRoom(const CatRef& o, const CatRef& t) {
    swapping = true;
    currentSwap.origin = o;
    currentSwap.target = t;
}

//*********************************************************************
//                          swapName
//*********************************************************************

std::string Server::swapName() {
    for(childProcess & child : children)
        if(child.type == ChildType::SWAP_FIND || child.type == ChildType::SWAP_FINISH)
            return(child.extra);
    return("Someone");
}

//*********************************************************************
//                          endSwap
//*********************************************************************

void Server::endSwap() {
    std::list<childProcess>::iterator it;
    for(it = children.begin(); it != children.end() ;) {
        if((*it).type == ChildType::SWAP_FIND || (*it).type == ChildType::SWAP_FINISH) {
            close((*it).fd);
            kill((*it).pid, 9);
            it = children.erase(it);
        } else
            it++;
    }
}

void Config::endSwap(int id) {
    if(id <= 1) {
        roomSearchFailure = false;
        swapping = false;
        swapList.clear();
        currentSwap.target.id = 0;
        swapNextInQueue();
        return;
    }
    std::list<Swap>::iterator qIt;
    int i=1;
    for(qIt = swapQueue.begin() ; qIt != swapQueue.end() ; qIt++) {
        if(++i==id) {
            swapQueue.erase(qIt);
            return;
        }
    }
}

//*********************************************************************
//                          swapInfo
//*********************************************************************

void Config::swapInfo(const Player* player) {
    bool canSee;
    char type;
    std::string mvName;

    player->printColor("^WWwap Config Info\n");
    player->printColor("   Swapping: %s^x  What: ^e%s\n", isSwapping() ? "^gYes" : "^rNo", swapName(currentSwap.type).c_str());
    player->printColor("   Queue Size: ^c%d\n", SWAP_QUEUE_LIMIT);
    player->print("   Data In Memory:\n");

    std::list<std::string>::iterator bIt;
    for(bIt = swapList.begin() ; bIt != swapList.end() ; bIt++) {
        canSee = true;
        type = (*bIt).at(0);
        mvName = (*bIt).substr(1);

        if(type == 'p' || type == 'b') {
            if(player->getClass() == CreatureClass::BUILDER)
                canSee = false;
            if(player->getClass() == CreatureClass::CARETAKER && isdm(mvName))
                canSee = false;
        }

        if(canSee)
            player->printColor("      ^e%s%s\n", mvName.c_str(),
                (*bIt).at(0) == 'b' ? " (backup)" : "");
    }
    std::list<Swap>::iterator qIt;
    int id=1;
    player->print("   The Queue:\n");
    for(qIt = swapQueue.begin() ; qIt != swapQueue.end() ; qIt++) {
        player->printColor("      %d) Player: ^e%s^x   Origin: ^e%s^x   Target: ^e%s\n", ++id,
            (*qIt).player.c_str(), (*qIt).origin.str().c_str(),
            (*qIt).target.id == -1 ? (*qIt).target.area.c_str() : (*qIt).target.str().c_str());
    }
}

void Server::swapInfo(const Player* player) {
    player->printColor("^WSwap Server Info\n");
    player->print("   Child Processes Being Watched:\n");
    for(childProcess & child : children) {
        if(child.type == ChildType::SWAP_FIND || child.type == ChildType::SWAP_FINISH) {
            player->printColor("      Player: ^e%s^x   Pid: ^e%d^x   Fd: ^e%d^x   Purpose: ^e%s\n",
                child.extra.c_str(), child.pid, child.fd,
                child.type == ChildType::SWAP_FIND ? "Finding next empty slot." :
                    "Finding things that need updating.");
        }
    }
}

//*********************************************************************
//                          swap queue functions
//*********************************************************************

void Config::swapEmptyQueue() { swapQueue.clear(); }
int Config::swapQueueSize() { return(swapQueue.size()); }

void Config::swapNextInQueue() {
    if(swapQueue.empty())
        return;
    Swap s = swapQueue.front();
    swapQueue.pop_front();
    gServer->swap(s);
}

void Config::swapAddQueue(const Swap& s) {
    swapQueue.push_back(s);
}

bool Config::inSwapQueue(const CatRef& origin, SwapType type, bool checkTarget) {
    std::list<Swap>::iterator it;
    for(it = swapQueue.begin() ; it != swapQueue.end() ; it++) {
        if(type == (*it).type) {
            if(origin == (*it).origin)
                return(true);
            if(checkTarget && origin == (*it).target)
                return(true);
        }
    }
    return(false);
}

//*********************************************************************
//                          swapAbort
//*********************************************************************

void Config::swapAbort() {
    swapEmptyQueue();
    gServer->endSwap();
    endSwap();
}

//*********************************************************************
//                          Player swap
//*********************************************************************

bool Config::swapIsInteresting(const MudObject* target) const {
    if(!isSwapping())
        return(false);

    const Monster* monster = target->getAsConstMonster();
    if(monster && monster->swapIsInteresting(currentSwap))
        return(true);

    const Player* player = target->getAsConstPlayer();
    if(player && player->swapIsInteresting(currentSwap))
        return(true);

    const Object* object = target->getAsConstObject();
    if(object && object->swapIsInteresting(currentSwap))
        return(true);

    const AreaRoom* aRoom = target->getAsConstAreaRoom();
    if(aRoom && aRoom->swapIsInteresting(currentSwap))
        return(true);

    const UniqueRoom* uRoom = target->getAsConstUniqueRoom();
    return uRoom && uRoom->swapIsInteresting(currentSwap);

}

//*********************************************************************
//                          nextDelim
//*********************************************************************

char whichDelim(std::string_view code) {
    std::string::size_type qPos, aPos;

    qPos = code.find("\"");
    aPos = code.find("'");

    if(qPos == std::string::npos && aPos == std::string::npos)
        return(0);
    if(qPos == std::string::npos)
        return('\'');
    if(aPos == std::string::npos)
        return('"');
    if(qPos < aPos)
        return('"');
    return('\'');
}

//*********************************************************************
//                          getParamFromCode
//*********************************************************************

std::string getParamFromCode(const std::string &pythonCode, const std::string &function, SwapType type) {
    std::string code = pythonCode;
    std::string::size_type pos;
    char delim;

    // find the function name
    pos = code.find(function);
    if(pos == std::string::npos)
        return("");

    if(function == "spawnObjects") {
        // room is the first param, objects is the second param
        code = code.substr(pos, pos - code.length());

        // find the delimiter for the first parameter
        delim = whichDelim(code);
        if(delim == 0)
            return("");
        pos = code.find(delim);

        code = code.substr(pos + 1, pos - code.length() - 1);

        // for objects, move on to the next parameter
        if(type == SwapObject) {
            pos = code.find(delim);
            if(pos == std::string::npos)
                return("");
            code = code.substr(pos + 1, pos - code.length() - 1);

            // find the delimiter for the second parameter
            delim = whichDelim(code);
            if(delim == 0)
                return("");
            pos = code.find(delim);

            code = code.substr(pos + 1, pos - code.length() - 1);
        }

        pos = code.find(delim);
        if(pos == std::string::npos)
            return("");
        code = code.substr(0, pos);

        return(code);
    }

    return("");
}

//*********************************************************************
//                          setParamInCode
//*********************************************************************

std::string setParamInCode(const std::string &pythonCode, const std::string &function, SwapType type, const std::string &param) {
    std::string code = pythonCode;
    std::string::size_type pos;

    pos = code.find(function);
    if(pos == std::string::npos)
        return(code);

    if(function == "spawnObjects") {
        // room is the first param, objects is the second param
    }

    return(code);
}

//*********************************************************************
//                          Hooks swap
//*********************************************************************

bool Hooks::swap(const Swap& s) {
    bool found=false;
    std::string param;
    CatRef cr;

    for(std::pair<std::string,std::string> p : hooks ) {
        if(s.type == SwapRoom) {
            param = getParamFromCode(p.second, "spawnObjects", s.type);
            if(!param.empty()) {
                getCatRef(param, &cr, 0);
                if(cr == s.origin) {
                    p.second = setParamInCode(p.second, "spawnObjects", s.type, param);
                    found = true;
                } else if(cr == s.target) {
                    found = true;
                }
            }
        }
    }

    return(found);
}

//*********************************************************************
//                          Hooks swapIsInteresting
//*********************************************************************

bool Hooks::swapIsInteresting(const Swap& s) const {
    std::string param;
    CatRef cr;

    for(const auto& p : hooks) {
        if(s.type == SwapRoom) {
            param = getParamFromCode(p.second, "spawnObjects", s.type);
            if(!param.empty()) {
                getCatRef(param, &cr, nullptr);
                if(cr == s.origin || cr == s.target)
                    return(true);
            }
        } else if(s.type == SwapObject) {
            std::string obj;
            int i=0;

            param = getParamFromCode(p.second, "spawnObjects", s.type);
            if(!param.empty()) {
                do {
                    obj = getFullstrTextTrun(param, i++);
                    if(!obj.empty())
                    {
                        getCatRef(obj, &cr, nullptr);
                        if(cr == s.origin || cr == s.target)
                            return(true);
                    }
                } while(!obj.empty());
            }
        }
    }

    return(false);
}

//*********************************************************************
//                          Player swap
//*********************************************************************

bool Player::swap(const Swap& s) {
    bool found=false;

    if(bound.room == s.origin) {
        bound.room = s.target;
        found = true;
    } else if(bound.room == s.target) {
        bound.room = s.origin;
        found = true;
    }
    if(currentLocation.room == s.origin) {
        currentLocation.room = s.target;
        found = true;
    } else if(currentLocation.room == s.target) {
        currentLocation.room = s.origin;
        found = true;
    }

    for(auto & i : anchor) {
        if(i) {
            if(i->getRoom() == s.origin) {
                i->setRoom(s.target);
                found = true;
            } else if(i->getRoom() == s.target) {
                i->setRoom(s.origin);
                found = true;
            }
        }
    }

    std::list<CatRef>::iterator it;
    for(it = roomExp.begin() ; it != roomExp.end() ; it++) {
        if(*it == s.origin) {
            *it = s.target;
            found = true;
        } else if(*it == s.target) {
            *it = s.origin;
            found = true;
        }
    }

    if(hooks.swap(s))
        found = true;

    return(found);
}

//*********************************************************************
//                          Player swapIsInteresting
//*********************************************************************

bool Player::swapIsInteresting(const Swap& s) const {
    if(!gConfig->isSwapping())
        return(false);

    if(bound.room == s.origin || bound.room == s.target)
        return(true);
    if(currentLocation.room == s.origin || currentLocation.room == s.target)
        return(true);

    for(auto i : anchor) {
        if(i) {
            if(i->getRoom() == s.origin || i->getRoom() == s.target)
                return(true);
        }
    }

    std::list<CatRef>::const_iterator it;
    for(it = roomExp.begin() ; it != roomExp.end() ; it++) {
        if(*it == s.origin || *it == s.target)
            return(true);
    }

    return hooks.swapIsInteresting(s);

}

//*********************************************************************
//                          Monster swap
//*********************************************************************

bool Monster::swap(const Swap& s) {
    bool found=false;

    if(jail == s.origin) {
        jail = s.target;
        found = true;
    } else if(jail == s.target) {
        jail = s.origin;
        found = true;
    }
    if(currentLocation.room == s.origin) {
        currentLocation.room = s.target;
        found = true;
    } else if(currentLocation.room == s.target) {
        currentLocation.room = s.origin;
        found = true;
    }

    if(hooks.swap(s))
        found = true;

    return(found);
}

//*********************************************************************
//                          Monster swapIsInteresting
//*********************************************************************

bool Monster::swapIsInteresting(const Swap& s) const {
    if(jail == s.origin || jail == s.target)
        return(true);
    if(currentLocation.room == s.origin || currentLocation.room == s.target)
        return(true);

    return hooks.swapIsInteresting(s);

}

//*********************************************************************
//                          Object swap
//*********************************************************************

bool Object::swap(Swap s) {
    bool found=false;

    if(hooks.swap(std::move(s)))
        found = true;

    return(found);
}

//*********************************************************************
//                          Object swapIsInteresting
//*********************************************************************

bool Object::swapIsInteresting(Swap s) const {

    return hooks.swapIsInteresting(std::move(s));

}

//*********************************************************************
//                          Room swap
//*********************************************************************

bool UniqueRoom::swap(const Swap& s) {
    bool found=false;

    if(info == s.origin || info == s.target) {
        // if shop relies on next room for storage
        if(flagIsSet(R_SHOP) && !trapexit.id) {
            // manually set
            trapexit = info;
            trapexit.id++;
        }

        if(info == s.origin) {
            info = s.target;
        } else {
            info = s.origin;
        }
        found = true;
    }

    for(Monster* monster : monsters) {
        if(monster->swap(s))
            found = true;
    }

    if(trapexit == s.origin) {
        trapexit = s.target;
        found = true;
    } else if(trapexit == s.target) {
        trapexit = s.origin;
        found = true;
    }

    for(Exit* ext : exits) {
        if(ext->target.room == s.origin) {
            ext->target.room = s.target;
            found = true;
        } else if(ext->target.room == s.target) {
            ext->target.room = s.origin;
            found = true;
        }
    }

    if(hooks.swap(s))
        found = true;

    return(found);
}

//*********************************************************************
//                          Room swapIsInteresting
//*********************************************************************

bool UniqueRoom::swapIsInteresting(const Swap& s) const {
    if(info == s.origin || info == s.target)
        return(true);

    for(const Monster* monster : monsters) {
        if(monster->swapIsInteresting(s))
            return(true);
    }

    if(trapexit == s.origin || trapexit == s.target)
        return(true);

    for(Exit* ext : exits) {
        if(ext->target.room == s.origin || ext->target.room == s.target)
            return(true);
    }

    return hooks.swapIsInteresting(s);

}

//*********************************************************************
//                          AreaRoom swap
//*********************************************************************

bool AreaRoom::swap(const Swap& s) {
    bool found=false;

    if(unique == s.origin) {
        unique = s.target;
        found = true;
    } else if(unique == s.target) {
        unique = s.origin;
        found = true;
    }

    for(Exit* ext : exits) {
        if(ext->target.room == s.origin) {
            ext->target.room = s.target;
            found = true;
        } else if(ext->target.room == s.target) {
            ext->target.room = s.origin;
            found = true;
        }
    }
    for(Monster* monster : monsters) {
        if(monster->swap(s))
            found = true;
    }

    if(hooks.swap(s))
        found = true;

    return(found);
}

//*********************************************************************
//                          AreaRoom swapIsInteresting
//*********************************************************************

bool AreaRoom::swapIsInteresting(const Swap& s) const {
    if(unique == s.origin || unique == s.target)
        return(true);

    for(Exit* ext : exits) {
        if(ext->target.room == s.origin || ext->target.room == s.target)
            return(true);
    }

    for(const Monster* monster : monsters) {
        if(monster->swapIsInteresting(s))
            return(true);
    }

    return hooks.swapIsInteresting(s);

}

//*********************************************************************
//                          AreaZone swap
//*********************************************************************

bool AreaZone::swap(const Swap& s) {
    bool found=false;

    if(unique == s.origin) {
        unique = s.target;
        found = true;
    } else if(unique == s.target) {
        unique = s.origin;
        found = true;
    }

    return(found);
}

//*********************************************************************
//                          Area swap
//*********************************************************************

bool Area::swap(const Swap& s) {
    //std::map<std::string, AreaRoom*>::iterator rIt;
    std::list<AreaZone*>::iterator zIt;
    bool found=false;

    for(zIt = zones.begin() ; zIt != zones.end() ; zIt++) {
        if((*zIt)->swap(s))
            found = true;
    }

    return(found);
}

//*********************************************************************
//                          ShipRaid swap
//*********************************************************************

bool ShipRaid::swap(const Swap& s) {
    bool found=false;

    if(prison == s.origin) {
        prison = s.target;
        found = true;
    } else if(prison == s.target) {
        prison = s.origin;
        found = true;
    }
    if(dump == s.origin) {
        dump = s.target;
        found = true;
    } else if(dump == s.target) {
        dump = s.origin;
        found = true;
    }

    return(found);
}

//*********************************************************************
//                          ShipExit swap
//*********************************************************************

bool ShipExit::swap(const Swap& s) {
    bool found=false;

    if(origin.room == s.origin) {
        origin.room = s.target;
        found = true;
    } else if(origin.room == s.target) {
        origin.room = s.origin;
        found = true;
    }
    if(target.room == s.origin) {
        target.room = s.target;
        found = true;
    } else if(target.room == s.target) {
        target.room = s.origin;
        found = true;
    }

    return(found);
}

//*********************************************************************
//                          ShipStop swap
//*********************************************************************

bool ShipStop::swap(const Swap& s) {
    std::list<ShipExit*>::iterator it;
    bool found=false;

    for(it = exits.begin() ; it != exits.end() ; it++) {
        if((*it)->swap(s))
            found = true;
    }

    if(raid && raid->swap(s))
        found = true;

    return(found);
}

//*********************************************************************
//                          Ship swap
//*********************************************************************

bool Ship::swap(const Swap& s) {
    std::list<ShipStop*>::iterator it;
    bool found=false;

    for(it = stops.begin() ; it != stops.end() ; it++) {
        if((*it)->swap(s))
            found = true;
    }

    return(found);
}

//*********************************************************************
//                          StartLoc swap
//*********************************************************************

bool StartLoc::swap(const Swap& s) {
    bool found=false;

    if(bind.room == s.origin) {
        bind.room = s.target;
        found = true;
    } else if(bind.room == s.target) {
        bind.room = s.origin;
        found = true;
    }
    if(required.room == s.origin) {
        required.room = s.target;
        found = true;
    } else if(required.room == s.target) {
        required.room = s.origin;
        found = true;
    }

    return(found);
}

//*********************************************************************
//                          CatRefInfo swap
//*********************************************************************

bool CatRefInfo::swap(const Swap& s) {
    bool found=false;

    if(s.origin.isArea(area) && limbo == s.origin.id) {
        limbo = s.target.id;
        found = true;
    } else if(s.target.isArea(area) && limbo == s.target.id) {
        limbo = s.origin.id;
        found = true;
    }

    if(s.origin.isArea(area) && recall == s.origin.id) {
        recall = s.target.id;
        found = true;
    } else if(s.target.isArea(area) && recall == s.target.id) {
        recall = s.origin.id;
        found = true;
    }

    return(found);
}
