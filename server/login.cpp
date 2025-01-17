/*
 * login.cpp
 *   Functions to log a person into the mud or create a new character
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

#include <fcntl.h>                               // for open, O_RDONLY
#include <fmt/format.h>                          // for format
#include <unistd.h>                              // for close, read
#include <boost/algorithm/string/predicate.hpp>  // for contains
#include <boost/algorithm/string/trim.hpp>       // for trim
#include <cctype>                                // for isalpha, tolower
#include <cstdio>                                // for sprintf
#include <cstdlib>                               // for atoi
#include <cstring>                               // for strcpy, strchr
#include <ctime>                                 // for time
#include <iomanip>                               // for operator<<, setw
#include <list>                                  // for list, operator==
#include <locale>                                // for locale
#include <map>                                   // for operator==, _Rb_tree...
#include <set>                                   // for set<>::iterator
#include <sstream>                               // for char_traits, operator<<
#include <string>                                // for string, allocator
#include <string_view>                           // for operator==, string_view
#include <type_traits>                           // for add_const<>::type
#include <utility>                               // for pair, tuple_element<...

#include "catRef.hpp"                            // for CatRef
#include "cmd.hpp"                               // for cmd
#include "commands.hpp"                          // for parse, cmdReconnect
#include "config.hpp"                            // for Config, gConfig, Rac...
#include "creatureStreams.hpp"                   // for Streamable, ColorOff
#include "deityData.hpp"                         // for DeityData
#include "flags.hpp"                             // for P_HARDCORE, O_STARTING
#include "free_crt.hpp"                          // for free_crt
#include "global.hpp"                            // for CreatureClass, Creat...
#include "lasttime.hpp"                          // for lasttime
#include "location.hpp"                          // for Location
#include "login.hpp"                             // for doPrint, doWork, CON...
#include "magic.hpp"                             // for Divine, S_SAP_LIFE
#include "money.hpp"                             // for Money
#include "mud.hpp"                               // for LT_AGE, MAX_LT, SONG...
#include "mudObjects/container.hpp"              // for ObjectSet
#include "mudObjects/creatures.hpp"              // for CustomCrt, Creature
#include "mudObjects/objects.hpp"                // for Object
#include "mudObjects/players.hpp"                // for Player
#include "os.hpp"                                // for merror
#include "paths.hpp"                             // for Config, CreateHelp
#include "playerClass.hpp"                       // for PlayerClass
#include "proto.hpp"                             // for low, get_class_string
#include "raceData.hpp"                          // for RaceData
#include "random.hpp"                            // for Random
#include "server.hpp"                            // for Server, gServer
#include "socket.hpp"                            // for Socket
#include "stats.hpp"                             // for Stat
#include "structs.hpp"                           // for SEX_FEMALE, SEX_MALE
#include "utils.hpp"                             // for MAX
#include "xml.hpp"                               // for loadPlayer, loadObject

class StartLoc;

/*
 * Generic get function, copy for future use
 *
bool Create::get(Socket* sock, std::string str, int mode) {
    if(mode == Create::doPrint) {

    } else if(mode == Create::doWork) {

    }
    return(true);
}
 *
 */

char allowedClassesStr[static_cast<int>(CreatureClass::CLASS_COUNT) + 4][16] =
    { "Assassin", "Berserker", "Cleric", "Fighter",
      "Mage", "Paladin", "Ranger", "Thief", "Pureblood", "Monk", "Death Knight",
      "Druid", "Lich", "Werewolf", "Bard", "Rogue", "Figh/Mage", "Figh/Thief",
      "Cler/Ass", "Mage/Thief", "Thief/Mage", "Cler/Figh", "Mage/Ass"
    };



//*********************************************************************
//                      cmdReconnect
//*********************************************************************

int cmdReconnect(Player* player, cmd* cmnd) {
    Socket* sock = player->getSock();

    player->print("\n\nReconnecting.\n\n\n");

    player->save(true);

    player->uninit();
    free_crt(player, true);
    sock->setPlayer(nullptr);

    sock->reconnect();
    return(0);
}

std::string::size_type checkProxyLogin(const std::string &str) {
    std::string::size_type x = std::string::npos;
    std::string::size_type n = str.find(" as ");
    if(n == x)
        n = str.find(" for ");
    return(n);
}
// Character used for access
std::string getProxyChar(const std::string &str, unsigned int n) {
    return(str.substr(0,n));
}
// Character being logged in
std::string getProxiedChar(const std::string &str, unsigned int n) {
    unsigned int m = str.find_first_of(" ", n+1);
    return(str.substr(m+1, str.length() - m - 1));
}

bool Player::checkProxyAccess(Player* proxy) {
    if(!proxy)
        return(false);

    // Only a DM can proxy a DM
    if(this->isDm() && !proxy->isDm())
        return(false);

    // A DM can proxy anybody
    if(proxy->isDm())
        return(true);

    return(gConfig->hasProxyAccess(proxy, this));
}
//*********************************************************************
//                      login
//*********************************************************************
// This function is the first function that gets input from a player when
// they log in. It asks for the player's name and password, and performs
// the according function calls.
unsigned const char echo_off[] = {255, 251, 1, 0};
unsigned const char echo_on[] = {255, 252, 1, 0};

void login(Socket* sock, const std::string& inStr) {
    Player  *player=nullptr;
    std::string::size_type proxyCheck = 0;
    if(!sock) {
        std::clog << "**** ERORR: Null socket in login.\n";
        return;
    }

    std::string str = inStr;


    switch(sock->getState()) {
    case LOGIN_DNS_LOOKUP:
        sock->print("Still performing DNS lookup, please be patient!\n");
        return;
    case LOGIN_GET_LOCKOUT_PASSWORD:
        if(str != sock->tempstr[0]) {
            sock->disconnect();
            return;
        }
        sock->askFor("Please enter name: ");

        sock->setState(LOGIN_GET_NAME);
//      sock->print("\n\nChoose - [A]nsi color, [M]irc color, [X] MXP + Ansi, or [N]o color\n: ");
//      sock->setState(LOGIN_GET_COLOR);
        return;
        // End LOGIN_GET_LOCKOUT_PASSWORD
    case LOGIN_GET_NAME:

        proxyCheck = checkProxyLogin(str);
        if(proxyCheck != std::string::npos) {
            std::string proxyChar = getProxyChar(str, proxyCheck);
            std::string proxiedChar = getProxiedChar(str, proxyCheck);
            lowercize(proxyChar, 1);
            lowercize(proxiedChar, 1);
            if(proxyChar == proxiedChar) {
                sock->askFor("That's just silly.\nPlease enter name: ");
                return;
            }
            if(!nameIsAllowed(proxyChar, sock) || !nameIsAllowed(proxiedChar, sock)) {
                sock->askFor("Please enter name: ");
                return;
            }
            if(!Player::exists(proxyChar)) {
                sock->println(proxyChar + " doesn't exist.");
                sock->askFor("Please enter name: ");
                return;
            }
            if(!Player::exists(proxiedChar)) {
                sock->println(proxiedChar + " doesn't exist.");
                sock->askFor("Please enter name: ");
                return;
            }
            if(!loadPlayer(proxiedChar, &player)) {
                sock->println(std::string("Error loading ") + proxiedChar + "\n");
                sock->askFor("Please enter name: ");
                return;
            }
            player->fd = -1;
            bool online = false;
            Player* proxy = nullptr;
            proxy = gServer->findPlayer(proxyChar);
            if(proxy)
                online = true;
            else {
                if(!loadPlayer(proxyChar, &proxy)) {
                    sock->println(std::string("Error loading ") + proxyChar + "\n");
                    free_crt(player, false);
                    sock->askFor("Please enter name: ");
                    return;
                }
            }
            if(!player->checkProxyAccess(proxy)) {
                sock->println(std::string(proxy->getName()) + " does not have access to " + player->getName());
                free_crt(player, false);
                if(!online)
                    free_crt(proxy, false);
                sock->askFor("Please enter name: ");
                return;
            }

            player->fd = -1;
            //gServer->addPlayer(player);
            sock->setPlayer(player);

            if(gServer->checkDuplicateName(*sock, false)) {
                // Don't free player here or ask for name again because checkDuplicateName does that
                // We only need to worry about freeing proxy
                if(!online)
                    free_crt(proxy, false);
                return;
            }
            sock->println(std::string("Trying to log in ") + player->getName() + " using " + proxy->getName() + " as proxy.");



            sock->print("%s", echo_off);
            //sock->print("%c%c%c", 255, 251, 1);
            std::string passwordPrompt = std::string("Please enter password for ") + proxy->getName() + ": ";
            sock->askFor(passwordPrompt.c_str());
            sock->tempbstr = proxy->getPassword();


            player->setProxy(proxy);

            if(!online)
                free_crt(proxy, false);
            sock->setState(LOGIN_GET_PROXY_PASSWORD);

            return;
        }
        lowercize(str, 1);
        if(str.length() >= 25)
            str[25]=0;

        if(!nameIsAllowed(str, sock)) {
            sock->askFor("Please enter name: ");
            return;
        }

        if(!loadPlayer(str, &player)) {
            strcpy(sock->tempstr[0], str.c_str());
            sock->print("\n%s? Did I get that right? ", str.c_str());
            sock->setState(LOGIN_CHECK_CREATE_NEW);
            return;
        } else {
            player->fd = -1;
            //gServer->addPlayer(player);
            sock->setPlayer(player);

            sock->print("%s", echo_off);
            //sock->print("%c%c%c", 255, 251, 1);
            sock->askFor("Please enter password: ");//, 255, 251, 1);
            sock->setState(LOGIN_GET_PASSWORD);
            return;
        }
        // End LOGIN_GET_NAME
    case LOGIN_CHECK_CREATE_NEW:
        if(str[0] != 'y' && str[0] != 'Y') {
            sock->tempstr[0][0] = 0;
            sock->askFor("Please enter name: ");
            sock->setState(LOGIN_GET_NAME);
            return;
        } else {


            sock->print("\nTo get help at any time during creation use the \"^Whelp^x\" command. \n");

            sock->print("\nHit return: ");
            sock->setState(CREATE_NEW);
            return;
        }
        // End LOGIN_CHECK_CREATE_NEW
    case LOGIN_GET_PASSWORD:

        if(!sock->getPlayer()->isPassword(str)) {
            sock->write("\255\252\1\n\rIncorrect.\n\r");
            logn("log.incorrect", fmt::format("Invalid password({}) for {} from {}\n", str, sock->getPlayer()->getName(), sock->getHostname()).c_str());
            sock->disconnect();
            return;
        } else {
            sock->finishLogin();
            return;
        }
        break;

    case LOGIN_GET_PROXY_PASSWORD:
        if(Player::hashPassword(str) != sock->tempbstr) {
            sock->write("\255\252\1\n\rIncorrect.\n\r");
            logn("log.incorrect", fmt::format("Invalid password({}) for {} from {}\n", str, sock->getPlayer()->getName(), sock->getHostname()).c_str());
            sock->disconnect();
            return;
        } else {
            sock->tempbstr.clear();
            sock->finishLogin();
            return;
        }
        break;
    }
}

