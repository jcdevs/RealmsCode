/*
 * skillGain.h
 *   Skill gain
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

#ifndef _SKILLGAIN_H
#define _SKILLGAIN_H

#include <map>
#include <libxml/parser.h>  // for xmlNodePtr


// Describes the skill gained and who gets it
class SkillGain {
public:
    SkillGain(xmlNodePtr rootNode);
    ~SkillGain();
    int getGained();
    std::string getName() const;
    bool deityIsAllowed(int deity);
    bool hasDeities();
protected:
    void load(xmlNodePtr rootNode);
    std::string skillName; // Name of the skill being gained
    int gainLevel{}; // Gained level they start with

    std::map<int, bool> deities;
};


#endif  /* _SKILLGAIN_H */

