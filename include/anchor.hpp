/*
 * anchor.h
 *   Magical anchors
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

#ifndef _ANCHOR_H
#define _ANCHOR_H

#include "catRef.hpp"

class AreaRoom;
class BaseRoom;
class MapMarker;
class Player;
class UniqueRoom;

class Anchor {
public:
    Anchor();
    Anchor(std::string_view a, const Player* player);
    ~Anchor();
    void reset();

    Anchor& operator=(const Anchor& a);
    void load(xmlNodePtr curNode);
    void save(xmlNodePtr curNode) const;

    void bind(const Player* player);
    void bind(const UniqueRoom* uRoom);
    void bind(const AreaRoom* aRoom);
    void setRoom(const CatRef& r);

    bool is(const BaseRoom* room) const;
    bool is(const Player* player) const;
    bool is(const UniqueRoom* uRoom) const;
    bool is(const AreaRoom* aRoom) const;

    [[nodiscard]] std::string getAlias() const;
    [[nodiscard]] std::string getRoomName() const;
    [[nodiscard]] CatRef getRoom() const;
    [[nodiscard]] const MapMarker* getMapMarker() const;
protected:
    std::string alias;
    std::string roomName;
    CatRef room;
    MapMarker *mapmarker{};
};


#endif  /* _ANCHOR_H */