void Socket::finishLogin() {
    char    charName[25];
    Player* player = nullptr;

    print("%s", echo_on);
    strcpy(charName, getPlayer()->getCName());

    gServer->checkDuplicateName(*this, true);
    if(gServer->checkDouble(*this)) {
//      gServer->cleanUp();
        return;
    }
//  gServer->cleanUp();

    player = getPlayer();
    std::string proxyName = player->getProxyName();
    std::string proxyId = player->getProxyId();
    free_crt(player, false);
    setPlayer(nullptr);

    if(!loadPlayer(charName, &player)) {
        askFor("Player no longer exists!\n\nPlease enter name: ");
        setState(LOGIN_GET_NAME);
        return;
    }
    player->setProxy(proxyName, proxyId);
    if(player->flagIsSet(P_HARDCORE)) {
        const StartLoc *location = gConfig->getStartLoc("highport");
        player->bind(location);
        // put them in the docks: hp.100
        player->currentLocation.room.area = "hp";
        player->currentLocation.room.id = 100;
        // remove all their stuff
        player->coins.zero();
        ObjectSet::iterator it;
        Object *obj;
        for( it = player->objects.begin() ; it != player->objects.end() ; ) {
            obj = (*it++);
            delete obj;
        }
        player->objects.clear();

    }

    setPlayer(player);
    player->fd = getFd();
    player->setSock(this);
    player->init();

    gServer->addPlayer(player);
    setState(CON_PLAYING);

    if(player->flagIsSet(P_HARDCORE)) {
        player->clearFlag(P_HARDCORE);
        player->print("\n\n\n");
        player->print("You wake up on the shores of Derlith, completely naked...\n");
        player->print("\n\n");
        player->print("You do not know how long you have been unconscious, or how long you have\n");
        player->print("been floating at sea. The sounds of the ocean fill the air, interrupted by\n");
        player->print("the bustling noise of a city street. An occasional bell rings and the\n");
        player->print("sounds of horse-drawn carriages stumbling over cobblestone soon grow louder\n");
        player->print("and louder. You look around to find yourself at the docks of Highport.\n");
        player->print("People stare at you as they pass.\n");
        player->print("\n\n");
        player->print("You no longer feel hunted, as you did in Oceancrest. You feel the peaceful\n");
        player->print("onset of immortality that comes with living in Derlith. You are now free to\n");
        player->print("continue your adventures in this world...\n");
        player->print("\n\n\n");
    }

}

// Blah I know its a big hack...fix it later if you don't like it

int setPlyClass(Socket* sock, int cClass) {
    // Make sure they both start off at 0
    sock->getPlayer()->setClass(CreatureClass::NONE);
    sock->getPlayer()->setSecondClass(CreatureClass::NONE);
    // Define's BASE as first # to interpret as multi class
    // Increment this when a class is added

    switch(cClass) {
    case static_cast<int>(CreatureClass::ASSASSIN):
    case static_cast<int>(CreatureClass::BARD):
    case static_cast<int>(CreatureClass::BERSERKER):
    case static_cast<int>(CreatureClass::CLERIC):
    case static_cast<int>(CreatureClass::DEATHKNIGHT):
    case static_cast<int>(CreatureClass::DRUID):
    case static_cast<int>(CreatureClass::FIGHTER):
    case static_cast<int>(CreatureClass::LICH):
    case static_cast<int>(CreatureClass::MAGE):
    case static_cast<int>(CreatureClass::MONK):
    case static_cast<int>(CreatureClass::PALADIN):
    case static_cast<int>(CreatureClass::RANGER):
    case static_cast<int>(CreatureClass::ROGUE):
    case static_cast<int>(CreatureClass::THIEF):
    case static_cast<int>(CreatureClass::PUREBLOOD):
    case static_cast<int>(CreatureClass::WEREWOLF):
        sock->getPlayer()->setClass(static_cast<CreatureClass>(cClass));
        break;
    case MULTI_BASE + 0:
        sock->getPlayer()->setClass(CreatureClass::FIGHTER);
        sock->getPlayer()->setSecondClass(CreatureClass::MAGE);
        break;
    case MULTI_BASE + 1:
        sock->getPlayer()->setClass(CreatureClass::FIGHTER);
        sock->getPlayer()->setSecondClass(CreatureClass::THIEF);
        break;
    case MULTI_BASE + 2:
        sock->getPlayer()->setClass(CreatureClass::CLERIC);
        sock->getPlayer()->setSecondClass(CreatureClass::ASSASSIN);
        break;
    case MULTI_BASE + 3:
        sock->getPlayer()->setClass(CreatureClass::MAGE);
        sock->getPlayer()->setSecondClass(CreatureClass::THIEF);
        break;
    case MULTI_BASE + 4:
        sock->getPlayer()->setClass(CreatureClass::THIEF);
        sock->getPlayer()->setSecondClass(CreatureClass::MAGE);
        break;
    case MULTI_BASE + 5:
        sock->getPlayer()->setClass(CreatureClass::CLERIC);
        sock->getPlayer()->setSecondClass(CreatureClass::FIGHTER);
        break;
    case MULTI_BASE + 6:
        sock->getPlayer()->setClass(CreatureClass::MAGE);
        sock->getPlayer()->setSecondClass(CreatureClass::ASSASSIN);
        break;
    default:
        break;
    }
    return(0);
}


//*********************************************************************
//                      setPlyDeity
//*********************************************************************

void setPlyDeity(Socket* sock, int deity) {

    switch(deity) {
        case ARAMON:
        case CERIS:
        case ENOCH:
        case GRADIUS:
        case ARES:
        case KAMIRA:
        case LINOTHAN:
        case ARACHNUS:
            break;
        default:
            deity = 0;
            break;
    }

    sock->getPlayer()->setDeity(deity);
}


//*********************************************************************
//                      createPlayer
//*********************************************************************
// This function allows a new player to create their character.

void doCreateHelp(Socket* sock, std::string_view str) {
    cmd cmnd;
    parse(str, &cmnd);

    std::string helpfile;
    if(cmnd.num < 2) {
        helpfile = std::string(Path::CreateHelp) + "/helpfile.txt";
        sock->viewFile(helpfile);
        return;
    }

    if(strchr(cmnd.str[1], '/')!=nullptr) {
        sock->print("You may not use backslashes.\n");
        return;
    }
    helpfile = std::string(Path::CreateHelp) + "/" + cmnd.str[1] + ".txt";
    sock->viewFile(helpfile);

}

