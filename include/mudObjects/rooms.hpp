/*
 * rooms.h
 *   Header file for Exit / BaseRoom / AreaRoom / Room classes
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

#pragma once

// define some limits
#define NUM_RANDOM_SLOTS    50
#define NUM_PERM_SLOTS      50

class UniqueRoom;
class AreaRoom;
class Fishing;

#include "container.hpp"
#include "exits.hpp"
#include "global.hpp"
#include "enums/loadType.hpp"
#include "location.hpp"
#include "realm.hpp"
#include "size.hpp"
#include "track.hpp"
#include "wanderInfo.hpp"
#include "mudObjects/players.hpp"

typedef std::list<Exit*> ExitList;

class BaseRoom: public Container {
protected:
    void BaseDestroy();
    std::string version;    // What version of the mud this object was saved under
    bool tempNoKillDarkmetal;

public:
    //xtag  *first_ext;     // Exits
    ExitList exits;

    char    misc[64]{};       // miscellaneous space

public:
    BaseRoom();
    virtual ~BaseRoom() {};
//  virtual bool operator< (const MudObject& t) const = 0;

    void readExitsXml(xmlNodePtr curNode, bool offline=false);
    bool delExit(std::string_view dir);
    bool delExit(Exit *exit);
    void clearExits();

    [[nodiscard]] bool isSunlight() const;
    // handles darkmetal and unique
    void killMortalObjectsOnFloor();
    void killMortalObjects(bool floor=true);
    void killUniques();
    void setTempNoKillDarkmetal(bool noKillDarkmetal);
    void scatterObjects();

    [[nodiscard]] bool isCombat() const;
    [[nodiscard]] bool isConstruction() const;

    int saveExitsXml(xmlNodePtr curNode) const;


    [[nodiscard]] Monster* getGuardingExit(const Exit* exit, const Player* player) const;
    void addExit(Exit *ext);
    void checkExits();
    [[nodiscard]] bool deityRestrict(const Creature* creature) const;
    [[nodiscard]] int maxCapacity() const;
    [[nodiscard]] bool isFull() const;
    [[nodiscard]] int countVisPly() const;
    [[nodiscard]] int countCrt() const;
    [[nodiscard]] Monster* getTollkeeper() const;

    [[nodiscard]] bool isMagicDark() const;
    [[nodiscard]] bool isNormalDark() const;
    [[nodiscard]] bool isUnderwater() const;
    [[nodiscard]] bool isOutdoors() const;
    [[nodiscard]] bool isDropDestroy() const;
    [[nodiscard]] bool magicBonus() const;
    [[nodiscard]] bool isForest() const;
    [[nodiscard]] bool vampCanSleep(Socket* sock) const;
    [[nodiscard]] int getMaxMobs() const;
    [[nodiscard]] int dmInRoom() const;
    void arrangeExits(Player* player= nullptr);
    [[nodiscard]] bool isWinter() const;
    [[nodiscard]] bool isOutlawSafe() const;
    [[nodiscard]] bool isPkSafe() const;
    [[nodiscard]] bool isFastTick() const;


    [[nodiscard]] virtual bool flagIsSet(int flag) const = 0;
    virtual void setFlag(int flag) = 0;
    [[nodiscard]] virtual Size getSize() const = 0;
    [[nodiscard]] bool hasRealmBonus(Realm realm) const;
    [[nodiscard]] bool hasOppositeRealmBonus(Realm realm) const;
    WanderInfo* getWanderInfo();
    void expelPlayers(bool useTrapExit, bool expulsionMessage, bool expelStaff);

    [[nodiscard]] std::string fullName() const;
    [[nodiscard]] std::string getVersion() const;
    void setVersion(std::string_view v);
    [[nodiscard]] bool hasTraining() const;
    [[nodiscard]] CreatureClass whatTraining(int extra=0) const;

    virtual const Fishing* getFishing() const = 0;

    bool pulseEffects(time_t t);

    void addEffectsIndex();
    bool removeEffectsIndex();
    [[nodiscard]] bool needsEffectsIndex() const;


    void print(Socket* ignore, const char *fmt, ...);
    void print(Socket* ignore1, Socket* ignore2, const char *fmt, ...);

    virtual std::string getMsdp(bool showExits = true) const { return ""; };
    [[nodiscard]] std::string getExitsMsdp() const;
private:
    void doPrint(bool showTo(Socket*), Socket* ignore1, Socket* ignore2, const char *fmt, va_list ap);
};

