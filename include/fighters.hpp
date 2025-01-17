/*
 * Fighters.h
 *   Header file for the fighter class
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

#ifndef FIGHTERS_H
#define FIGHTERS_H

// What sort of action is being passed to increaseFocus
enum FocusAction {
    FOCUS_DAMAGE_IN,
    FOCUS_DAMAGE_OUT,
    FOCUS_DODGE,
    FOCUS_BLOCK,
    FOCUS_PARRY,
    FOCUS_RIPOSTE,
    FOCUS_CIRCLE,
    FOCUS_BASH,
    FOCUS_SPECIAL,

    LAST_FOCUS
};


#endif //FIGHTERS_H