void createPlayer(Socket* sock, const std::string& str) {

    switch(sock->getState()) {
    case CREATE_NEW:
    case CREATE_GET_DM_PASSWORD:
        break;
    default:
        if(str.starts_with("help")) {
            doCreateHelp(sock, str);
            return;
        }
        break;
    }
    switch(sock->getState()) {
    case CREATE_NEW:
        {
            Player* target = sock->getPlayer();
            sock->print("\n");
            if(target) {
                gServer->clearPlayer(target->getName());
                delete target;
            }
            target = new Player;
            if(!target)
                merror("createPlayer", FATAL);

            target->fd = -1;

            target->setSock(sock);
            sock->setPlayer(target);

            target->defineColors();
        }
        if(isdm(sock->tempstr[0])) {
            sock->print("\nYou must enter a password to create that character.\n");
            sock->print("Please enter password: ");
            gServer->processOutput();
            sock->setState(CREATE_GET_DM_PASSWORD);
            return;
        } else
            goto no_pass;
        // End CREATE_NEW
    case CREATE_GET_DM_PASSWORD:
        if(str != gConfig->getDmPass()) {
            sock->disconnect();
            return;
        }

    case CREATE_CHECK_LOCKED_OUT:
no_pass:
        if(gConfig->isLockedOut(sock) == 1) {
            sock->disconnect();
            return;
        }

        Create::getRace(sock, str, Create::doPrint);
        return;

    case CREATE_GET_RACE:

        if(!Create::getRace(sock, str, Create::doWork))
            return;
        if( gConfig->getRace(sock->getPlayer()->getRace())->isParent() &&
            Create::getSubRace(sock, str, Create::doPrint)
        )
            return;
        Create::getSex(sock, str, Create::doPrint);
        return;

    case CREATE_GET_SUBRACE:

        if(!Create::getSubRace(sock, str, Create::doWork))
            return;
        Create::getClass(sock, str, Create::doPrint);
        return;

    case CREATE_GET_SEX:

        if(!Create::getSex(sock, str, Create::doWork))
            return;
        Create::getClass(sock, str, Create::doPrint);
        return;

    case CREATE_GET_CLASS:

        if(!Create::getClass(sock, str, Create::doWork))
            return;

        if(gConfig->classes[get_class_string(static_cast<int>(sock->getPlayer()->getClass()))]->needsDeity())
            Create::getDeity(sock, str, Create::doPrint);
        else if(Create::getLocation(sock, str, Create::doPrint))
            Create::getStatsChoice(sock, str, Create::doPrint);
        return;

    case CREATE_GET_DEITY:

        if(!Create::getDeity(sock, str, Create::doWork))
            return;
        if(Create::getLocation(sock, str, Create::doPrint))
            Create::getStatsChoice(sock, str, Create::doPrint);
        return;

    case CREATE_START_LOC:

        if(!Create::getLocation(sock, str, Create::doWork))
            return;
        Create::getStatsChoice(sock, str, Create::doPrint);
        return;
    case CREATE_GET_STATS_CHOICE:
        if(!Create::getStatsChoice(sock, str, Create::doWork))
            return;
        Create::finishStats(sock);
        Create::startCustom(sock, str, Create::doPrint);
        return;

    case CREATE_GET_STATS:

        if(!Create::getStats(sock, str, Create::doWork))
            return;

        if(gConfig->getRace(sock->getPlayer()->getRace())->bonusStat())
            Create::getBonusStat(sock, str, Create::doPrint);
        else {
            Create::finishStats(sock);
            Create::startCustom(sock, str, Create::doPrint);
        }
        return;

    case CREATE_BONUS_STAT:

        if(!Create::getBonusStat(sock, str, Create::doWork))
            return;
        Create::getPenaltyStat(sock, str, Create::doPrint);
        return;

    case CREATE_PENALTY_STAT:

        if(!Create::getPenaltyStat(sock, str, Create::doWork))
            return;
        Create::startCustom(sock, str, Create::doPrint);
        return;

    case CREATE_START_CUSTOM:

        if(Create::startCustom(sock, str, Create::doWork))
            Create::getCommunity(sock, str, Create::doPrint);
        else
            Create::getProf(sock, str, Create::doPrint);
        return;

    case CREATE_GET_PROF:

        if(!Create::getProf(sock, str, Create::doWork))
            return;
        if(gConfig->classes[get_class_string(static_cast<int>(sock->getPlayer()->getClass()))]->numProfs()==2)
            Create::getSecondProf(sock, str, Create::doPrint);
        else
            Create::getPassword(sock, str, Create::doPrint);
        return;

    case CREATE_SECOND_PROF:

        if(!Create::getSecondProf(sock, str, Create::doWork))
            return;
        Create::getPassword(sock, str, Create::doPrint);
        return;

    case CREATE_GET_PASSWORD:

        if(!Create::getPassword(sock, str, Create::doWork))
            return;
        Create::done(sock, str, Create::doPrint);
        return;

    case CREATE_DONE:

        Create::done(sock, str, Create::doWork);
        return;




    // handle character customization here:
    case CUSTOM_COMMUNITY:

        if(!Create::getCommunity(sock, str, Create::doWork))
            return;
        Create::getFamily(sock, str, Create::doPrint);
        return;

    case CUSTOM_FAMILY:

        if(!Create::getFamily(sock, str, Create::doWork))
            return;
        Create::getSocial(sock, str, Create::doPrint);
        return;

    case CUSTOM_SOCIAL:

        if(!Create::getSocial(sock, str, Create::doWork))
            return;
        Create::getEducation(sock, str, Create::doPrint);
        return;

    case CUSTOM_EDUCATION:

        if(!Create::getEducation(sock, str, Create::doWork))
            return;
        Create::getHeight(sock, str, Create::doPrint);
        return;

    case CUSTOM_HEIGHT:

        if(!Create::getHeight(sock, str, Create::doWork))
            return;
        Create::getWeight(sock, str, Create::doPrint);
        return;

    case CUSTOM_WEIGHT:

        if(!Create::getWeight(sock, str, Create::doWork))
            return;
        Create::getStats(sock, str, Create::doPrint);
        return;

    }
}



//*********************************************************************
//                      addStartingItem
//*********************************************************************

void Create::addStartingItem(Player* player, const std::string &area, int id, bool wear, bool skipUseCheck, int num) {
    CatRef cr;
    cr.setArea(area);
    cr.id = id;

    for(int i=0; i<num; i++) {
        Object* object=nullptr;
        if(loadObject(cr, &object)) {
            object->setFlag(O_STARTING);
            // if they can't use it, don't even give it to them
            if(!skipUseCheck && !player->canUse(object, true)) {
                delete object;
            } else {
                object->setDroppedBy(player, "PlayerCreation");
                player->addObj(object);
                if(wear)
                    player->equip(object, false);
            }
        }
    }
}

//*********************************************************************
//                      addStartingWeapon
//*********************************************************************

void Create::addStartingWeapon(Player* player, const std::string &weapon) {
    if(weapon == "sword")
        Create::addStartingItem(player, "tut", 28, false);
    else if(weapon == "great-sword")
        Create::addStartingItem(player, "tut", 21, false);
    else if(weapon == "polearm")
        Create::addStartingItem(player, "tut", 24, false);
    else if(weapon == "whip")
        Create::addStartingItem(player, "tut", 29, false);
    else if(weapon == "rapier")
        Create::addStartingItem(player, "tut", 25, false);
    else if(weapon == "spear")
        Create::addStartingItem(player, "tut", 26, false);
    else if(weapon == "axe")
        Create::addStartingItem(player, "tut", 14, false);
    else if(weapon == "great-axe")
        Create::addStartingItem(player, "tut", 18, false);
    else if(weapon == "dagger")
        Create::addStartingItem(player, "tut", 17, false);
    else if(weapon == "staff")
        Create::addStartingItem(player, "tut", 27, false);
    else if(weapon == "mace")
        Create::addStartingItem(player, "tut", 23, false);
    else if(weapon == "great-mace")
        Create::addStartingItem(player, "tut", 20, false);
    else if(weapon == "club")
        Create::addStartingItem(player, "tut", 16, false);
    else if(weapon == "hammer")
        Create::addStartingItem(player, "tut", 22, false);
    else if(weapon == "great-hammer")
        Create::addStartingItem(player, "tut", 19, false);
    else if(weapon == "bow")
        Create::addStartingItem(player, "tut", 15, false);
    else if(weapon == "crossbow")
        Create::addStartingItem(player, "tut", 30, false);
    else if(weapon == "thrown")
        Create::addStartingItem(player, "tut", 31, false);
    else if(weapon == "knife")
        Create::addStartingItem(player, "tut", 43, false);
    else if(weapon == "arcane-weapon")
        Create::addStartingItem(player, "tut", 44, false);
    else if(weapon == "divine-weapon")
        Create::addStartingItem(player, "tut", 45, false);
    else if(weapon == "flail")
        Create::addStartingItem(player, "tut", 46, false);
    else if(weapon == "sling")
        Create::addStartingItem(player, "tut", 47, false);
    
}


//
// work functions
//


//*********************************************************************
//                      getSex
//*********************************************************************

bool Create::getSex(Socket* sock, std::string str, int mode) {
    const RaceData* rdata = gConfig->getRace(sock->getPlayer()->getRace());
    if(!rdata->isGendered()) {
        sock->getPlayer()->setSex(SEX_NONE);
        return(true);
    }

    if(mode == Create::doPrint) {

        sock->askFor("Do you wish to be [^WM^x] Male or [^WF^x] Female? ");
        sock->setState(CREATE_GET_SEX);

    } else if(mode == Create::doWork) {

        if(low(str[0]) == 'f') {
            sock->getPlayer()->setSex(SEX_FEMALE);
            sock->printColor("You are now ^Wfemale^x.\n");
        } else if(low(str[0]) == 'm') {
            sock->getPlayer()->setSex(SEX_MALE);
            sock->printColor("You are now ^Wmale^x.\n");
        } else {
            sock->askFor("[^WM^x] Male or [^WF^x] Female: ");
            sock->setState(CREATE_GET_SEX);
            return(false);
        }

    }
    return(true);
}

//*********************************************************************
//                      getRace
//*********************************************************************

#define FBUF    800

