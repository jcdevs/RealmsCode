/*
 * Container.h
 *   Classes to handle classes that can be containers or contained by a container
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

#ifndef CONTAINER_H_
#define CONTAINER_H_

#include "mudObject.hpp"

#include <set>

class AreaRoom;
class BaseRoom;
class Containable;
class Container;
class EffectInfo;
class Monster;
class MudObject;
class Object;
class Player;
class Socket;
class UniqueRoom;

class cmd;

struct PlayerPtrLess : public std::binary_function<const Player*, const Player*, bool> {
    bool operator()(const Player* lhs, const Player* rhs) const;
};

struct MonsterPtrLess : public std::binary_function<const Monster*, const Monster*, bool> {
    bool operator()(const Monster* lhs, const Monster* rhs) const;
};

struct ObjectPtrLess : public std::binary_function<const Object*, const Object*, bool> {
    bool operator()(const Object* lhs, const Object* rhs) const;
};

typedef std::set<Player*, PlayerPtrLess> PlayerSet;
typedef std::set<Monster*, MonsterPtrLess> MonsterSet;
typedef std::set<Object*, ObjectPtrLess> ObjectSet;

// Any container or containable item is a MudObject.  Since an object can be both a container and containable...
// make sure we use virtual MudObject as the parent to avoid the "dreaded" diamond

class Container : public virtual MudObject {
public:
    Container();
    ~Container() override = default;

    PlayerSet players;
    MonsterSet monsters;
    ObjectSet objects;

    bool purge(bool includePets = false);
    bool purgeMonsters(bool includePets = false);
    bool purgeObjects();


    Container* remove(Containable* toRemove);
    bool add(Containable* toAdd);


    void registerContainedItems() override;
    void unRegisterContainedItems() override;


    bool checkAntiMagic(Monster* ignore = nullptr);

    void doSocialEcho(std::string str, const Creature* actor, const Creature* target = nullptr);

    void effectEcho(const std::string &fmt, const MudObject* actor = nullptr, const MudObject* applier = nullptr, Socket* ignore = nullptr);

    void wake(const std::string &str, bool noise) const;


    std::string listObjects(const Player* player, bool showAll, char endColor ='x' ) const;

    // Find routines
    Creature* findCreaturePython(Creature* searcher, const std::string& name, bool monFirst = true, bool firstAggro = false, bool exactMatch = false );
    Creature* findCreature(const Creature* searcher,  cmd* cmnd, int num=1) const;
    Creature* findCreature(const Creature* searcher, const std::string& name, int num, bool monFirst = true, bool firstAggro = false, bool exactMatch = false) const;
    Creature* findCreature(const Creature* searcher, const std::string& name, int num, bool monFirst, bool firstAggro, bool exactMatch, int& match) const;
    Monster* findMonster(const Creature* searcher,  cmd* cmnd, int num=1) const;
    Monster* findMonster(const Creature* searcher, const std::string& name, int num, bool firstAggro = false, bool exactMatch = false) const;
    Monster* findMonster(const Creature* searcher, const std::string& name, int num, bool firstAggro, bool exactMatch, int& match) const;
    Player* findPlayer(const Creature* searcher,  cmd* cmnd, int num=1) const;
    Player* findPlayer(const Creature* searcher, const std::string& name, int num, bool exactMatch = false) const;
    Player* findPlayer(const Creature* searcher, const std::string& name, int num, bool exactMatch, int& match) const;

    Object* findObject(const Creature *searcher, const cmd* cmnd, int val) const;
    Object* findObject(const Creature* searcher, const std::string& name, int num, bool exactMatch = false) const;
    Object* findObject(const Creature* searcher, const std::string& name, int num, bool exactMatch, int& match) const;

    MudObject* findTarget(const Creature* searcher,  cmd* cmnd, int num=1) const;
    MudObject* findTarget(const Creature* searcher,  const std::string& name, int num, bool monFirst= true, bool firstAggro = false, bool exactMatch = false) const;
    MudObject* findTarget(const Creature* searcher,  const std::string& name, int num, bool monFirst, bool firstAggro, bool exactMatch, int& match) const;

};

class Containable : public virtual MudObject {
public:
    Containable();
    ~Containable() override = default;

    bool addTo(Container* container);
    Container* removeFrom();

    void setParent(Container* container);

    Container* getParent() const;


    // What type of parent are we contained in?
    [[nodiscard]] bool inRoom() const;
    [[nodiscard]] bool inUniqueRoom() const;
    [[nodiscard]] bool inAreaRoom() const;
    [[nodiscard]] bool inObject() const;
    [[nodiscard]] bool inPlayer() const;
    [[nodiscard]] bool inMonster() const;
    [[nodiscard]] bool inCreature() const;

    BaseRoom* getRoomParent();
    UniqueRoom* getUniqueRoomParent();
    AreaRoom* getAreaRoomParent();
    Object* getObjectParent();
    Player* getPlayerParent();
    Monster* getMonsterParent();
    Creature* getCreatureParent();

    [[nodiscard]] const BaseRoom* getConstRoomParent() const;
    [[nodiscard]] const UniqueRoom* getConstUniqueRoomParent() const;
    [[nodiscard]] const AreaRoom* getConstAreaRoomParent() const;
    [[nodiscard]] const Object* getConstObjectParent() const;
    [[nodiscard]] const Player* getConstPlayerParent() const;
    [[nodiscard]] const Monster* getConstMonsterParent() const;
    [[nodiscard]] const Creature* getConstCreatureParent() const;

protected:
    Container* parent;   // Parent Container

    // Last parent is only used in removeFromSet and addToSet and should not be used anywhere else
    Container* lastParent;
    void removeFromSet() override;
    void addToSet() override;

};

#endif /* CONTAINER_H_ */
