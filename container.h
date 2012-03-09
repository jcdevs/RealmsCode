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
 *  Creative Commons - Attribution - Non Commercial - Share Alike 3.0 License
 *    http://creativecommons.org/licenses/by-nc-sa/3.0/
 *
 *  Copyright (C) 2007-2012 Jason Mitchell, Randi Mitchell
 *     Contributions by Tim Callahan, Jonathan Hseu
 *  Based on Mordor (C) Brooke Paul, Brett J. Vickers, John P. Freeman
 *
 */

#ifndef CONTAINER_H_
#define CONTAINER_H_

#include "mudObject.h"

#include <set>

class Player;
class Monster;
class Object;
class Containable;
class Container;
class MudObject;
class BaseRoom;
class AreaRoom;
class UniqueRoom;
class EffectInfo;
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
    virtual ~Container() {};

    PlayerSet players;
    MonsterSet monsters;
    ObjectSet objects;

    bool remove(Containable* toRemove);
    bool add(Containable* toAdd);
    bool checkAntiMagic(Monster* ignore = 0);

    void doSocialEcho(bstring str, const Creature* actor, const Creature* target = null);

	void effectEcho(bstring fmt, const MudObject* actor = NULL, const MudObject* applier = null, Socket* ignore = null);

//    virtual bool flagIsSet(int flag) const = 0;
//    virtual void setFlag(int flag) = 0;
//    virtual bool isEffected(const bstring& effect) const = 0;
//    virtual bool isEffected(EffectInfo*) const = 0;

	void wake(bstring str, bool noise) const;

	// Find routines
	Creature* findCreaturePython(Creature* searcher, const bstring& name, bool monFirst = true, bool firstAggro = false, bool exactMatch = false );
    Creature* findCreature(Creature* searcher, const cmd* cmnd, int num=1);
	Creature* findCreature(Creature* searcher, const bstring& name, const int num, bool monFirst = true, bool firstAggro = false, bool exactMatch = false);
	Creature* findCreature(Creature* searcher, const bstring& name, const int num, bool monFirst, bool firstAggro, bool exactMatch, int& match);
	Monster* findMonster(Creature* searcher, const cmd* cmnd, int num=1);
	Monster* findMonster(Creature* searcher, const bstring& name, const int num, bool firstAggro = false, bool exactMatch = false);
	Monster* findMonster(Creature* searcher, const bstring& name, const int num, bool firstAggro, bool exactMatch, int& match);
	Player* findPlayer(Creature* searcher, const cmd* cmnd, int num=1);
	Player* findPlayer(Creature* searcher, const bstring& name, const int num, bool exactMatch = false);
	Player* findPlayer(Creature* searcher, const bstring& name, const int num, bool exactMatch, int& match);
	MudObject* findTarget(Creature* searcher, const cmd* cmnd, int num=1);
	MudObject* findTarget(Creature* searcher,  const bstring& name, const int num, bool monFirst= true, bool firstAggro = false, bool exactMatch = false);
	MudObject* findTarget(Creature* searcher,  const bstring& name, const int num, bool monFirst, bool firstAggro, bool exactMatch, int& match);



};

class Containable : public virtual MudObject {
public:
    Containable();
    virtual ~Containable() {};

    bool addTo(Container* container);
    bool removeFrom(void);

    void setParent(Container* container);

    Container* getParent() const;


	// What type of parent are we contained in?
	bool inRoom() const;
	bool inUniqueRoom() const;
	bool inAreaRoom() const;
	bool inObject() const;
	bool inPlayer() const;
	bool inMonster() const;
	bool inCreature() const;

	BaseRoom* getRoomParent();
	UniqueRoom* getUniqueRoomParent();
	AreaRoom* getAreaRoomParent();
	Object* getObjectParent();
	Player* getPlayerParent();
	Monster* getMonsterParent();
	Creature* getCreatureParent();

	const BaseRoom* getConstRoomParent() const;
	const UniqueRoom* getConstUniqueRoomParent() const;
	const AreaRoom* getConstAreaRoomParent() const;
	const Object* getConstObjectParent() const;
	const Player* getConstPlayerParent() const;
	const Monster* getConstMonsterParent() const;
	const Creature* getConstCreatureParent() const;

protected:
    Container* parent;   // Parent Container
};

#endif /* CONTAINER_H_ */