bool Create::getRace(Socket* sock, std::string str, int mode) {
    RaceDataMap choices;
    RaceDataMap::iterator it;
    int k=0;

    // figure out what they can play
    for(it = gConfig->races.begin() ; it != gConfig->races.end() ; it++) {
        // if not playable, skip
        // if sub race, skip, they'll choose when they pick parent race
        if((*it).second->isPlayable() && !(*it).second->getParentRace())
            choices[k++] = (*it).second;
    }

    if(mode == Create::doPrint) {

        char file[80];
        int ff=0;

        // show them the race menu header
        sprintf(file, "%s/race_menu.0.txt", Path::Config);
        sock->viewFile(file);

        // show them the main race menu
        sprintf(file, "%s/race_menu.1.txt", Path::Config);
        char    buf[FBUF + 1];

        ff = open(file, O_RDONLY, 0);
        k=0;
        if(ff < 0) {
            // we can't load the file? just give them a menu

            for(it = choices.begin() ; it != choices.end() ; it++) {
                sock->printColor("[^W%1c^x] %-16s\n", k + 65, (*it).second->getName().c_str());
                k++;
            }

            return(false);
        } else {
            // display them the file
            int     i=0, l=0, n=0;  //, line=0;
            //long  offset=0;
            zero(buf, sizeof(buf));

            while(true) {
                n = read(ff, buf, FBUF);
                l = 0;
                for(i=0; i<n; i++) {
                    if(buf[i] == '\n') {
                        buf[i] = 0;
                        if(i != 0 && buf[i-1] == '\r')
                            buf[i-1] = 0;
                        //line++;
                        sock->printColor("%s", &buf[l]);

                        // print out the next playable race
                        if(choices.find(k) != choices.end()) {
                            sock->printColor("[^W%1c^x] %-16s", k + 65, choices[k]->getName().c_str());
                            k++;
                        }
                        sock->print("\n");
                        //offset += (i - l + 1);
                        l = i + 1;
                    }
                }
                if(l != n) {
                    sock->printColor("%s", &buf[l]);
                    //offset += (i - l);
                }
                if(n < FBUF) {
                    close(ff);
                    break;
                }
            }
        }
        sock->print("\n\n\n");

        sock->askFor("Choose one: ");
        sock->setState(CREATE_GET_RACE);

    } else if(mode == Create::doWork) {
        k = low(str[0]) - 'a';

        if(choices.find(k) != choices.end()) {
            sock->getPlayer()->setRace(choices[k]->getId());
        } else {
            Create::getRace(sock, "", Create::doPrint);
            sock->setState(CREATE_GET_RACE);
            return(false);
        }
        if(!sock->getPlayer()->getRace())
            return(false);
        if(!gConfig->getRace(sock->getPlayer()->getRace())->isParent())
            Create::finishRace(sock);
    }
    return(true);
}

//*********************************************************************
//                      getSubRace
//*********************************************************************

bool Create::getSubRace(Socket* sock, std::string str, int mode) {
    RaceDataMap choices;
    RaceDataMap::iterator it;
    int k=0;

    // figure out what they can play
    for(it = gConfig->races.begin() ; it != gConfig->races.end() ; it++) {
        if( (*it).second->isPlayable() &&
            (   (*it).second->getId() == sock->getPlayer()->getRace() ||
                (*it).second->getParentRace() == sock->getPlayer()->getRace()
            )
        )
            choices[k++] = (*it).second;
    }

    k=0;
    if(mode == Create::doPrint) {
        std::ostringstream oStr;
        char c;
        bool    hasChildren = false;

        // set left aligned
        oStr.setf(std::ios::left, std::ios::adjustfield);
        oStr.imbue(std::locale(""));

        oStr << "\nChoose a sub race:\n";

        for(it = choices.begin() ; it != choices.end() ; it++) {
            // self doesn't count when counting children
            if((*it).second->getId() != sock->getPlayer()->getRace())
                hasChildren = true;

            if(k%2==0)
                oStr << "\n" << std::setw(20) << " ";
            c = ++k + 64;
            oStr << "[^W" << c << "^x] " << std::setw(16) << (*it).second->getName();
        }

        // if there's no children, none of the subraces are playable,
        // so they have to choose the parent
        if(!hasChildren) {
            Create::finishRace(sock);
            return(false);
        }

        sock->printColor("%s\n\n", oStr.str().c_str());

        sock->askFor("Choose one: ");
        sock->setState(CREATE_GET_SUBRACE);

    } else if(mode == Create::doWork) {

        k = low(str[0]) - 'a';

        if(choices.find(k) != choices.end()) {
            sock->getPlayer()->setRace(choices[k]->getId());
            Create::finishRace(sock);
            return(true);
        }
        return(false);
    }
    return(true);
}

//*********************************************************************
//                      finishRace
//*********************************************************************

void Create::finishRace(Socket* sock) {
    const RaceData* race = gConfig->getRace(sock->getPlayer()->getRace());

    sock->printColor("\nYour chosen race: ^W%s^x\n\n", race->getName().c_str());
    sock->getPlayer()->setSize(race->getSize());
    sock->getPlayer()->initLanguages();

    std::list<std::string>::const_iterator it;
    for(it = race->effects.begin() ; it != race->effects.end() ; it++) {
        sock->getPlayer()->addPermEffect((*it));
    }
}

//*********************************************************************
//                      getClass
//*********************************************************************

bool Create::getClass(Socket* sock, std::string str, int mode) {
    int     l=0, k=0;
    if(mode == Create::doPrint) {

        sock->print("   ____________\n");
        sock->print("  /------------\\            VVVVVVVVVVVVVVVV\n");
        sock->print(" // ___    ___ \\\\   %12s:  Choose A Class\n", gConfig->getRace(sock->getPlayer()->getRace())->getName().c_str());
        sock->print(" \\_/   \\  /   \\_/           ^^^^^^^^^^^^^^^^\n");
        sock->print(" ______/  \\__________________________________________________\n");
        sock->print("<______    ________\\\\\\\\_____\\\\\\\\_____\\\\\\\\_____\\\\\\\\_____\\\\\\\\__]\n");
        sock->print("  _    \\  /    _\n");
        sock->print(" / \\___/  \\___/ \\\n");
        sock->print(" \\\\____________//\n");
        sock->print("  `------------'"," ");

        for(l=1, k=0; l<CLASS_COUNT_MULT; l++) {
            if(gConfig->getRace(sock->getPlayer()->getRace())->allowedClass(l)) {
                if(k%2==0)
                    sock->print("\n%20s", " ");
                sock->printColor("[^W%1c^x] %-16s", ++k + 64, allowedClassesStr[l-1]);
            }
        }

        sock->print("\n                                                         |\\\n");
        sock->print("                                                         <<\\         _\n");
        sock->print("                                                          / \\       //\n");
        sock->print(" .--------------------------------------------------------{o}______/|\n");
        sock->print("<       -===========================================:::{*}///////////]\n");
        sock->print(" `--------------------------------------------------------{o}~~~~~~\\|\n");
        sock->print("                                                          \\ /       \\\\\n");
        sock->print("                                                         <</         -\n");
        sock->print("                                                         |/\n");
        sock->askFor("Choose one: ");
        sock->setState(CREATE_GET_CLASS);

    } else if(mode == Create::doWork) {

        int     i=0;
        if(!isalpha(str[0]))
            i = -1;
        else
            i = up(str[0]) - 64;

        for(l=1, k=0; l<CLASS_COUNT_MULT; l++) {
            if(gConfig->getRace(sock->getPlayer()->getRace())->allowedClass(l)) {
                k++;
                if(k == i)
                    setPlyClass(sock, l);
            }
        }
        if(sock->getPlayer()->getClass() != CreatureClass::NONE) {
            if(sock->getPlayer()->hasSecondClass())
                sock->printColor("Your chosen class: ^W%s/%s\n", get_class_string(static_cast<int>(sock->getPlayer()->getClass())),
                                 get_class_string(static_cast<int>(sock->getPlayer()->getSecondClass())));
            else
                sock->printColor("Your chosen class: ^W%s\n", get_class_string(static_cast<int>(sock->getPlayer()->getClass())));
        }
        if(sock->getPlayer()->getClass() == CreatureClass::NONE) {
            sock->printColor("Invalid selection: ^W%s\n", str.c_str());
            Create::getClass(sock, "", Create::doPrint);

            sock->setState(CREATE_GET_CLASS);
            return(false);
        }

    }
    return(true);
}

//*********************************************************************
//                      getDeity
//*********************************************************************

bool Create::getDeity(Socket* sock, std::string str, int mode) {
    int     l=0, k=0;
    const RaceData* race = gConfig->getRace(sock->getPlayer()->getRace());
    if(mode == Create::doPrint) {

        sock->print("Please choose a deity:\n");

        for(l=1, k=0 ; l<DEITY_COUNT ; l++) {
            if(race->allowedDeity(sock->getPlayer()->getClass(), sock->getPlayer()->getSecondClass(), l)) {
                if(k%2==0)
                    sock->print("\n%20s", " ");
                sock->printColor("[^W%1c^x] %-16s", ++k + 64, gConfig->getDeity(l)->getName().c_str());
            }
        }
        sock->askFor("\n\nChoose one: ");

        sock->setState(CREATE_GET_DEITY);

    } else if(mode == Create::doWork) {

        int     i=0;
        if(!isalpha(str[0]))
            i = -1;
        else
            i = up(str[0]) - 64;

        for(l=1, k=0; l < static_cast<int>(CreatureClass::CLASS_COUNT)+4; l++) {

            if(race->allowedDeity(sock->getPlayer()->getClass(),
                                  sock->getPlayer()->getSecondClass(), l)) {
                k++;
                if(k == i)
                    setPlyDeity(sock, l);
            }
        }

        if(sock->getPlayer()->getDeity())
            sock->printColor("Your chosen deity: ^W%s\n", gConfig->getDeity(sock->getPlayer()->getDeity())->getName().c_str());
        else {
            sock->printColor("Invalid selection: ^W%s\n", str.c_str());
            Create::getDeity(sock, "", Create::doPrint);

            sock->setState(CREATE_GET_DEITY);
            return(false);
        }

    }
    return(true);
}

