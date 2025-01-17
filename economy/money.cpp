/*
 * money.cpp
 *   Money
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

#include <boost/algorithm/string/case_conv.hpp>     // for to_lower_copy
#include <boost/iterator/iterator_facade.hpp>       // for operator!=
#include <boost/lexical_cast/bad_lexical_cast.hpp>  // for bad_lexical_cast
#include <locale>                                   // for locale
#include <ostream>                                  // for operator<<, strin...
#include <string>                                   // for string, allocator

#include "money.hpp"                                // for Money, Coin, MAX_...
#include "proto.hpp"                                // for zero
#include "utils.hpp"                                // for MIN
#include "xml.hpp"                                  // for loadNumArray, sav...

//*********************************************************************
//                      Money
//*********************************************************************

Money::Money() {
    zero();
}

// Create some new money with n amount of Coin c
Money::Money(unsigned long n, Coin c) {
    zero();
    set(n,c);
}
//*********************************************************************
//                      load
//*********************************************************************

void Money::load(xmlNodePtr curNode) {
    zero();
    xml::loadNumArray<unsigned long>(curNode, m, "Coin", MAX_COINS+1);
}

//*********************************************************************
//                      save
//*********************************************************************

void Money::save(const char* name, xmlNodePtr curNode) const {
    saveULongArray(curNode, name, "Coin", m, MAX_COINS+1);
}

//*********************************************************************
//                      isZero
//*********************************************************************

bool Money::isZero() const {
    for(Coin i = MIN_COINS; i < MAX_COINS; i = (Coin)((int)i + 1))
        if(m[i])
            return(false);
    return(true);
}

//*********************************************************************
//                      zero
//*********************************************************************

void Money::zero() { ::zero(m, sizeof(m)); }

//*********************************************************************
//                      operators
//*********************************************************************

bool Money::operator==(const Money& mn) const {
    for(Coin i = MIN_COINS; i < MAX_COINS; i = (Coin)((int)i + 1))
        if(m[i] != mn.get(i))
            return(false);
    return(true);
}

bool Money::operator!=(const Money& mn) const {
    return(!(*this==mn));
}

unsigned long Money::operator[](Coin c) const { return(m[c]); }

//*********************************************************************
//                      get
//*********************************************************************

unsigned long Money::get(Coin c) const { return(m[c]); }

//*********************************************************************
//                      add
//*********************************************************************

void Money::add(unsigned long n, Coin c) { set(m[c] + n, c); }

void Money::add(Money mn) {
    for(Coin i = MIN_COINS; i < MAX_COINS; i = (Coin)((int)i + 1))
        add(mn[i], i);
}

//*********************************************************************
//                      sub
//*********************************************************************

void Money::sub(unsigned long n, Coin c) { set(m[c] - n, c); }

void Money::sub(Money mn) {
    for(Coin i = MIN_COINS; i < MAX_COINS; i = (Coin)((int)i + 1))
        sub(mn[i], i);
}

//*********************************************************************
//                      set
//*********************************************************************

void Money::set(unsigned long n, Coin c) { m[c] = MIN(2000000000UL, n); }

void Money::set(Money mn) {
    for(Coin i = MIN_COINS; i < MAX_COINS; i = (Coin)((int)i + 1))
        set(mn[i], i);
}

//*********************************************************************
//                      str
//*********************************************************************

std::string Money::str() const {
    bool found=false;
    std::stringstream oStr;
    oStr.imbue(std::locale(""));

    for(Coin i = MIN_COINS; i < MAX_COINS; i = (Coin)((int)i + 1)) {
        if(m[i]) {
            if(found)
                oStr << ", ";
            found = true;
            oStr << m[i] << " " << boost::algorithm::to_lower_copy(coinNames(i)) << " coin" << (m[i] != 1 ? "s" : "");
        }
    }

    if(!found)
        oStr << "0 coins";

    return(oStr.str());
}

//*********************************************************************
//                      coinNames
//*********************************************************************

std::string Money::coinNames(Coin c) {
    switch(c) {
    case COPPER:
        return("Copper");
    case SILVER:
        return("Silver");
    case GOLD:
        return("Gold");
    case PLATINUM:
        return("Platinum");
    case ALANTHIUM:
        return("Alanthium");
    default:
        break;
    }
    return("");
}
