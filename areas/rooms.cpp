/*
 * rooms.cpp
 *   Room routines.
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

#include <stdarg.h>                    // for va_list, va_end, va_start
#include <unistd.h>                    // for unlink
#include <algorithm>                   // for find
#include <cstring>                     // for memset, strcpy
#include <ctime>                       // for time
#include <list>                        // for operator==, list<>::iterator
#include <map>                         // for operator==, _Rb_tree_const_ite...
#include <set>                         // for set, set<>::iterator
#include <sstream>                     // for char_traits, operator<<, basic...
#include <string>                      // for string, allocator, operator<<
#include <string_view>                 // for string_view, operator==, basic...

#include "area.hpp"                    // for MapMarker, Area, AreaZone, Til...
#include "catRef.hpp"                  // for CatRef
#include "catRefInfo.hpp"              // for CatRefInfo, CatRefInfo::limbo
#include "config.hpp"                  // for Config, gConfig
#include "effects.hpp"                 // for Effects
#include "enums/loadType.hpp"          // for LoadType, LoadType::LS_BACKUP
#include "flags.hpp"                   // for R_LIMBO, R_VAMPIRE_COVEN, R_DA...
#include "free_crt.hpp"                // for free_crt
#include "global.hpp"                  // for HELD, ARACHNUS, ARAMON, ARES
#include "hooks.hpp"                   // for Hooks
#include "lasttime.hpp"                // for lasttime, crlasttime
#include "location.hpp"                // for Location
#include "move.hpp"                    // for deletePortal
#include "mud.hpp"                     // for MAX_MOBS_IN_ROOM
#include "mudObjects/areaRooms.hpp"    // for AreaRoom
#include "mudObjects/container.hpp"    // for ObjectSet, MonsterSet, PlayerSet
#include "mudObjects/creatures.hpp"    // for Creature
#include "mudObjects/exits.hpp"        // for Exit
#include "mudObjects/monsters.hpp"     // for Monster
#include "mudObjects/objects.hpp"      // for Object
#include "mudObjects/players.hpp"      // for Player
#include "mudObjects/rooms.hpp"        // for BaseRoom, ExitList
#include "mudObjects/uniqueRooms.hpp"  // for UniqueRoom
#include "proto.hpp"                   // for link_rom, isDay, broadcast
#include "random.hpp"                  // for Random
#include "realm.hpp"                   // for Realm
#include "server.hpp"                  // for Server, gServer, RoomCache
#include "size.hpp"                    // for Size, NO_SIZE, SIZE_COLOSSAL
#include "socket.hpp"                  // for Socket
#include "utils.hpp"                   // for MAX
#include "wanderInfo.hpp"              // for WanderInfo
#include "xml.hpp"                     // for loadRoom



BaseRoom::BaseRoom() {
    tempNoKillDarkmetal = false;
    memset(misc, 0, sizeof(misc));
    hooks.setParent(this);
}


std::string BaseRoom::getVersion() const { return(version); }
void BaseRoom::setVersion(std::string_view v) { version = v; }


std::string BaseRoom::fullName() const {
    std::ostringstream oStr;
    if(!this) {
        oStr << "Invalid Room";
        return(oStr.str());
    }

    const UniqueRoom* uRoom = getAsConstUniqueRoom();
    const AreaRoom* aRoom = getAsConstAreaRoom();

    if(uRoom) {
        oStr << uRoom->info.str() << "(" << uRoom->getName() << ")";
    } else if(aRoom) {
        oStr << aRoom->mapmarker.str();
    } else
        oStr << "invalid room";
    return(oStr.str());
}


//*********************************************************************
//                      Room
//*********************************************************************

UniqueRoom::UniqueRoom() {
    short_desc = "";
    long_desc = "";
    lowLevel = highLevel = maxmobs = trap = trapweight = trapstrength = 0;
    memset(flags, 0, sizeof(flags));
    roomExp = 0;
    size = NO_SIZE;
    beenhere = 0;
    memset(last_mod, 0, sizeof(last_mod));

    memset(lastModTime, 0, sizeof(lastModTime));

    memset(lastPly, 0, sizeof(lastPly));
    memset(lastPlyTime, 0, sizeof(lastPlyTime));
}

bool UniqueRoom::operator< (const UniqueRoom& t) const {
    if(this->info.area[0] == t.info.area[0]) {
        return(this->info.id < t.info.id);
    } else {
        return(this->info.area < t.info.area);
    }
}

std::string UniqueRoom::getShortDescription() const { return(short_desc); }
std::string UniqueRoom::getLongDescription() const { return(long_desc); }
short UniqueRoom::getLowLevel() const { return(lowLevel); }
short UniqueRoom::getHighLevel() const { return(highLevel); }
short UniqueRoom::getMaxMobs() const { return(maxmobs); }
short UniqueRoom::getTrap() const { return(trap); }
CatRef UniqueRoom::getTrapExit() const { return(trapexit); }
short UniqueRoom::getTrapWeight() const { return(trapweight); }
short UniqueRoom::getTrapStrength() const { return(trapstrength); }
std::string UniqueRoom::getFaction() const { return(faction); }
long UniqueRoom::getBeenHere() const { return(beenhere); }
int UniqueRoom::getRoomExperience() const { return(roomExp); }
Size UniqueRoom::getSize() const { return(size); }

void UniqueRoom::setShortDescription(std::string_view desc) { short_desc = desc; }
void UniqueRoom::setLongDescription(std::string_view desc) { long_desc = desc; }
void UniqueRoom::appendShortDescription(std::string_view desc) { short_desc += desc; }
void UniqueRoom::appendLongDescription(std::string_view desc) { long_desc += desc; }
void UniqueRoom::setLowLevel(short lvl) { lowLevel = MAX<short>(0, lvl); }
void UniqueRoom::setHighLevel(short lvl) { highLevel = MAX<short>(0, lvl); }
void UniqueRoom::setMaxMobs(short m) { maxmobs = MAX<short>(0, m); }
void UniqueRoom::setTrap(short t) { trap = t; }
void UniqueRoom::setTrapExit(const CatRef& t) { trapexit = t; }
void UniqueRoom::setTrapWeight(short weight) { trapweight = MAX<short>(0, weight); }
void UniqueRoom::setTrapStrength(short strength) { trapstrength = MAX<short>(0, strength); }
void UniqueRoom::setFaction(std::string_view f) { faction = f; }
void UniqueRoom::incBeenHere() { beenhere++; }
void UniqueRoom::setRoomExperience(int exp) { roomExp = MAX(0, exp); }
void UniqueRoom::setSize(Size s) { size = s; }

//*********************************************************************
//                      deconstructors
//*********************************************************************

void BaseRoom::BaseDestroy() {
    clearExits();
    ObjectSet::iterator it;
    Object* obj;
    for(it = objects.begin() ; it != objects.end() ; ) {
        obj = (*it++);
        delete obj;
    }
    objects.clear();

    auto mIt = monsters.begin();
    while(mIt != monsters.end()) {
        Monster* mons = (*mIt++);
        free_crt(mons);
    }
    monsters.clear();

    players.clear();

    effects.removeAll();
    removeEffectsIndex();
    gServer->removeDelayedActions(this);
}

UniqueRoom::~UniqueRoom() {
    BaseDestroy();
}

AreaRoom::~AreaRoom() {
    BaseDestroy();
    reset();
}
bool AreaRoom::operator< (const AreaRoom& t) const {
    if(this->mapmarker.getX() == t.mapmarker.getX()) {
        if(this->mapmarker.getY() == t.mapmarker.getY()) {
            return(this->mapmarker.getZ() < t.mapmarker.getZ());
        } else {
            return(this->mapmarker.getY() < t.mapmarker.getY());
        }
    } else {
        return(this->mapmarker.getX() < t.mapmarker.getX());
    }
}

//*********************************************************************
//                      reset
//*********************************************************************

void AreaRoom::reset() {
    area = nullptr;
    decCompass = needsCompass = stayInMemory = false;

    ExitList::iterator xit;
    for(xit = exits.begin() ; xit != exits.end(); ) {
        Exit* exit = (*xit++);
        delete exit;
    }
    exits.clear();

}

//*********************************************************************
//                      AreaRoom
//*********************************************************************

AreaRoom::AreaRoom(Area *a, const MapMarker *m) {
    reset();

    area = a;
    if(m) {
        setMapMarker(m);
        recycle();
    }
}

Size AreaRoom::getSize() const { return(SIZE_COLOSSAL); }
bool AreaRoom::getNeedsCompass() const { return(needsCompass); }
bool AreaRoom::getDecCompass() const { return(decCompass); }
bool AreaRoom::getStayInMemory() const { return(stayInMemory); }

void AreaRoom::setNeedsCompass(bool need) { needsCompass = need; }
void AreaRoom::setDecCompass(bool dec) { decCompass = dec; }
void AreaRoom::setStayInMemory(bool stay) { stayInMemory = stay; }

//*********************************************************************
//                      recycle
//*********************************************************************

void AreaRoom::recycle() {
    ObjectSet::iterator it;
    Object* obj;
    for(it = objects.begin() ; it != objects.end() ; ) {
        obj = (*it++);
        delete obj;
    }
    objects.clear();

    updateExits();
}

//*********************************************************************
//                      setMapMarker
//*********************************************************************

void AreaRoom::setMapMarker(const MapMarker* m) {
    unRegisterMo();
    setId("-1");
    std::string str = mapmarker.str();
    area->rooms.erase(str);
    *&mapmarker = *m;
    str = mapmarker.str();
    area->rooms[str] = this;
    setId(std::string("R") + str);
    registerMo();
}

//*********************************************************************
//                      updateExits
//*********************************************************************

bool AreaRoom::updateExit(std::string_view dir) {
    if(dir == "north") {
        mapmarker.add(0, 1, 0);
        link_rom(this, &mapmarker, dir);
        mapmarker.add(0, -1, 0);

    } else if(dir == "east") {
        mapmarker.add(1, 0, 0);
        link_rom(this, &mapmarker, dir);
        mapmarker.add(-1, 0, 0);

    } else if(dir == "south") {
        mapmarker.add(0, -1, 0);
        link_rom(this, &mapmarker, dir);
        mapmarker.add(0, 1, 0);

    } else if(dir == "west") {
        mapmarker.add(-1, 0, 0);
        link_rom(this, &mapmarker, dir);
        mapmarker.add(1, 0, 0);

    } else if(dir == "northeast") {
        mapmarker.add(1, 1, 0);
        link_rom(this, &mapmarker, dir);
        mapmarker.add(-1, -1, 0);

    } else if(dir == "northwest") {
        mapmarker.add(-1, 1, 0);
        link_rom(this, &mapmarker, dir);
        mapmarker.add(1, -1, 0);

    } else if(dir == "southeast") {
        mapmarker.add(1, -1, 0);
        link_rom(this, &mapmarker, dir);
        mapmarker.add(-1, 1, 0);

    } else if(dir == "southwest") {
        mapmarker.add(-1, -1, 0);
        link_rom(this, &mapmarker, dir);
        mapmarker.add(1, 1, 0);

    } else
        return(false);
    return(true);
}

void AreaRoom::updateExits() {
    updateExit("north");
    updateExit("east");
    updateExit("south");
    updateExit("west");
    updateExit("northeast");
    updateExit("northwest");
    updateExit("southeast");
    updateExit("southwest");
}


//*********************************************************************
//                      exitNameByOrder
//*********************************************************************

const char *exitNameByOrder(int i) {
    if(i==0) return("north");
    if(i==1) return("east");
    if(i==2) return("south");
    if(i==3) return("west");
    if(i==4) return("northeast");
    if(i==5) return("northwest");
    if(i==6) return("southeast");
    if(i==7) return("southwest");
    return("");
}

//*********************************************************************
//                      canSave
//*********************************************************************

bool AreaRoom::canSave() const {
    int     i=0;

    if(unique.id)
        return(true);

    if(needsEffectsIndex())
        return(true);

    if(!exits.empty()) {
        for(Exit* ext : exits) {
            if(ext->getName() != exitNameByOrder(i++))
                return(true);
            // doesnt check rooms leading to other area rooms
            if(ext->target.room.id)
                return(true);
        }
        if(i != 8)
            return(true);
    }

    return(false);
}


//*********************************************************************
//                      getRandomWanderInfo
//*********************************************************************

WanderInfo* AreaRoom::getRandomWanderInfo() {
    std::list<AreaZone*>::iterator it;
    AreaZone *zone=nullptr;
    TileInfo* tile=nullptr;
    std::map<int, WanderInfo*> w;
    int i=0;

    // we want to randomly pick one of the zones
    for(it = area->zones.begin() ; it != area->zones.end() ; it++) {
        zone = (*it);
        if(zone->wander.getTraffic() && zone->inside(area, &mapmarker)) {
            w[i++] = &zone->wander;
        }
    }

    tile = area->getTile(area->getTerrain(nullptr, &mapmarker, 0, 0, 0, true), area->getSeasonFlags(&mapmarker));
    if(tile && tile->wander.getTraffic())
        w[i++] = &tile->wander;

    if(!i)
        return(nullptr);
    return(w[Random::get(0,i-1)]);
}


//*********************************************************************
//                      getWanderInfo
//*********************************************************************

WanderInfo* BaseRoom::getWanderInfo() {
    UniqueRoom* uRoom = getAsUniqueRoom();
    if(uRoom)
        return(&uRoom->wander);
    AreaRoom* aRoom = getAsAreaRoom();
    if(aRoom)
        return(aRoom->getRandomWanderInfo());
    return(nullptr);
}

//*********************************************************************
//                      delExit
//*********************************************************************

bool BaseRoom::delExit(Exit *exit) {
    if(exit) {
        auto xit = std::find(exits.begin(), exits.end(), exit);
        if(xit != exits.end()) {
            exits.erase(xit);
            delete exit;
            return(true);
        }
    }
    return(false);
}
bool BaseRoom::delExit( std::string_view dir) {
    for(Exit* ext : exits) {
        if(ext->getName() == dir) {
            exits.remove(ext);
            delete ext;
            return(true);
        }
    }

    return(false);
}


void BaseRoom::clearExits() {
    ExitList::iterator xit;
    for(xit = exits.begin() ; xit != exits.end(); ) {
        Exit* exit = (*xit++);
        if(exit->flagIsSet(X_PORTAL)) {
            BaseRoom* target = exit->target.loadRoom();
            Move::deletePortal(target, exit->getPassPhrase());
        }
        delete exit;
    }
    exits.clear();
}

//*********************************************************************
//                      canDelete
//*********************************************************************

bool AreaRoom::canDelete() {
    // don't delete unique rooms
    if(stayInMemory || unique.id)
        return(false);
    if(!monsters.empty())
        return(false);
    if(!players.empty())
        return(false);
    for(Object* obj : objects) {
        if(!obj->flagIsSet(O_DISPOSABLE))
            return(false);
    }
    // any room effects?
    if(needsEffectsIndex())
        return(false);
    if(!exits.empty()) {
        int     i=0;
        for(Exit* ext : exits) {
            if(ext->getName() != exitNameByOrder(i++))
                return(false);
            // doesnt check rooms leading to other area rooms
            if(ext->target.room.id)
                return(false);
        }
        if(i != 8)
            return(false);
    }
    return(true);
}

//*********************************************************************
//                  isInteresting
//*********************************************************************
// This will determine whether the looker will see anything of
// interest in the room. This is used by cmdScout.

bool AreaRoom::isInteresting(const Player *viewer) const {
    int     i=0;

    if(unique.id)
        return(true);

    for(Player* ply : players) {
        if(!ply->flagIsSet(P_HIDDEN) && viewer->canSee(ply))
            return(true);
    }
    for(Monster* mons : monsters) {
        if(!mons->flagIsSet(M_HIDDEN) && viewer->canSee(mons))
            return(true);
    }
    i = 0;
    for(Exit* ext : exits) {
        if(i < 7 && ext->getName() != exitNameByOrder(i))
            return(true);
        if(i >= 8) {
            if(viewer->showExit(ext))
                return(true);
        }
        i++;
    }
    // Fewer than 8 exits
    if( i < 7 )
        return(true);

    return(false);
}


bool AreaRoom::flagIsSet(int flag) const {
    MapMarker m = mapmarker;
    return(area->flagIsSet(flag, &m));
}

void AreaRoom::setFlag(int flag) {
    std::clog << "Trying to set a flag on an area room!" << std::endl;
}
bool UniqueRoom::flagIsSet(int flag) const {
    return(flags[flag/8] & 1<<(flag%8));
}
void UniqueRoom::setFlag(int flag) {
    flags[flag/8] |= 1<<(flag%8);
}
void UniqueRoom::clearFlag(int flag) {
    flags[flag/8] &= ~(1<<(flag%8));
}
bool UniqueRoom::toggleFlag(int flag) {
    if(flagIsSet(flag))
        clearFlag(flag);
    else
        setFlag(flag);
    return(flagIsSet(flag));
}


//*********************************************************************
//                      isMagicDark
//*********************************************************************

bool BaseRoom::isMagicDark() const {
    if(flagIsSet(R_MAGIC_DARKNESS))
        return(true);

    // check for darkness spell
    for(Player* ply : players) {
        // darkness spell on staff does nothing
        if(ply->isEffected("darkness") && !ply->isStaff())
            return(true);
        if(ply->flagIsSet(P_DARKNESS) && !ply->flagIsSet(P_DM_INVIS))
            return(true);
    }
    for(Monster* mons : monsters) {
        if(mons->isEffected("darkness"))
            return(true);
        if(mons->flagIsSet(M_DARKNESS))
            return(true);
    }
    for(Object *obj : objects) {
        if(obj->flagIsSet(O_DARKNESS))
            return(true);
    }
    return(false);
}


//*********************************************************************
//                      isNormalDark
//*********************************************************************

bool BaseRoom::isNormalDark() const {
    if(flagIsSet(R_DARK_ALWAYS))
        return(true);

    if(!isDay() && flagIsSet(R_DARK_AT_NIGHT))
        return(true);

    return(false);
}


void BaseRoom::addExit(Exit *ext) {
    ext->setRoom(this);

    exits.push_back(ext);
}

//
// This function checks the status of the exits in a room.  If any of
// the exits are closable or lockable, and the correct time interval
// has occurred since the last opening/unlocking, then the doors are
// re-shut/re-closed.
//
void BaseRoom::checkExits() {
    long    t = time(nullptr);

    for(Exit* ext : exits) {
        if( ext->flagIsSet(X_LOCKABLE) && (ext->ltime.ltime + ext->ltime.interval) < t)
        {
            ext->setFlag(X_LOCKED);
            ext->setFlag(X_CLOSED);
        } else if(  ext->flagIsSet(X_CLOSABLE) && (ext->ltime.ltime + ext->ltime.interval) < t)
            ext->setFlag(X_CLOSED);
    }
}

//*********************************************************************
//                      deityRestrict
//*********************************************************************
// if our particular flag is set, fail = 0
// else if ANY OTHER flags are set, fail = 1

bool BaseRoom::deityRestrict(const Creature* creature) const {

    // if no flags are set
    if( !flagIsSet(R_ARAMON) &&
        !flagIsSet(R_CERIS) &&
        !flagIsSet(R_ENOCH) &&
        !flagIsSet(R_GRADIUS) &&
        !flagIsSet(R_KAMIRA) &&
        !flagIsSet(R_ARES) &&
        !flagIsSet(R_ARACHNUS) &&
        !flagIsSet(R_LINOTHAN) )
        return(false);

    // if the deity flag is set and they match, they pass
    if( (flagIsSet(R_ARAMON) && creature->getDeity() == ARAMON) ||
        (flagIsSet(R_CERIS) && creature->getDeity() == CERIS) ||
        (flagIsSet(R_ENOCH) && creature->getDeity() == ENOCH) ||
        (flagIsSet(R_GRADIUS) && creature->getDeity() == GRADIUS) ||
        (flagIsSet(R_KAMIRA) && creature->getDeity() == KAMIRA) ||
        (flagIsSet(R_ARES) && creature->getDeity() == ARES) ||
        (flagIsSet(R_ARACHNUS) && creature->getDeity() == ARACHNUS) ||
        (flagIsSet(R_LINOTHAN) && creature->getDeity() == LINOTHAN) )
        return(false);

    return(true);
}



//*********************************************************************
//                          maxCapacity
//*********************************************************************
// 0 means no limit

int BaseRoom::maxCapacity() const {
    if(flagIsSet(R_ONE_PERSON_ONLY))
        return(1);
    if(flagIsSet(R_TWO_PEOPLE_ONLY))
        return(2);
    if(flagIsSet(R_THREE_PEOPLE_ONLY))
        return(3);
    return(0);
}

//*********************************************************************
//                          isFull
//*********************************************************************
// Can the player fit in the room?

bool BaseRoom::isFull() const {
    return(maxCapacity() && countVisPly() >= maxCapacity());
}

//*********************************************************************
//                      countVisPly
//*********************************************************************
// This function counts the number of (non-DM-invisible) players in a
// room and returns that number.

int BaseRoom::countVisPly() const {
    int     num = 0;

    for(Player* ply : players) {
        if(!ply->flagIsSet(P_DM_INVIS))
            num++;
    }

    return(num);
}


//*********************************************************************
//                      countCrt
//*********************************************************************
// This function counts the number of creatures in a
// room and returns that number.

int BaseRoom::countCrt() const {
    int num = 0;

    for(Monster* mons : monsters) {
        if(!mons->isPet())
            num++;
    }

    return(num);
}


//*********************************************************************
//                      getMaxMobs
//*********************************************************************

int BaseRoom::getMaxMobs() const {
    const UniqueRoom* room = getAsConstUniqueRoom();
    if(!room)
        return(MAX_MOBS_IN_ROOM);
    return(room->getMaxMobs() ? room->getMaxMobs() : MAX_MOBS_IN_ROOM);
}


//*********************************************************************
//                      vampCanSleep
//*********************************************************************

bool BaseRoom::vampCanSleep(Socket* sock) const {
    // not at night
    if(!isDay()) {
        sock->print("Your thirst for blood keeps you from sleeping.\n");
        return(false);
    }
    // during the day, under certain circumstances
    if(isMagicDark())
        return(true);
    if( flagIsSet(R_DARK_ALWAYS) ||
        flagIsSet(R_INDOORS) ||
        flagIsSet(R_LIMBO) ||
        flagIsSet(R_VAMPIRE_COVEN) ||
        flagIsSet(R_UNDERGROUND) )
        return(true);
    sock->print("The sunlight prevents you from sleeping.\n");
    return(false);
}


//*********************************************************************
//                      isCombat
//*********************************************************************
// checks to see if there is any time of combat going on in this room

bool BaseRoom::isCombat() const {

    for(Player* ply : players) {
        if(ply->inCombat(true))
            return(true);
    }
    for(Monster* mons : monsters) {
        if(mons->inCombat(true))
            return(true);
    }

    return(false);
}


//*********************************************************************
//                      isUnderwater
//*********************************************************************

bool BaseRoom::isUnderwater() const {
    return(flagIsSet(R_UNDER_WATER_BONUS));
}


//*********************************************************************
//                      isOutdoors
//*********************************************************************

bool BaseRoom::isOutdoors() const {
    return(!flagIsSet(R_INDOORS) && !flagIsSet(R_VAMPIRE_COVEN) && !flagIsSet(R_SHOP_STORAGE) && !flagIsSet(R_LIMBO));
}


//*********************************************************************
//                      magicBonus
//*********************************************************************

bool BaseRoom::magicBonus() const {
    return(flagIsSet(R_MAGIC_BONUS));
}


//*********************************************************************
//                      isForest
//*********************************************************************

bool BaseRoom::isForest() const {
    return(flagIsSet(R_FOREST_OR_JUNGLE));
}

//*********************************************************************
//                      isWater
//*********************************************************************
// drowning in area rooms is different than drowning in unique water rooms,
// therefore we need a different isWater function

bool AreaRoom::isWater() const {
    return(area->isWater(mapmarker.getX(), mapmarker.getY(), mapmarker.getZ(), true));
}

//*********************************************************************
//                      isRoad
//*********************************************************************

bool AreaRoom::isRoad() const {
    return(area->isRoad(mapmarker.getX(), mapmarker.getY(), mapmarker.getZ(), true));
}

//*********************************************************************
//                      getUnique
//*********************************************************************
// creature is allowed to be null

CatRef AreaRoom::getUnique(Creature *creature, bool skipDec) {
    if(!needsCompass)
        return(unique);
    CatRef  cr;

    if(!creature)
        return(cr);

    bool pet = creature->isPet();
    Player* player = creature->getPlayerMaster();

    if( !player ||
        !player->ready[HELD-1] ||
        player->ready[HELD-1]->getShotsCur() < 1 ||
        !player->ready[HELD-1]->compass ||
        *player->ready[HELD-1]->compass != *&mapmarker
    )
        return(cr);

    // no need to decrement twice if their pet is going through
    if(!skipDec && decCompass && !pet) {
        player->ready[HELD-1]->decShotsCur();
        player->breakObject(player->ready[HELD-1], HELD);
    }
    return(unique);
}

bool BaseRoom::isDropDestroy() const {
    if(!flagIsSet(R_DESTROYS_ITEMS))
        return(false);
    const AreaRoom* aRoom = getAsConstAreaRoom();
    return !(aRoom &&
             aRoom->area->isRoad(aRoom->mapmarker.getX(), aRoom->mapmarker.getY(), aRoom->mapmarker.getZ(), true));
}
bool BaseRoom::hasRealmBonus(Realm realm) const {
    return(flagIsSet(R_ROOM_REALM_BONUS) && flagIsSet(getRealmRoomBonusFlag(realm)));
}
bool BaseRoom::hasOppositeRealmBonus(Realm realm) const {
    return(flagIsSet(R_OPPOSITE_REALM_BONUS) && flagIsSet(getRealmRoomBonusFlag(getOppositeRealm(realm ))));
}


Monster* BaseRoom::getTollkeeper() const {
    for(Monster* mons : monsters) {
        if(!mons->isPet() && mons->flagIsSet(M_TOLLKEEPER))
            return(mons->getAsMonster());
    }
    return(nullptr);
}

//*********************************************************************
//                      isSunlight
//*********************************************************************
// the first version is isSunlight is only called when an AreaRoom
// does not exist - when rogues scout, for example

bool Area::isSunlight(const MapMarker* mapmarker) const {
    if(!isDay())
        return(false);

    // if their room has any of the following flags, no kill
    if( flagIsSet(R_DARK_ALWAYS, mapmarker) ||
        flagIsSet(R_UNDERGROUND, mapmarker) ||
        flagIsSet(R_INDOORS, mapmarker) ||
        flagIsSet(R_LIMBO, mapmarker) ||
        flagIsSet(R_VAMPIRE_COVEN, mapmarker) ||
        flagIsSet(R_ETHEREAL_PLANE, mapmarker) ||
        flagIsSet(R_IS_STORAGE_ROOM, mapmarker) ||
        flagIsSet(R_MAGIC_DARKNESS, mapmarker)
    )
        return(false);

    return(true);
}

// this version of isSunlight is called when a room exists

bool BaseRoom::isSunlight() const {

    // !this - for offline players. make it not sunny so darkmetal isnt destroyed
    if(!this)
        return(false);

    if(!isDay() || !isOutdoors() || isMagicDark())
        return(false);

    const UniqueRoom* uRoom = getAsConstUniqueRoom();
    const CatRefInfo* cri=nullptr;
    if(uRoom)
        cri = gConfig->getCatRefInfo(uRoom->info.area);

    // be nice - no killing darkmetal in special rooms
    if(cri && (
        !uRoom->info.id ||
        uRoom->info.id == cri->getRecall() ||
        uRoom->info.id == cri->getLimbo()
    ) )
        return(false);

    // if their room has any of the following flags, no kill
    if( flagIsSet(R_DARK_ALWAYS) ||
        flagIsSet(R_UNDERGROUND) ||
        flagIsSet(R_ETHEREAL_PLANE) ||
        flagIsSet(R_IS_STORAGE_ROOM)
    )
        return(false);

    return(true);
}

//*********************************************************************
//                      destroy
//*********************************************************************

void UniqueRoom::destroy() {
    saveToFile(0, LoadType::LS_BACKUP);
    char    filename[256];
    strcpy(filename, roomPath(info));
    expelPlayers(true, true, true);
    gServer->roomCache.remove(info);
    unlink(filename);
}

//*********************************************************************
//                      expelPlayers
//*********************************************************************

void BaseRoom::expelPlayers(bool useTrapExit, bool expulsionMessage, bool expelStaff) {
    Player* target=nullptr;
    BaseRoom* newRoom=nullptr;
    UniqueRoom* uRoom=nullptr;

    // allow them to be sent to the room's trap exit
    if(useTrapExit) {
        uRoom = getAsUniqueRoom();
        if(uRoom) {
            CatRef cr = uRoom->getTrapExit();
            uRoom = nullptr;
            if(cr.id && loadRoom(cr, &uRoom))
                newRoom = uRoom;
        }
    }

    auto pIt = players.begin();
    auto pEnd = players.end();
    while(pIt != pEnd) {
        target = (*pIt++);

        if(!expelStaff && target->isStaff())
            continue;

        if(expulsionMessage) {
            target->printColor("^rYou are expelled from this room as it collapses in on itself!\n");
            broadcast(target->getSock(), this, "^r%M is expelled from the room as it collapses in on itself!", target);
        } else {
            target->printColor("^CShifting dimensional forces displace you from this room.\n");
            broadcast(target->getSock(), this, "^CShifting dimensional forces displace %N from this room.", target);
        }

        if(!useTrapExit || !newRoom) {
            newRoom = target->bound.loadRoom(target);
            if(!newRoom || newRoom == this)
                newRoom = abortFindRoom(target, "expelPlayers");
        }

        target->deleteFromRoom();
        target->addToRoom(newRoom);
        target->doPetFollow();
    }
}


//*********************************************************************
//                      getSpecialArea
//*********************************************************************

Location getSpecialArea(int (CatRefInfo::*toCheck), const CatRef& cr) {
    return(getSpecialArea(toCheck, nullptr, cr.area, cr.id));
}

Location getSpecialArea(int (CatRefInfo::*toCheck), const Creature* creature, std::string_view area, short id) {
    Location l;

    if(creature) {
        if(creature->inAreaRoom()) {
            l.room.setArea("area");
            l.room.id = creature->getConstAreaRoomParent()->area->id;
        } else {
            l.room.setArea(creature->currentLocation.room.area);
        }
    }
    if(!area.empty())
        l.room.setArea(std::string(area));
    if(id)
        l.room.id = id;


    const CatRefInfo* cri = gConfig->getCatRefInfo(l.room.area, l.room.id, true);

    if(!cri)
        cri = gConfig->getCatRefInfo(gConfig->getDefaultArea());
    if(cri) {
        l.room.setArea(cri->getArea());
        l.room.id = (cri->*toCheck);
    } else {
        // failure!
        l.room.setArea(gConfig->getDefaultArea());
        l.room.id = 0;
    }

    return(l);
}

//*********************************************************************
//                      getLimboRoom
//*********************************************************************

Location Creature::getLimboRoom() const {
    return(getSpecialArea(&CatRefInfo::limbo, this, "", 0));
}

//*********************************************************************
//                      getRecallRoom
//*********************************************************************

Location Creature::getRecallRoom() const {
    const Player* player = getAsConstPlayer();
    if(player && (player->flagIsSet(P_T_TO_BOUND) || player->getClass() == CreatureClass::BUILDER))
        return(player->bound);
    return(getSpecialArea(&CatRefInfo::recall, this, "", 0));
}


//*********************************************************************
//                      print
//*********************************************************************

bool hearBroadcast(Creature* target, Socket* ignore1, Socket* ignore2, bool showTo(Socket*));

void BaseRoom::print(Socket* ignore, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    doPrint(nullptr, ignore, nullptr, fmt, ap);
    va_end(ap);
}
void BaseRoom::print(Socket* ignore1, Socket* ignore2, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    doPrint(nullptr, ignore1, ignore2, fmt, ap);
    va_end(ap);
}

//*********************************************************************
//                      doPrint
//*********************************************************************

void BaseRoom::doPrint(bool showTo(Socket*), Socket* ignore1, Socket* ignore2, const char *fmt, va_list ap) {
    for(Player* ply : players) {
        if(!hearBroadcast(ply, ignore1, ignore2, showTo))
            continue;
        if(ply->flagIsSet(P_UNCONSCIOUS))
            continue;

        ply->vprint(ply->customColorize(fmt).c_str(), ap);

    }
}


//*********************************************************************
//                      isOutlawSafe
//*********************************************************************

bool BaseRoom::isOutlawSafe() const {
    return( flagIsSet(R_OUTLAW_SAFE) ||
            flagIsSet(R_VAMPIRE_COVEN) ||
            flagIsSet(R_LIMBO) ||
            hasTraining()
    );
}

//*********************************************************************
//                      isOutlawSafe
//*********************************************************************

bool BaseRoom::isPkSafe() const {
    return( flagIsSet(R_SAFE_ROOM) ||
            flagIsSet(R_VAMPIRE_COVEN) ||
            flagIsSet(R_LIMBO)
    );
}

//*********************************************************************
//                      isOutlawSafe
//*********************************************************************

bool BaseRoom::isFastTick() const {
    return( flagIsSet(R_FAST_HEAL) ||
            flagIsSet(R_LIMBO)
    );
}

//*********************************************************************
//                      canPortHere
//*********************************************************************

bool UniqueRoom::canPortHere(const Creature* creature) const {
    // check creature-specific settings
    if(creature) {
        if(size && creature->getSize() && creature->getSize() > size)
            return(false);
        if(deityRestrict(creature))
            return(false);
        if(getLowLevel() > creature->getLevel())
            return(false);
        if(getHighLevel() && creature->getLevel() > getHighLevel())
            return(false);
    }

    // check room-specific settings
    if( flagIsSet(R_NO_TELEPORT) ||
        flagIsSet(R_LIMBO) ||
        flagIsSet(R_VAMPIRE_COVEN) ||
        flagIsSet(R_SHOP_STORAGE) ||
        flagIsSet(R_JAIL) ||
        flagIsSet(R_IS_STORAGE_ROOM) ||
        flagIsSet(R_ETHEREAL_PLANE) ||
        isConstruction() ||
        hasTraining()
            )
        return(false);
    if(isFull())
        return(false);
    // artificial limits for the misc area
    if(info.isArea("misc") && info.id <= 1000)
        return(false);
    if(exits.empty())
        return(false);

    return(true);
}