//*********************************************************************
//                      getLocation
//*********************************************************************

// from startlocs.cpp
bool startingChoices(Player* player, std::string str, char* location, bool choose);

bool Create::getLocation(Socket* sock, const std::string &str, int mode) {
    char location[256];
    if(mode == Create::doPrint) {

        if(!startingChoices(sock->getPlayer(), str, location, false)) {
            sock->print("\n\nPlease choose a starting location:");
            sock->printColor("\n   %s\n\n", location);

            // OCENCREAST: Dom: HC
            //sock->printColor("^YCaution:^x If you choose Oceancrest as a starting location, you will\n");
            //sock->printColor("automatically be entered into the tournament. See the forum for details.\n");
            //sock->printColor("^Yhttp://forums.rohonline.net/topic/1655/\n\n");
            sock->askFor(": ");

            sock->setState(CREATE_START_LOC);
            return(false);
        }

    } else if(mode == Create::doWork) {

        if(!startingChoices(sock->getPlayer(), str, location, true)) {
            sock->print("Invalid selection.\n");
            sock->askFor(": ");

            sock->setState(CREATE_START_LOC);
            return(false);
        }

    }

    sock->print("\n\nYour starting location is: ");
    sock->print("\n   %s\n\n", location);
    return(true);
}

//*********************************************************************
//                      startCustom
//*********************************************************************

bool Create::startCustom(Socket* sock, std::string str, int mode) {
    // still in progress! bypass for now
    return(getProf(sock, str, mode));

    if(mode == Create::doPrint) {

        sock->printColor("\nFor more customization options, press [^WM^x].\n");
        sock->print("To continue with your character, press enter.\n");

        sock->askFor(": ");

        sock->setState(CREATE_START_CUSTOM);

    } else if(mode == Create::doWork) {

        if(str[0] != 'm' && str[0] != 'M') {
            // randomly customize them
            return(false);
        }

    }
    return(true);
}
//*********************************************************************
//                      getStatsChoice
//*********************************************************************

bool Create::getStatsChoice(Socket* sock, std::string str, int mode) {
    if(mode == Create::doPrint) {
        PlayerClass *pClass = gConfig->classes[sock->getPlayer()->getClassString()];
        if(!pClass || !pClass->hasDefaultStats()) {
            Create::getStats(sock, str, Create::doPrint);
            return(false);
        }

        sock->print("\nFor character stats, you may:\n");

        sock->printColor("\n[^WC^x]hoose your own stats");
        sock->printColor("\n[^WU^x]se predefined stats provided by the mud");

        sock->print("\n\nNote: For beginners that are unfamiliar with game mechanics, it is highly recommended to use predefined stats to reduce the learning curve.\n");

        sock->askFor(": ");

        sock->setState(CREATE_GET_STATS_CHOICE);

    } else if(mode == Create::doWork) {
        boost::trim(str);
        if (str.length() > 0) {
            if(tolower(str.at(0)) == 'c') {
                sock->print("You have chosen to select your own stats.\n");
                Create::getStats(sock, "", Create::doPrint);
                // We've set the next state so don't change it after we return
                return(false);
            } else if(tolower(str.at(0)) == 'u') {
                PlayerClass *pClass = gConfig->classes[sock->getPlayer()->getClassString()];
                if(!pClass) {
                    Create::getStats(sock, str, Create::doPrint);
                    return(false);
                }
                pClass->setDefaultStats(sock->getPlayer());

                // Continue on with character creation
                return(true);
            }
        }

        sock->askFor(": ");
        sock->setState(CREATE_GET_STATS_CHOICE);
    }
    return(false);
}


//*********************************************************************
//                      getStats
//*********************************************************************

bool Create::getStats(Socket* sock, std::string str, int mode) {
    if(mode == Create::doPrint) {

        sock->print("\nYou have 56 points to distribute among your 5 stats. Please enter your 5");
        sock->print("\nnumbers in the following order: Strength, Dexterity, Constitution,");
        sock->print("\nIntelligence, Piety.  No stat may be smaller than 3 or larger than 18.");
        sock->print("\nUse the following format: ## ## ## ## ##\n\n");

        sock->askFor(": ");

        sock->setState(CREATE_GET_STATS);

    } else if(mode == Create::doWork) {

        int     i=0, l=0, k=0, sum=0, num[5], n = str.length();

        for(i=0; i<=n; i++) {
            if(str[i]==' ' || str[i]==0) {
                str[i] = 0;
                num[k++] = atoi(&str[l]);
                l = i+1;
            }
            if(k>4)
                break;
        }
        if(k<5) {
            sock->print("Please enter all 5 numbers.\n");
            sock->askFor(": ");

            sock->setState(CREATE_GET_STATS);
            return(false);
        }

        for(i=0; i<5; i++) {
            if(num[i] < 3 || num[i] > 18) {
                sock->print("No stats < 3 or > 18 please.\n");
                sock->print(": ");
                sock->setState(CREATE_GET_STATS);
                return(false);
            }
            sum += num[i];
        }

        if(sum != 56) {
            sock->print("Stat total must equal 56 points, yours totaled %d.\n", sum);
            sock->print(": ");
            sock->setState(CREATE_GET_STATS);
            return(false);
        }

        sock->getPlayer()->strength.setInitial(num[0] * 10);
        sock->getPlayer()->dexterity.setInitial(num[1] * 10);
        sock->getPlayer()->constitution.setInitial(num[2] * 10);
        sock->getPlayer()->intelligence.setInitial(num[3] * 10);
        sock->getPlayer()->piety.setInitial(num[4] * 10);

    }
    return(true);
}

//*********************************************************************
//                      finishStats
//*********************************************************************

void Create::finishStats(Socket* sock) {
    Player* ply = sock->getPlayer();
    ply->strength.addInitial( gConfig->getRace(ply->getRace())->getStatAdj(STR));
    ply->dexterity.addInitial( gConfig->getRace(ply->getRace())->getStatAdj(DEX));
    ply->constitution.addInitial(  gConfig->getRace(ply->getRace())->getStatAdj(CON));
    ply->intelligence.addInitial(  gConfig->getRace(ply->getRace())->getStatAdj(INT));
    ply->piety.addInitial( gConfig->getRace(ply->getRace())->getStatAdj(PTY));

    ply->strength.setInitial(MAX<int>(10, ply->strength.getInitial()));
    ply->dexterity.setInitial(MAX<int>(10, ply->dexterity.getInitial()));
    ply->constitution.setInitial(MAX<int>(10, ply->constitution.getInitial()));
    ply->intelligence.setInitial(MAX<int>(10, ply->intelligence.getInitial()));
    ply->piety.setInitial(MAX<int>(10, ply->piety.getInitial()));

    sock->print("Your stats: %d %d %d %d %d\n",
        ply->strength.getMax(),
        ply->dexterity.getMax(),
        ply->constitution.getMax(),
        ply->intelligence.getMax(),
        ply->piety.getMax());
}

//*********************************************************************
//                      getBonusStat
//*********************************************************************

bool Create::getBonusStat(Socket* sock, std::string str, int mode) {
    if(mode == Create::doPrint) {

        sock->print("\nRaise which stat?\n[A] Strength, [B] Dexterity, [C] Constitution, [D] Intelligence, or [E] Piety.\n : ");
        sock->setState(CREATE_BONUS_STAT);

    } else if(mode == Create::doWork) {

        switch(low(str[0])) {
        case 'a':
            sock->getPlayer()->strength.addInitial(10);
            break;
        case 'b':
            sock->getPlayer()->dexterity.addInitial(10);
            break;
        case 'c':
            sock->getPlayer()->constitution.addInitial(10);
            break;
        case 'd':
            sock->getPlayer()->intelligence.addInitial(10);
            break;
        case 'e':
            sock->getPlayer()->piety.addInitial(10);
            break;
        default:
            sock->print("\nChoose one: ");
            sock->setState(CREATE_BONUS_STAT);
            return(false);
        }

    }
    return(true);
}

//*********************************************************************
//                      getPenaltyStat
//*********************************************************************

bool Create::getPenaltyStat(Socket* sock, std::string str, int mode) {
    if(mode == Create::doPrint) {

        sock->printColor("\nChoose your stat to lower:\n[^WA^x] Strength, [^WB^x] Dexterity, [^WC^x] Constitution, [^WD^x] Intelligence, or [^WE^x] Piety.\n : ");
        sock->setState(CREATE_PENALTY_STAT);

    } else if(mode == Create::doWork) {

        switch(low(str[0])) {
        case 'a':
            sock->getPlayer()->strength.addInitial(-10);
            break;
        case 'b':
            sock->getPlayer()->dexterity.addInitial(-10);
            break;
        case 'c':
            sock->getPlayer()->constitution.addInitial(-10);
            break;
        case 'd':
            sock->getPlayer()->intelligence.addInitial(-10);
            break;
        case 'e':
            sock->getPlayer()->piety.addInitial(-10);
            break;
        default:
            sock->print("\nChoose one: ");
            sock->setState(CREATE_PENALTY_STAT);
            return(false);
        }
    }
    return(true);
}

//*********************************************************************
//                      handleWeapon
//*********************************************************************

bool Create::handleWeapon(Socket* sock, int mode, char ch) {
    int     i=0;

    if(!isalpha(ch))
        i = -1;
    else
        i = up(ch) - 64;

    if(mode == Create::doPrint) {
        if(sock->getPlayer()->getClass() == CreatureClass::WEREWOLF) {
            sock->printColor("\n\n^WSlashing Weapons^x\n     [^WA^x] Claws\n");
            return(true);
        } else if(sock->getPlayer()->getClass() == CreatureClass::MONK) {
            sock->printColor("\n\n^WCrushing Weapons^x\n     [^WA^x] Bare-Handed\n");
            return(true);
        }
    } else {
        if(sock->getPlayer()->getClass() == CreatureClass::WEREWOLF && low(ch) == 'a') {
            sock->getPlayer()->setSkill("claw", 1);
            return(true);
        } else if(sock->getPlayer()->getClass() == CreatureClass::MONK && low(ch) == 'a') {
            sock->getPlayer()->setSkill("bare-hand", 1);
            return(true);
        } else if(sock->getPlayer()->getClass() == CreatureClass::MONK || sock->getPlayer()->getClass() == CreatureClass::WEREWOLF) {
            sock->print("Choose a weapon skill:\n");
            sock->setState(CREATE_GET_PROF);
            return(false);
        }
    }
    if(!sock->getPlayer()->knowsSkill("bare-hand"))
        sock->getPlayer()->addSkill("bare-hand", 1);





    int k = 0, n = 0;
    for(const auto& [curGroup, curGroupDisplay] : gConfig->skillGroups) {
        if(!curGroup.starts_with("weapons") || curGroup.length() <= 7)
            continue;

        if(sock->getPlayer()->getClass() == CreatureClass::CLERIC && sock->getPlayer()->getDeity() == CERIS) {
            if(boost::contains(curGroup, "slashing") || boost::contains(curGroup, "piercing"))
                continue;
        }

        if(mode == Create::doPrint) {
            sock->bprint(fmt::format("^W\n\n{}", curGroupDisplay));
            n = 0;
        }
        for(const auto& [skillName, curSkill] : gConfig->skills) {
            if(curSkill.getGroup() != curGroup)
                continue;

            if(curSkill.getName() == "claw")
                continue;

            if(sock->getPlayer()->getClass() == CreatureClass::CLERIC && sock->getPlayer()->getDeity() == CERIS)
                if(curSkill.getName() == "whip")
                    continue;

            if(sock->getPlayer()->knowsSkill(curSkill.getName()))
                continue;

            if(sock->getPlayer()->getLevel() < 4 && ((curSkill.getName() == "arcane-weapon" || curSkill.getName() == "divine-weapon")))
                continue;

            if ((!(sock->getPlayer()->getClass() == CreatureClass::MAGE ||
                sock->getPlayer()->getClass() == CreatureClass::LICH ||
                sock->getPlayer()->getClass() == CreatureClass::BARD ||
                sock->getPlayer()->getSecondClass() == CreatureClass::MAGE))
                && curSkill.getName() == "arcane-weapon")
                    continue;

            if ((!(sock->getPlayer()->getClass() == CreatureClass::CLERIC ||
                sock->getPlayer()->getClass() == CreatureClass::PALADIN ||
                sock->getPlayer()->getClass() == CreatureClass::DRUID ||
                sock->getPlayer()->getClass() == CreatureClass::DEATHKNIGHT))
                && curSkill.getName() == "divine-weapon")
                    continue;

            if(mode == Create::doPrint) {
                if(n++%2==0)
                    sock->print("\n%5s", " ");

                sock->bprint(fmt::format("[^W{:1}^x] {:<30}", (char)(++k + 64), curSkill.getDisplayName()));
            } else {
                if(i == ++k) {
                    sock->getPlayer()->addSkill(curSkill.getName(),1);
                    Create::addStartingWeapon(sock->getPlayer(), curSkill.getName());
                    sock->bprint(fmt::format("You have learned how to use ^W{}^x.\n", curSkill.getDisplayName()));
                    return(true);
                }
            }
        }
    }
    if(mode == Create::doPrint) {
        return k != 0;
    }
    return(false);
}

//*********************************************************************
//                      cmdWeapons
//*********************************************************************

int cmdWeapons(Player* player, cmd* cmnd) {
    if(player->getWeaponTrains() < 1) {
        *player << "You don't have any weapon trains left!\n";
        return(0);
    }

//  if(!player->getParent()->findWeaponsTrainer()) {
//      player->print("You can't find anyone here to train you in weapon skills!\n");
//      return(0);
//  }

    *player << ColorOn <<fmt::format("You have ^W{}^x weapon skills to choose.\n", player->getWeaponTrains()) << ColorOff;

    if(!Create::handleWeapon(player->getSock(), Create::doPrint, '\0')) {
        *player << "\n\nSorry, couldn't find any weapon skills you don't know that you can learn.\n";
        return(0);
    }
    player->getSock()->setState(CON_CHOSING_WEAPONS);
    player->getSock()->askFor("\n(0 to exit): ");
    return(0);
}

//*********************************************************************
//                      convertNewWeaponSkills
//*********************************************************************

void convertNewWeaponSkills(Socket* sock, const std::string& str) {
    if(!isalpha(str[0])) {
        sock->setState(CON_PLAYING);
        return;
    }

    if(Create::handleWeapon(sock, Create::doWork, str[0])) {
        sock->getPlayer()->subWeaponTrains(1);
        if(sock->getPlayer()->getWeaponTrains() == 0) {
            sock->print("Exiting weapon skill selector.\n");
            sock->setState(CON_PLAYING);
            return;
        } else {
            sock->print("You have %d weapon skill(s) left to choose.", sock->getPlayer()->getWeaponTrains());
            if(!Create::handleWeapon(sock, Create::doPrint, str[0])) {
                sock->print("\n\nSorry, couldn't find any weapon skills you don't know that you can learn.\n");
                sock->setState(CON_PLAYING);
                return;
            }
            sock->askFor("\n(0 to exit): ");
        }
    } else {
        sock->print("Please pick: ");
    }
}

//*********************************************************************
//                      getProf
//*********************************************************************

bool Create::getProf(Socket* sock, std::string str, int mode) {

    if(mode == Create::doPrint) {
        if(gConfig->classes[get_class_string(sock->getPlayer()->getClassInt())]->numProfs() > 1) {
            sock->print("\nPick %d weapon skills:", gConfig->classes[get_class_string(sock->getPlayer()->getClassInt())]->numProfs());
        } else {
            sock->print("\nChoose a weapon skill:");
        }

        Create::handleWeapon(sock, mode, str[0]);
        sock->askFor("\n: ");
        sock->setState(CREATE_GET_PROF);

    } else if(mode == Create::doWork) {

        if(Create::handleWeapon(sock, mode, str[0]))
            return(true);
        else {
            sock->print("Choose a weapon skill:\n");
            sock->setState(CREATE_GET_PROF);
            return(false);
        }
    }

    return(true);
}

//*********************************************************************
//                      getSecondProf
//*********************************************************************

bool Create::getSecondProf(Socket* sock, std::string str, int mode) {
    if(mode == Create::doPrint) {

        Create::handleWeapon(sock, mode, str[0]);

        sock->print("\nPick a second proficiency:\n");
        sock->askFor(": ");
        sock->setState(CREATE_SECOND_PROF);

    } else if(mode == Create::doWork) {

        if(Create::handleWeapon(sock, mode, str[0]))
            return(true);
        else {
            sock->print("Pick a second proficiency!\n");
            sock->askFor(": ");
            sock->setState(CREATE_SECOND_PROF);
            return(false);
        }

    }
    return(true);
}

//*********************************************************************
//                      getPassword
//*********************************************************************

bool Create::getPassword(Socket* sock, const std::string &str, int mode) {
    if(mode == Create::doPrint) {

        sock->print("\nYou must now choose a password. Remember that it\n");
        sock->print("is YOUR responsibility to remember this password. The staff\n");
        sock->print("at Realms will not give out password information to anyone\n");
        sock->print("at any time. Please write it down someplace, because if you\n");
        sock->print("forget it, you will no longer be able to play this character.\n\n");
        sock->print("Please choose a password (up to 14 chars): ");

        sock->setState(CREATE_GET_PASSWORD);

    } else if(mode == Create::doWork) {

        if(!isValidPassword(sock, str)) {
            sock->print("\nChoose a password: ");
            sock->setState(CREATE_GET_PASSWORD);
            return(false);
        }

        sock->getPlayer()->setFlag(P_PASSWORD_CURRENT);

        //t = time(0);
        //strcpy(sock->getPlayer()->last_mod, ctime(&t));

        sock->getPlayer()->setPassword(str);

    }
    return(true);
}

//*********************************************************************
//                      done
//*********************************************************************

void Create::done(Socket* sock, const std::string &str, int mode) {

    if(mode == Create::doPrint) {

        char file[80];
        sprintf(file, "%s/policy_login.txt", Path::Config);
        sock->viewFile(file);

        sock->print("[Press Enter to Continue]");
        sock->setState(CREATE_DONE);

    } else if(mode == Create::doWork) {
        Player* player = sock->getPlayer();
        long t = time(nullptr);
        int i=0;

        player->setBirthday();
        player->setCreated();

        player->setName( sock->tempstr[0]);

        if(gServer->checkDuplicateName(*sock, false))
            return;
        if(gServer->checkDouble(*sock))
            return;
        if(Player::exists(player->getName())) {
            sock->printColor("\n\n^ySorry, that player already exists.^x\n\n\n");
            sock->reconnect();
            return;
        }

        player->lasttime[LT_AGE].interval = 0;

        for(i=0; i<MAX_LT; i++)
            player->lasttime[i].ltime = t;

        // Moved here so con affects a level 1's hp
        player->adjustStats();
        player->upLevel();

        player->addSkill("defense", 1);
        // Give out parry & block
        switch(player->getClass()) {
        // These classes get block + parry
            case CreatureClass::ASSASSIN:
            case CreatureClass::BARD:
            case CreatureClass::FIGHTER:
            case CreatureClass::RANGER:
            case CreatureClass::ROGUE:
            case CreatureClass::PALADIN:
            case CreatureClass::DEATHKNIGHT:
                player->addSkill("block", 1);
                // fall through for parry
            case CreatureClass::THIEF:
                player->addSkill("parry", 1);
                break;
            case CreatureClass::BERSERKER:
                player->addSkill("block", 1);
                // Zerkers are more brute force than finnese...no parry
                break;
            default:
                break;
        }
        // Give out armor skills here
        switch(player->getClass()) {
            case CreatureClass::BARD:
            case CreatureClass::BERSERKER:
            case CreatureClass::FIGHTER:
            case CreatureClass::PALADIN:
            case CreatureClass::DEATHKNIGHT:
            case CreatureClass::RANGER:
                player->addSkill("plate", 1);
                player->addSkill("chain", 1);
                player->addSkill("scale", 1);
                player->addSkill("ring", 1);
            case CreatureClass::ASSASSIN:
            case CreatureClass::THIEF:
            case CreatureClass::ROGUE:
            case CreatureClass::PUREBLOOD:
            case CreatureClass::DRUID:
            case CreatureClass::CLERIC:
                player->addSkill("leather", 1);
            case CreatureClass::MAGE:
            case CreatureClass::MONK:
            case CreatureClass::LICH:
            case CreatureClass::WEREWOLF:
            default:
                player->addSkill("cloth", 1);
                break;
        }

        // A few clerics get plate
        if(player->getClass() == CreatureClass::CLERIC && !player->hasSecondClass()) {
            if(player->getDeity() == ENOCH || player->getDeity() == ARES || player->getDeity() == GRADIUS) {
                player->addSkill("plate", 1);
                player->addSkill("chain", 1);
                player->addSkill("scale", 1);
                player->addSkill("ring", 1);
            }
        }

        // TODO: Dom: make this mage only
        if(player->getClass() !=  CreatureClass::BERSERKER) {
            if(player->getCastingType() == Divine) {
                player->addSkill("healing", 1);
                player->addSkill("destruction", 1);
                player->addSkill("evil", 1);
                player->addSkill("knowledge", 1);
                player->addSkill("protection", 1);
                player->addSkill("augmentation", 1);
                player->addSkill("travel", 1);
                player->addSkill("creation", 1);
                player->addSkill("trickery", 1);
                player->addSkill("good", 1);
                player->addSkill("nature", 1);
            } else {
                player->addSkill("abjuration", 1);
                player->addSkill("conjuration", 1);
                player->addSkill("divination", 1);
                player->addSkill("enchantment", 1);
                player->addSkill("evocation", 1);
                player->addSkill("illusion", 1);
                player->addSkill("necromancy", 1);
                player->addSkill("translocation", 1);
                player->addSkill("transmutation", 1);
            }

            player->addSkill("fire", 1);
            player->addSkill("water", 1);
            player->addSkill("earth", 1);
            player->addSkill("air", 1);
            player->addSkill("cold", 1);
            player->addSkill("electric", 1);
        }

        if(player->getClass() == CreatureClass::LICH)
            player->learnSpell(S_SAP_LIFE);

        Create::addStartingItem(player, "tut", 32);
        Create::addStartingItem(player, "tut", 33);
        Create::addStartingItem(player, "tut", 34);
        Create::addStartingItem(player, "tut", 35);
        Create::addStartingItem(player, "tut", 36);
        Create::addStartingItem(player, "tut", 37);
        Create::addStartingItem(player, "tut", 38);
        Create::addStartingItem(player, "tut", 39);
        Create::addStartingItem(player, "tut", 40);

        Create::addStartingItem(player, "tut", 42, false, true, 3);

        player->fd = sock->getFd();
        player->setSock(sock);
        sock->setPlayer(player);
        player->init();

        sock->print("\n");

//      player->adjustStats();

        player->setFlag(P_LAG_PROTECTION_SET);
        player->clearFlag(P_NO_AUTO_WEAR);

        if(player->getClass() == CreatureClass::BARD)
            player->learnSong(SONG_HEAL);

        player->save(true);
        gServer->addPlayer(player);

        sock->print("Type 'welcome' at prompt to get more info on the game\nand help you get started.\n");

        sock->setState(CON_PLAYING);
    }
}

//*********************************************************************
//                      adjustStats
//*********************************************************************

void Creature::adjustStats() {
    strength.restore();
    dexterity.restore();
    constitution.restore();
    intelligence.restore();
    piety.restore();

    strength.setInitial(strength.getCur());
    dexterity.setInitial(dexterity.getCur());
    constitution.setInitial(constitution.getCur());
    intelligence.setInitial(intelligence.getCur());
    piety.setInitial(piety.getCur());


    if(deity) {
        switch(deity) {
        case ARAMON:
        case ARACHNUS:
            alignment = -100;
            break;
        case ENOCH:
        case LINOTHAN:
        case KAMIRA:
            alignment = 100;
            break;
        default:
            break;
        }
    }

    if(cClass == CreatureClass::LICH)
        alignment = -100;
}

//*********************************************************************
//                      CustomCrt
//*********************************************************************

CustomCrt::CustomCrt() {
    community = parents = brothers = sisters = social = education = height = weight = 0;
    hair = eyes = skin = "";
}

//*********************************************************************
//                      getCommunity
//*********************************************************************

bool Create::getCommunity(Socket* sock, std::string str, int mode) {
    if(mode == Create::doPrint) {

        sock->print("\nYour Community\n\n");
        // these match up with the defines in CustomCrt
        sock->printColor("   [^Wa^x] You lived in the wilderness, shunning contact with most of your community.\n");
        sock->printColor("   [^Wb^x] Your home was a small hamlet far from major cities.\n");
        sock->printColor("   [^Wc^x] You lived in village that saw a fair amount of trading.\n");
        sock->printColor("   [^Wd^x] The large town you grew up in was fairly well known.\n");
        sock->printColor("   [^We^x] You grew up in a major city.\n\n");

        sock->askFor(": ");

        sock->setState(CUSTOM_COMMUNITY);

    } else if(mode == Create::doWork) {

        switch(str[0]) {
        case 'a':
        case 'b':
        case 'c':
        case 'd':
        case 'e':
            // these match up with the defines in CustomCrt, so we can just assign
            sock->getPlayer()->custom.community = str[0]-'a'+1;
            break;
        default:
            sock->printColor("Invalid selection: ^W%s\n", str.c_str());
            sock->askFor("Choose one: ");

            sock->setState(CUSTOM_COMMUNITY);
            return(false);
        }

    }
    return(true);
}

//*********************************************************************
//                      doFamily
//*********************************************************************

void Create::doFamily(Player* player, int mode) {

    if(mode == 1) {
        player->custom.parents = CustomCrt::PARENTS_UNKNOWN;
        return;
    }

    // siblings
    int i = 0;
    if(mode == 3)
        i = Random::get(1,3);
    else if(mode == 4)
        i = Random::get(4, 10);

    while(i) {
        if(Random::get(0,1))
            player->custom.sisters++;
        else
            player->custom.brothers++;
        i--;
    }

    switch(Random::get(1,6)) {
    case 1:
        player->custom.parents = CustomCrt::PARENTS_DEAD;
        break;
    case 2:
        player->custom.parents = CustomCrt::PARENTS_MOTHER_DEAD;
        break;
    case 3:
        player->custom.parents = CustomCrt::PARENTS_FATHER_DEAD;
        break;
    // 50% chance of them being alive
    default:
        player->custom.parents = CustomCrt::PARENTS_ALIVE;
        break;
    }
}

//*********************************************************************
//                      getFamily
//*********************************************************************

bool Create::getFamily(Socket* sock, std::string str, int mode) {
    if(mode == Create::doPrint) {

        sock->print("\nYour Family\n\n");
        sock->printColor("   [^Wa^x] You were orphaned as a child and have no family.\n");
        sock->printColor("   [^Wb^x] You are an only child.\n");
        sock->printColor("   [^Wc^x] You grew up in a small family with 1-3 siblings.\n");
        sock->printColor("   [^Wd^x] The grew up in a large family with 4-10 siblings.\n\n");

        sock->askFor(": ");

        sock->setState(CUSTOM_FAMILY);

    } else if(mode == Create::doWork) {

        switch(str[0]) {
        case 'a':
        case 'b':
        case 'c':
        case 'd':
            doFamily(sock->getPlayer(), str[0]-'a'+1);
            break;
        default:
            sock->printColor("Invalid selection: ^W%s\n", str.c_str());
            sock->askFor("Choose one: ");

            sock->setState(CUSTOM_FAMILY);
            return(false);
        }

    }
    return(true);
}

//*********************************************************************
//                      getSocial
//*********************************************************************

bool Create::getSocial(Socket* sock, std::string str, int mode) {
    CustomCrt *custom = &sock->getPlayer()->custom;

    if(mode == Create::doPrint) {

        sock->print("\nYour Social Status\n\n");
        if(custom->parents == CustomCrt::PARENTS_UNKNOWN)
            sock->printColor("   [^Wa^x] You were an outcast in society.\n");
        else
            sock->printColor("   [^Wa^x] You and your family were outcasts in society.\n");

        if(custom->community != CustomCrt::COMMUNITY_OUTCAST) {
            sock->printColor("   [^Wb^x] You were considered a criminal (justly so or not).\n");
            sock->printColor("   [^Wc^x] You grew up poor and on the lowest rungs of society.\n");
            sock->printColor("   [^Wd^x] You had to work hard to provide for yourself, but were able to manage.\n");

            if(custom->community != CustomCrt::COMMUNITY_HAMLET) {
                if(custom->parents == CustomCrt::PARENTS_UNKNOWN)
                    sock->printColor("   [^We^x] Your life was easier than most as you were fairly well off.\n");
                else
                    sock->printColor("   [^We^x] Your life was easier than most as you and your family were fairly well off.\n");

                if(custom->community != CustomCrt::COMMUNITY_VILLAGE && custom->parents != CustomCrt::PARENTS_UNKNOWN) {
                    sock->printColor("   [^Wf^x] You and your family were well off in society and did not need to worry about much.\n");
                    sock->printColor("   [^Wg^x] Being of noble descent, you and your family were well known in the community.\n");
                }
            }
        }
        sock->print("\n");

        sock->askFor(": ");

        sock->setState(CUSTOM_SOCIAL);

    } else if(mode == Create::doWork) {

        if( (str[0] > 'a' && custom->community == CustomCrt::COMMUNITY_OUTCAST) ||
            (str[0] > 'd' && custom->community == CustomCrt::COMMUNITY_HAMLET) ||
            (str[0] > 'e' && (custom->community == CustomCrt::COMMUNITY_VILLAGE || custom->parents == CustomCrt::PARENTS_UNKNOWN))
        )
            str[0] = 'z';

        switch(str[0]) {
        case 'a':
        case 'b':
        case 'c':
        case 'd':
        case 'e':
        case 'f':
        case 'g':
            custom->social = str[0]-'a'+1;
            break;
        default:
            sock->printColor("Invalid selection: ^W%s\n", str.c_str());
            sock->askFor("Choose one: ");

            sock->setState(CUSTOM_SOCIAL);
            return(false);
        }

    }
    return(true);
}

//*********************************************************************
//                      getEducation
//*********************************************************************

bool Create::getEducation(Socket* sock, std::string str, int mode) {
    CustomCrt *custom = &sock->getPlayer()->custom;

    if(mode == Create::doPrint) {

        sock->print("\nYour Education\n\n");
        sock->printColor("   [^Wa^x] You have no formal education.\n");

        if(custom->community != CustomCrt::COMMUNITY_OUTCAST && custom->social != CustomCrt::SOCIAL_OUTCAST) {
            sock->printColor("   [^Wb^x] You trained as an apprentice to a trader, crafstman, or farmer.\n");
            sock->printColor("   [^Wc^x] You attended a local school and received a basic education.\n");

            if(custom->community != CustomCrt::COMMUNITY_HAMLET && custom->social > CustomCrt::SOCIAL_LOWER) {
                sock->printColor("   [^Wd^x] You received a decent education or were tutored in several subject areas.\n");
                sock->printColor("   [^We^x] You attended a large university and received an official degree.\n");
            }

        }

        sock->askFor(": ");

        sock->setState(CUSTOM_EDUCATION);

    } else if(mode == Create::doWork) {

        if( (str[0] > 'a' && (custom->community == CustomCrt::COMMUNITY_OUTCAST || custom->social == CustomCrt::SOCIAL_OUTCAST)) ||
            (str[0] > 'c' && (custom->community == CustomCrt::COMMUNITY_HAMLET || custom->social <= CustomCrt::SOCIAL_LOWER))
        )
            str[0] = 'z';

        switch(str[0]) {
        case 'a':
        case 'b':
        case 'c':
        case 'd':
        case 'e':
            custom->education = str[0]-'a'+1;
            break;
        default:
            sock->printColor("Invalid selection: ^W%s\n", str.c_str());
            sock->askFor("Choose one: ");

            sock->setState(CUSTOM_EDUCATION);
            return(false);
        }

    }
    return(true);
}

//*********************************************************************
//                      calcHeight
//*********************************************************************

int Create::calcHeight(int race, int mode) {
    switch(mode) {
    case 1:     // shorter
        return(Random::get(1,50));
    case 3:     // taller
        return(Random::get(100,150));
    default:    // normal
        return(Random::get(50,100));
    }
}

//*********************************************************************
//                      getHeight
//*********************************************************************

bool Create::getHeight(Socket* sock, std::string str, int mode) {
    if(mode == Create::doPrint) {

        sock->print("\nYour Height\n\n");
        sock->printColor("   [^Wa^x] You are shorter than most members of your race.\n");
        sock->printColor("   [^Wb^x] Your height is average.\n");
        sock->printColor("   [^Wc^x] You are taller than most members of your race.\n\n");

        sock->askFor(": ");

        sock->setState(CUSTOM_HEIGHT);

    } else if(mode == Create::doWork) {

        switch(str[0]) {
        case 'a':
        case 'b':
        case 'c':
            sock->getPlayer()->custom.height = Create::calcHeight(sock->getPlayer()->getRace(), str[0]-'a'+1);
            break;
        default:
            sock->printColor("Invalid selection: ^W%s\n", str.c_str());
            sock->askFor("Choose one: ");

            sock->setState(CUSTOM_HEIGHT);
            return(false);
        }

    }
    return(true);
}

//*********************************************************************
//                      calcWeight
//*********************************************************************

int Create::calcWeight(int race, int mode) {
    switch(mode) {
    case 1:     // thinner
        return(Random::get(1,50));
    case 3:     // stockier
        return(Random::get(100,150));
    default:    // normal
        return(Random::get(50,100));
    }
}

//*********************************************************************
//                      getWeight
//*********************************************************************

bool Create::getWeight(Socket* sock, std::string str, int mode) {
    if(mode == Create::doPrint) {

        sock->print("\nYour Weight\n\n");
        sock->printColor("   [^Wa^x] You are thinner than most members of your race.\n");
        sock->printColor("   [^Wb^x] Your weight is average.\n");
        sock->printColor("   [^Wc^x] You are heavier than most members of your race.\n\n");

        sock->askFor(": ");

        sock->setState(CUSTOM_WEIGHT);

    } else if(mode == Create::doWork) {

        switch(str[0]) {
        case 'a':
        case 'b':
        case 'c':
            sock->getPlayer()->custom.weight = Create::calcWeight(sock->getPlayer()->getRace(), str[0]-'a'+1);
            break;
        default:
            sock->printColor("Invalid selection: ^W%s\n", str.c_str());
            sock->askFor("Choose one: ");

            sock->setState(CUSTOM_WEIGHT);
            return(false);
        }

    }
    return(true);
}

//*********************************************************************
//                      nameIsAllowed
//*********************************************************************
// Return: Is the name allowed? true/false
// Parameters:
//  str:  The name we're testing
//  sock: The socket to print error messages to

bool nameIsAllowed(std::string str, Socket* sock) {
    int i=0, nonalpha=0, len = str.length();

    if(!isalpha(str[0]))
        return(false);

    if(len < 3) {
        sock->print("Name must be at least 3 characters.\n");
        return(false);
    }
    if(len >= 20) {
        sock->print("Name must be less than 20 characters.\n");
        return(false);
    }

    for(i=0; i< len; i++)
        if(!isalpha(str[i]))
            nonalpha++;

    if(nonalpha && len < 6) {
        sock->print("Name must be at least 6 characters in order to contain a - or '.\n");
        return(false);
    }

    if(nonalpha > 1) {
        sock->print("May not have more than one non-alpha character in your name.\n");
        return(false);
    }

    for(i=0; i<len; i++) {
        if(!isalpha(str[i])) {
            sock->print("Name must be alphabetic.\n");
            return(false);
        }
    }

    if(!parse_name(str)) {
        sock->print("That name is not allowed.\n");
        return(false);
    }
    return(true);
}
