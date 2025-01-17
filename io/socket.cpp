/*
 * socket.cpp
 *   Stuff to deal with sockets
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


#include <arpa/telnet.h>                            // for IAC, SE, WILL, SB
#include <ext/alloc_traits.h>                       // for __alloc_traits<>:...
#include <fcntl.h>                                  // for fcntl, F_GETFL
#include <fmt/format.h>                             // for format
#include <netinet/in.h>                             // for htonl, sockaddr_in
#include <sys/socket.h>                             // for linger, setsockopt
#include <unistd.h>                                 // for ssize_t, write
#include <zconf.h>                                  // for Bytef
#include <zlib.h>                                   // for z_stream, deflate
#include <algorithm>                                // for replace
#include <boost/algorithm/string/predicate.hpp>     // for iequals, istarts_...
#include <boost/algorithm/string/replace.hpp>       // for replace_all
#include <boost/iterator/iterator_facade.hpp>       // for operator!=, itera...
#include <boost/iterator/iterator_traits.hpp>       // for iterator_value<>:...
#include <boost/lexical_cast/bad_lexical_cast.hpp>  // for bad_lexical_cast
#include <boost/token_functions.hpp>                // for char_separator
#include <boost/token_iterator.hpp>                 // for token_iterator
#include <boost/tokenizer.hpp>                      // for tokenizer
#include <cctype>                                   // for isalpha, isdigit
#include <cerrno>                                   // for EWOULDBLOCK, errno
#include <cstdarg>                                  // for va_end, va_list
#include <cstdio>                                   // for fseek, size_t, ftell
#include <cstdlib>                                  // for free, atol, calloc
#include <cstring>                                  // for strlen, strcpy
#include <ctime>                                    // for time
#include <deque>                                    // for _Deque_iterator
#include <iostream>                                 // for operator<<, basic...
#include <fstream>                                  // for std::ifstream
#include <list>                                     // for list, operator==
#include <map>                                      // for map
#include <memory>                                   // for allocator, alloca...
#include <queue>                                    // for queue
#include <sstream>                                  // for basic_ostringstre...
#include <string>                                   // for string, basic_string
#include <string_view>                              // for string_view, basi...
#include <vector>                                   // for vector

#include "color.hpp"                                // for stripColor
#include "commands.hpp"                             // for command, changing...
#include "config.hpp"                               // for Config, gConfig
#include "flags.hpp"                                // for P_READING_FILE
#include "free_crt.hpp"                             // for free_crt
#include "global.hpp"                               // for MAXALVL
#include "login.hpp"                                // for createPlayer, CON...
#include "msdp.hpp"                                 // for ReportedMsdpVariable
#include "mud.hpp"                                  // for StartTime
#include "mudObjects/players.hpp"                   // for Player
#include "os.hpp"                                   // for ASSERTLOG
#include "paths.hpp"                                // for Config
#include "post.hpp"                                 // for histedit, postedit
#include "property.hpp"                             // for Property
#include "proto.hpp"                                // for zero
#include "security.hpp"                             // for changePassword
#include "server.hpp"                               // for Server, gServer
#include "socket.hpp"                               // for Socket, Socket::S...
#include "utils.hpp"                                // for MIN, MAX
#include "version.hpp"                              // for VERSION
#include "xml.hpp"                                  // for copyToBool, newBo...

const int MIN_PAGES = 10;

// Static initialization
const int Socket::COMPRESSED_OUTBUF_SIZE = 8192;
int Socket::numSockets = 0;

enum telnetNegotiation {
    NEG_NONE,
    NEG_IAC,
    NEG_WILL,
    NEG_WONT,
    NEG_DO,
    NEG_DONT,

    NEG_SB,
    NEG_START_NAWS,
    NEG_SB_NAWS_COL_HIGH,
    NEG_SB_NAWS_COL_LOW,
    NEG_SB_NAWS_ROW_HIGH,
    NEG_SB_NAWS_ROW_LOW,
    NEG_END_NAWS,

    NEG_SB_TTYPE,
    NEG_SB_TTYPE_END,

    NEG_SB_MSDP,
    NEG_SB_MSDP_END,

    NEG_SB_GMCP,
    NEG_SB_GMCP_END,

    NEG_SB_CHARSET,
    NEG_SB_CHARSET_LOOK_FOR_IAC,
    NEG_SB_CHARSET_END,

    NEG_MXP_SECURE,
    NEG_MXP_SECURE_TWO,
    NEG_MXP_SECURE_THREE,
    NEG_MXP_SECURE_FINISH,
    NEG_MXP_SECURE_CONSUME,

    NEG_UNUSED
};

//********************************************************************
//                      telnet namespace
//********************************************************************

namespace telnet {
// MSDP Support
unsigned const char will_msdp[] = { IAC, WILL, TELOPT_MSDP, '\0' };
unsigned const char wont_msdp[] = { IAC, WONT, TELOPT_MSDP, '\0' };

// GMCP Support
unsigned const char will_gmcp[] = { IAC, WILL, TELOPT_GMCP, '\0' };
unsigned const char wont_gmcp[] = { IAC, WONT, TELOPT_GMCP, '\0' };

// MXP Support
unsigned const char will_mxp[] = { IAC, WILL, TELOPT_MXP, '\0' };
// Start mxp string
unsigned const char start_mxp[] = { IAC, SB, TELOPT_MXP, IAC, SE, '\0' };

// MCCP V2 support
unsigned const char will_comp2[] = { IAC, WILL, TELOPT_COMPRESS2, '\0' };
// MCCP V1 support
unsigned const char will_comp1[] = { IAC, WILL, TELOPT_COMPRESS, '\0' };
// Start string for compress2
unsigned const char start_mccp2[] = { IAC, SB, TELOPT_COMPRESS2, IAC, SE, '\0' };
// start string for compress1
unsigned const char start_mccp[] = { IAC, SB, TELOPT_COMPRESS, WILL, SE, '\0' };

// Echo input
unsigned const char will_echo[] = { IAC, WILL, TELOPT_ECHO, '\0' };

// EOR After every prompt
unsigned const char will_eor[] = { IAC, WILL, TELOPT_EOR, '\0' };

// MSP Support
unsigned const char will_msp[] = { IAC, WILL, TELOPT_MSP, '\0' };
// MSP Stop
unsigned const char wont_msp[] = { IAC, WONT, TELOPT_MSP, '\0' };

// MSSP Support
unsigned const char will_mssp[] = { IAC, WILL, TELOPT_MSSP, '\0' };
// MSSP SB
unsigned const char sb_mssp_start[] = { IAC, SB, TELOPT_MSSP, '\0' };
// MSSP SB stop
unsigned const char sb_mssp_end[] = { IAC, SE, '\0' };

// Terminal type negotation
unsigned const char do_ttype[] = { IAC, DO, TELOPT_TTYPE, '\0' };
unsigned const char wont_ttype[] = { IAC, WONT, TELOPT_TTYPE, '\0' };

// Charset
unsigned const char do_charset[] = { IAC, DO, TELOPT_CHARSET, '\0' };
unsigned const char charset_utf8[] = { IAC, SB, TELOPT_CHARSET, 1, ' ', 'U',
        'T', 'F', '-', '8', IAC, SE, '\0' };

// Start sub negotiation for terminal type
unsigned const char query_ttype[] = { IAC, SB, TELOPT_TTYPE, TELQUAL_SEND, IAC, SE, '\0' };
// Window size negotation NAWS
unsigned const char do_naws[] = { IAC, DO, TELOPT_NAWS, '\0' };

// End of line string
unsigned const char eor_str[] = { IAC, EOR, '\0' };

// MCCP Hooks
void *zlib_alloc(void *opaque, unsigned int items, unsigned int size) {
    return calloc(items, size);
}
void zlib_free(void *opaque, void *address) {
    free(address);
}
}

//--------------------------------------------------------------------
// Constructors, Destructors, etc

//********************************************************************
//                      reset
//********************************************************************

void Socket::reset() {
    fd = -1;

    opts.mccp = 0;
    opts.dumb = true;
    opts.mxp = false;
    opts.msp = false;
    opts.eor = false;
    opts.msdp = false;
    opts.charset = false;
    opts.utf8 = false;
    opts.mxpClientSecure = false;
    opts.color = NO_COLOR;
    opts.xterm256 = false;
    opts.lastColor = '\0';

    opts.compressing = false;
    inPlayerList = false;

    outCompressBuf = nullptr;
    outCompress = nullptr;
    myPlayer = nullptr;

    tState = NEG_NONE;
    oneIAC = watchBrokenClient = false;

    term.type = "dumb";
    term.firstType.clear();
    term.cols = 82;
    term.rows = 40;

    ltime = time(nullptr);
    intrpt = 0;

    fn = nullptr;
    tState = NEG_NONE;
    connState = LOGIN_START;
    lastState = LOGIN_START;

    zero(tempstr, sizeof(tempstr));
    inBuf.clear();
    spyingOn = nullptr;
}

//********************************************************************
//                      Socket
//********************************************************************

Socket::Socket(int pFd) {
    reset();
    fd = pFd;
    numSockets++;
}

Socket::Socket(int pFd, sockaddr_in pAddr, bool dnsDone) {
    reset();

    struct linger ling{};
    fd = pFd;

    resolveIp(pAddr, host.ip);
    resolveIp(pAddr, host.hostName); // Start off with the hostname as the ip, then do an asyncronous lookup

    // Make this socket non blocking
    nonBlock(fd);

    // Set Linger behavior
    ling.l_onoff = ling.l_linger = 0;
    setsockopt(fd, SOL_SOCKET, SO_LINGER, (char *) &ling, sizeof(struct linger));

    numSockets++;
    std::clog << "Constructing socket (" << fd << ") from " << host.ip << " Socket #" << numSockets << std::endl;

    // If we're running under valgrind, we don't resolve dns.  The child process tends to mess with proper memory leak detection
    if (gServer->getDnsCache(host.ip, host.hostName) || gServer->isValgrind()) {
        dnsDone = true;
    } else {
        dnsDone = false;
        setState(LOGIN_DNS_LOOKUP);
        gServer->startDnsLookup(this, pAddr);
    }

    startTelnetNeg();

    showLoginScreen(dnsDone);
}

//********************************************************************
//                      closeFd
//********************************************************************
// Disconnect the underlying file descriptor
void Socket::cleanUp() {
    clearSpying();
    clearSpiedOn();
    msdpClearReporting();

    if (myPlayer) {
        if (myPlayer->fd > -1) {
            myPlayer->save(true);
            myPlayer->uninit();
        }
        freePlayer();
    }
    endCompress();
    if(fd > -1) {
        close(fd);
        fd = -1;
    }

}
//********************************************************************
//                      ~Socket
//********************************************************************

Socket::~Socket() {
    std::cout << "Deconstructing socket , ";
    numSockets--;
    std::cout << "Num sockets: " << numSockets << std::endl;
    cleanUp();
}

//********************************************************************
//                      addToPlayerList
//********************************************************************

void Socket::addToPlayerList() {
    inPlayerList = true;
}

//********************************************************************
//                      freePlayer
//********************************************************************

void Socket::freePlayer() {
    if (myPlayer)
        free_crt(myPlayer, inPlayerList);
    myPlayer = nullptr;
    inPlayerList = false;
}

// End - Constructors, Destructors, etc
//--------------------------------------------------------------------

//********************************************************************
//                      clearSpying
//********************************************************************

void Socket::clearSpying() {
    if (spyingOn) {
        spyingOn->removeSpy(this);
        spyingOn = nullptr;
    }
}

//********************************************************************
//                      clearSpiedOn
//********************************************************************

void Socket::clearSpiedOn() {
    std::list<Socket*>::iterator it;
    for (it = spying.begin(); it != spying.end(); it++) {
        Socket *sock = *it;
        if (sock)
            sock->setSpying(nullptr);
    }
    spying.clear();
}

//********************************************************************
//                      setSpying
//********************************************************************

void Socket::setSpying(Socket *sock) {
    if (sock)
        clearSpying();
    spyingOn = sock;

    if (sock)
        sock->addSpy(this);
    if (!sock) {
        if (myPlayer)
            myPlayer->clearFlag(P_SPYING);
    }
}

//********************************************************************
//                      removeSpy
//********************************************************************

void Socket::removeSpy(Socket *sock) {
    spying.remove(sock);
    if (myPlayer->getClass() >= sock->myPlayer->getClass())
        sock->printColor("^r%s is no longer observing you.\n",
                sock->myPlayer->getCName());
}

//********************************************************************
//                      addSpy
//********************************************************************

void Socket::addSpy(Socket *sock) {
    spying.push_back(sock);
}

//********************************************************************
//                      disconnect
//********************************************************************
// Set this socket for removal on the next cleanup

void Socket::disconnect() {
    setState(CON_DISCONNECTING);
    flush();
    cleanUp();
}

//********************************************************************
//                      resolveIp
//********************************************************************

void Socket::resolveIp(const sockaddr_in &addr, std::string& ip) {
    std::ostringstream tmp;
    long i = htonl(addr.sin_addr.s_addr);
    tmp << ((i >> 24) & 0xff) << "." << ((i >> 16) & 0xff) << "." << ((i >> 8) & 0xff) << "." << (i & 0xff);
    ip = tmp.str();
}

std::string Socket::parseForOutput(std::string_view outBuf) {
    int i = 0;
    auto n = outBuf.size();
    std::ostringstream oStr;
    bool inTag = false;
    unsigned char ch = 0;
    while(i < n) {
        ch = outBuf[i++];
        if(inTag) {
            if(ch == CH_MXP_END) {
                inTag = false;
                if(opts.mxp)
                    oStr << ">" << MXP_LOCK_CLOSE;
            } else if(opts.mxp)
                oStr << ch;

            continue;
        } else {
            if(ch == CH_MXP_BEG) {
                inTag = true;
                if(opts.mxp)
                    oStr << MXP_SECURE_OPEN << "<";
                continue;
            } else {
                if(ch == '^') {
                    ch = outBuf[i++];
                    oStr << getColorCode(ch);
                } else if(ch == '\n') {
                    oStr << "\r\n";
                } else {
                    oStr << ch;
                }
                continue;
            }
        }
    }
    return(oStr.str());

}

bool Socket::needsPrompt(std::string_view inStr) {
    int i = 0;
    auto n = inStr.size();

    while(i < n) {
        if((unsigned char)inStr[i] == IAC) {
            switch((unsigned char)inStr[i+1]) {
                case WILL:
                case WONT:
                case DO:
                    // Skip 3
                    i+=3;
                    continue;
                case EOR:
                    // Skip 2
                    i+=2;
                    continue;
                case SB:
                    // Skip until we find IAC SE
                    while(i < n) {
                        if((unsigned char)inStr[i] == IAC && (unsigned char)inStr[i+1] == SE) {
                            // Skip two more
                            i += 2;
                            break;
                        }
                        i++;
                    }
                    continue;
            }
        }
        return true;
    }
    return false;
}

std::string Socket::stripTelnet(std::string_view inStr) {
    int i = 0;
    auto n = inStr.size();
    std::ostringstream oStr;

    while(i < n) {
        if((unsigned char)inStr[i] == IAC) {
            switch((unsigned char)inStr[i+1]) {
            case WILL:
            case WONT:
            case DO:
                // Skip 3
                i+=3;
                continue;
            case EOR:
                // Skip 2
                i+=2;
                continue;
            case SB:
                // Skip until we find IAC SE
                while(i < n) {
                    if((unsigned char)inStr[i] == IAC && (unsigned char)inStr[i+1] == SE) {
                        // Skip two more
                        i += 2;
                        break;
                    }
                    i++;
                }
                continue;
            }
        }
        oStr << inStr[i++];
    }
    return(oStr.str());
}

//********************************************************************
//                      checkLockOut
//********************************************************************

void Socket::checkLockOut() {
    int lockStatus = gConfig->isLockedOut(this);
    if (lockStatus == 0) {
        askFor("\n\nPlease enter name: ");
        setState(LOGIN_GET_NAME);
    } else if (lockStatus == 2) {
        print("\n\nA password is required to play from your site: ");
        setState(LOGIN_GET_LOCKOUT_PASSWORD);
    } else if (lockStatus == 1) {
        print("\n\nYou are not welcome here. Begone.\n");
        setState(CON_DISCONNECTING);
    }
}

//********************************************************************
//                      startTelnetNeg
//********************************************************************

void Socket::startTelnetNeg() {
    // As noted from KaVir -
    // Some clients (such as GMud) don't properly handle negotiation, and simply
    // display every printable character to the screen.  However TTYPE isn't a
    // printable character, so we negotiate for it first, and only negotiate for
    // other protocols if the client responds with IAC WILL TTYPE or IAC WONT
    // TTYPE.  Thanks go to Donky on MudBytes for the suggestion.

    write(reinterpret_cast<const char *>(telnet::do_ttype), false);

}
void Socket::continueTelnetNeg(bool queryTType) {
    if (queryTType)
        write(reinterpret_cast<const char *>(telnet::query_ttype), false);

    // Not a dumb client if we've gotten a response
    opts.dumb = false;
    write(reinterpret_cast<const char *>(telnet::will_comp2), false);
    write(reinterpret_cast<const char *>(telnet::will_comp1), false);

    write(reinterpret_cast<const char *>(telnet::do_naws), false);
    write(reinterpret_cast<const char *>(telnet::will_msdp), false);
    write(reinterpret_cast<const char *>(telnet::will_mssp), false);
    write(reinterpret_cast<const char *>(telnet::will_msp), false);
//  write(reinterpret_cast<const char *>(telnet::do_charset), false);  // Not implemented yet
    write(reinterpret_cast<const char *>(telnet::will_mxp), false);
    write(reinterpret_cast<const char *>(telnet::will_eor), false);
}

//********************************************************************
//                      processInput
//********************************************************************

int Socket::processInput() {
    unsigned char tmpBuf[1024];
    ssize_t n;
    ssize_t i = 0;
    std::string tmp = "";

    // Attempt to read from the socket
    n = read(getFd(), tmpBuf, 1023);
    if (n <= 0) {
        if (errno != EWOULDBLOCK)
            return (-1);
        else
            return (0);
    }

    tmp.reserve(n);

    InBytes += n;

    tmpBuf[n] = '\0';
    // If we have any full strings, copy it over to the queue to be interpreted

    // Look for any IAC commands using a finite state machine
    for (i = 0; i < n; i++) {
        // For debugging
//        std::clog << "DEBUG:" << (unsigned int)tmpBuf[i] << "'" << (unsigned char)tmpBuf[i] << "'" << "\n";

        // Try to handle zMud, cMud & tintin++ which don't seem to double the IAC for NAWS
        // during my limited testing -JM
        if (oneIAC && tState > NEG_START_NAWS && tState < NEG_END_NAWS && tmpBuf[i] != IAC) {
            // Broken Client
            std::clog << "NAWS: BUG - Broken Client: Non-doubled IAC\n";
            i--;
        }
        if (watchBrokenClient) {
            // If we just finished NAWS with a 255 height...keep an eye out for the next
            // character to be a stray SE
            if (tState == NEG_NONE && (unsigned char) tmpBuf[i] == SE) {
                std::clog << "NAWS: BUG - Stray SE\n";
                // Set the tState to NEG_IAC as it should have been, and carry gracefully on
                tState = NEG_IAC;
            }
            // It should only be the next character, so if we don't find it...don't keep looking for it
            watchBrokenClient = false;
        }

        switch (tState) {
            case NEG_NONE:
                // Expecting an IAC here
                if ((unsigned char) tmpBuf[i] == IAC) {
                    tState = NEG_IAC;
                    break;
                } else if((unsigned char)tmpBuf[i] == '\033') {
                    tState = NEG_MXP_SECURE;
                    break;
                } else {
                    tmp += tmpBuf[i];
                    break;
                }
                break;
            case NEG_MXP_SECURE:
                if(tmpBuf[i] == '[') {
                    tState = NEG_MXP_SECURE_TWO;
                    break;
                } else {
                    tmp += fmt::format("\033{}", tmpBuf[i]);
                }
                tState = NEG_NONE;
                break;
            case NEG_MXP_SECURE_TWO:
                if(tmpBuf[i] == '1') {
                    tState = NEG_MXP_SECURE_FINISH;
                    break;
                } else {
                    tmp += fmt::format("\033[{}", tmpBuf[i]);
                }
                tState = NEG_NONE;
                break;
            case NEG_MXP_SECURE_FINISH:
                if(tmpBuf[i] == 'z') {
                    opts.mxpClientSecure = true;
                    tState = NEG_MXP_SECURE_CONSUME;
                    std::clog << "Client secure MXP mode enabled" << std::endl;
                    break;
                } else {
                    tmp += fmt::format("\033[1{}",tmpBuf[i]);
                }
                tState = NEG_NONE;
                break;
            case NEG_MXP_SECURE_CONSUME:
                if(tmpBuf[i] == '\n') {
                    tState = NEG_NONE;
                    parseMXPSecure();
                } else {
                    cmdInBuf.push_back(tmpBuf[i]);
                }
                break;
            case NEG_IAC:
                switch ((unsigned char) tmpBuf[i]) {
                    case NOP:
                    case IP:
                    case GA:
                        tState = NEG_NONE;
                        break;
                    case WILL:
                        tState = NEG_WILL;
                        break;
                    case WONT:
                        tState = NEG_WONT;
                        break;
                    case DO:
                        tState = NEG_DO;
                        break;
                    case DONT:
                        tState = NEG_DONT;
                        break;
                    case SE:
                        tState = NEG_NONE;
                        break;
                    case SB:
                        tState = NEG_SB;
                        break;
                    case IAC:
                        // Doubled IAC, send along to parser
                        tmp += tmpBuf[i];
                        tState = NEG_NONE;
                        break;
                    default:
                        tState = NEG_NONE;
                        break;
                }
                break;
                // Handle Do and Will
            case NEG_DO:
            case NEG_WILL:
            case NEG_DONT:
            case NEG_WONT:
                negotiate((unsigned char) tmpBuf[i]);
                break;
            case NEG_SB:
                switch ((unsigned char) tmpBuf[i]) {
                    case NAWS:
                        tState = NEG_SB_NAWS_COL_HIGH;
                        break;
                    case TTYPE:
                        tState = NEG_SB_TTYPE;
                        break;
                    case CHARSET:
                        if (charsetEnabled())
                            tState = NEG_SB_CHARSET;
                        else
                            tState = NEG_NONE;
                        break;
                    case MSDP:
                        tState = NEG_SB_MSDP;
                        break;
                    case TELOPT_GMCP:
                        tState = NEG_SB_GMCP;
                        break;
                    default:
                        std::clog << "Unknown Sub Negotiation: " << (int)tmpBuf[i] << std::endl;
                        tState = NEG_NONE;
                        break;
                }
                break;
            case NEG_SB_MSDP:
                cmdInBuf.push_back(tmpBuf[i]);
                if (tmpBuf[i] == IAC) {
                    tState = NEG_SB_MSDP_END;
                    break;
                }
                break;
            case NEG_SB_MSDP_END:
                if (tmpBuf[i] == SE) {
                    // We should have a full MDSP command now, let's parse it now
                    parseMsdp();
                    tState = NEG_NONE;
                    break;
                } else {
                    // Not an SE: The last input was an IAC, so push the new input
                    // onto the inbuf and keep going
                    cmdInBuf.push_back(tmpBuf[i]);
                    tState = NEG_SB_MSDP;
                    break;
                }
                break;
            case NEG_SB_GMCP:
                // We don't handle this right now, but ignoring it because Mudlet likes to send it anyway
                if(tmpBuf[i] == IAC) {
                    tState = NEG_SB_GMCP_END;
                    break;
                }
                break;
            case NEG_SB_GMCP_END:
                if (tmpBuf[i] == SE) {
                    // We should have a full GMCP command now, let's ignore it
                    tState = NEG_NONE;
                    break;
                } else {
                    // Not an SE: The last input was an IAC, so keep going
                    cmdInBuf.push_back(tmpBuf[i]);
                    tState = NEG_SB_GMCP;
                    break;
                }
                break;
            case NEG_SB_CHARSET:
                // We've only asked for UTF-8, so assume if they respond it's for that and just eat the rest of the input
                //
                // Any other sub-negotiations (such as TTABLE-*) are not handled
                if (tmpBuf[i] == ACCEPTED) {
                    std::clog << "Enabled UTF8" << std::endl;
                    opts.utf8 = true;
                    tState = NEG_SB_CHARSET_LOOK_FOR_IAC;
                } else if (tmpBuf[i] == REJECTED) {
                    opts.utf8 = false;
                    tState = NEG_SB_CHARSET_LOOK_FOR_IAC;
                } else {
                    tState = NEG_SB_CHARSET_LOOK_FOR_IAC;
                }
                break;
            case NEG_SB_CHARSET_LOOK_FOR_IAC:
                // Do nothing while we wait for an IAC
                if (tmpBuf[i] == IAC)
                    tState = NEG_SB_CHARSET_END;
                break;
            case NEG_SB_CHARSET_END:
                if (tmpBuf[i] == IAC) {
                    // Double IAC, part of the data
                    tState = NEG_SB_CHARSET_LOOK_FOR_IAC;
                    break;
                } else if (tmpBuf[i] == SE) {
                    // Found what we were looking for
                } else {
                    std::clog << "NEG_SB_CHARSET_END Error: Expected SE, got '" << (int) tmpBuf[i] << "'" << std::endl;
                }
                tState = NEG_NONE;
                break;
            case NEG_SB_TTYPE:
                // Grab the terminal type
                if (tmpBuf[i] == TELQUAL_IS) {
                    term.lastType = term.type;
                    term.type.erase();
                    term.type = "";
                } else if (tmpBuf[i] == IAC) {
                    // Expect a SE next
                    tState = NEG_SB_TTYPE_END;
                    break;
                } else {
                    term.type += tmpBuf[i];
                }
                break;
            case NEG_SB_TTYPE_END:
                if (tmpBuf[i] == SE) {
                    std::clog << "Found term type: " << term.type << std::endl;
                    // We haven't cycled back around to the first term type, and
                    // No previous term type or the current term type isn't the same as the last
                    if ((term.firstType != term.type) && (term.lastType.empty() ||  (term.type != term.lastType))) {
                        term.lastType = "";
                        // Look for 256 color support
                        if (term.type.find("-256color") != std::string::npos) {
                            // Works for tintin++, wintin++ and blowtorch
                            opts.xterm256 = true;
                        }

                        // Request another!
                        write(reinterpret_cast<const char *>(telnet::query_ttype), false);
                    }
                    if (term.firstType.empty()) {
                        term.firstType = term.type;
                    }

                    if (term.type.find("Mudlet") != std::string::npos and term.type > "Mudlet 1.1") {
                        opts.xterm256 = true;
                    } else if(boost::iequals(term.type, "EMACS-RINZAI") || term.type.find("DecafMUD") != std::string::npos) {
                        opts.xterm256 = true;
                    }

                } else if (tmpBuf[i] == IAC) {
                    // I doubt this will happen
                    std::clog << "NEG_SB_TTYPE: Found double IAC" << std::endl;
                    term.type += tmpBuf[i];
                    tState = NEG_SB_TTYPE;
                    break;
                } else {
                    std::clog << "NEG_SB_TTYPE_END Error: Expected SE, got '" << (int) tmpBuf[i] << "'" << std::endl;
                }

                tState = NEG_NONE;
                break;
            case NEG_SB_NAWS_COL_HIGH:
                if (handleNaws(term.cols, tmpBuf[i], true))
                    tState = NEG_SB_NAWS_COL_LOW;
                break;
            case NEG_SB_NAWS_COL_LOW:
                if (handleNaws(term.cols, tmpBuf[i], false))
                    tState = NEG_SB_NAWS_ROW_HIGH;
                break;
            case NEG_SB_NAWS_ROW_HIGH:
                if (handleNaws(term.rows, tmpBuf[i], true))
                    tState = NEG_SB_NAWS_ROW_LOW;
                break;
            case NEG_SB_NAWS_ROW_LOW:
                if (handleNaws(term.rows, tmpBuf[i], false)) {
                    std::clog << "New term size: " << term.cols << " x " << term.rows << std::endl;
                    // Some clients (tintin++, cmud, possibly zmud) don't seem to double an IAC(255) when it's
                    // sent as data, if this happens in the cols...we should be able to gracefully catch it
                    // but if it happens in the rows...it'll eat the IAC from IAC SE and cause problems,
                    // so we set the state machine to keep an eye out for a stray SE if the rows were set to 255
                    if (term.rows == 255)
                        watchBrokenClient = true;
                    tState = NEG_NONE;
                }
                break;
            default:
                std::clog << "Unhandled state" << std::endl;
                tState = NEG_NONE;
                break;
        }
    }

    // Handles the screwy windows telnet, and its not that hard for
    // other clients that send \n\r too
    std::replace(tmp.begin(), tmp.end(), '\r', '\n');
    inBuf += tmp;

    // handle backspaces
    n = inBuf.length();

    for (i = MAX<int>(n - tmp.length(), 0); i < (unsigned) n; i++) {
        if (inBuf.at(i) == '\b' || inBuf.at(i) == 127) {
            if (n < 2) {
                inBuf = "";
                n = 0;
            } else {
                inBuf.erase(i - 1, 2);
                n -= 2;
                i--;
            }
        }
    }

    std::string::size_type idx = 0;
    while ((idx = inBuf.find("\n", 0)) != std::string::npos) {
        std::string tmpr = inBuf.substr(0, idx); // Don't copy the \n
        idx += 1; // Consume the \n
        if (inBuf[idx] == '\n')
            idx += 1; // Consume the extra \n if applicable

        inBuf.erase(0, idx);
        if(opts.mxpClientSecure) {
            if(boost::istarts_with(inBuf, "<version")) {
                std::clog << "Got msxp version\n";
            } else if(boost::istarts_with(inBuf, "<supports")) {
                std::clog << "Got msxp supports\n";
            }
        }
        input.push(tmpr);
    }
    ltime = time(nullptr);
    return (0);
}

bool Socket::negotiate(unsigned char ch) {

    switch (ch) {
        case TELOPT_CHARSET:
            if (tState == NEG_WILL) {
                opts.charset = true;
                write(reinterpret_cast<const char *>(telnet::charset_utf8), false);
                std::clog << "Charset On" << std::endl;
            } else if (tState == NEG_WONT) {
                opts.charset = false;
                std::clog << "Charset Off" << std::endl;
            }
            tState = NEG_NONE;
            break;
        case TELOPT_TTYPE:
            // If we've gotten this far, we're fairly confident they support ANSI color
            // so enable that
            opts.color = ANSI_COLOR;

            // If we get here it's clearly not a dumb terminal, however if
            // dumb is still set, it means we haven't negotiated, so let's negotiate now
            if (opts.dumb) {
                if (tState == NEG_WILL) {
                    // Continue and query the rest of the options, including term type
                    std::clog << "Continuing telnet negotiation\n";
                    continueTelnetNeg(true);
                } else if (tState == NEG_WONT) {
                    // If they respond to something here they know how to negotiate,
                    // so continue and ask for the rest of the options, except term type
                    // which they have just indicated they won't do
                    write(reinterpret_cast<const char *>(telnet::wont_ttype));
                    continueTelnetNeg(false);

                }
            }
            tState = NEG_NONE;
            break;
        case TELOPT_MXP:
            if (tState == NEG_WILL || tState == NEG_DO) {
                write(reinterpret_cast<const char *>(telnet::start_mxp));
                // Start off in MXP LOCKED CLOSED
                //TODO: send elements we're using for mxp
                opts.mxp = true;
                std::clog << "Enabled MXP" << std::endl;
                defineMxp();
            } else if (tState == NEG_WONT || tState == NEG_DONT) {
                opts.mxp = false;
                std::clog << "Disabled MXP" << std::endl;
            }
            tState = NEG_NONE;
            break;
        case TELOPT_COMPRESS2:
            if (tState == NEG_WILL || tState == NEG_DO) {
                opts.mccp = 2;
                startCompress();
            } else if (tState == NEG_WONT || tState == NEG_DONT) {
                if (opts.mccp == 2) {
                    opts.mccp = 0;
                    endCompress();
                }
            }
            tState = NEG_NONE;
            break;
        case TELOPT_COMPRESS:
            if (tState == NEG_WILL || tState == NEG_DO) {
                opts.mccp = 1;
                startCompress();
            } else if (tState == NEG_WONT || tState == NEG_DONT) {
                if (opts.mccp == 1) {
                    opts.mccp = 0;
                    endCompress();
                }
            }
            tState = NEG_NONE;
            break;
        case TELOPT_EOR:
            if (tState == NEG_WILL || tState == NEG_DO) {
                opts.eor = true;
                std::clog << "Activating EOR\n";
            } else if (tState == NEG_WONT || tState == NEG_DONT) {
                opts.eor = false;
                std::clog << "Deactivating EOR\n";
            }
            tState = NEG_NONE;
            break;
        case TELOPT_NAWS:
            opts.naws = (tState == NEG_WILL);
            tState = NEG_NONE;
            break;
        case TELOPT_ECHO:
        case TELOPT_NEW_ENVIRON:
            // TODO: Echo/New Environ
            tState = NEG_NONE;
            break;
        case TELOPT_MSSP:
            if (tState == NEG_DO) {
                sendMSSP();
            }
            tState = NEG_NONE;
            break;
        case TELOPT_MSP:
            if (tState == NEG_WILL || tState == NEG_DO) {
                opts.msp = true;
            } else if (tState == NEG_WONT || tState == NEG_DONT) {
                opts.msp = false;
            }

            tState = NEG_NONE;
            break;
        case TELOPT_MSDP:
            if (tState == NEG_DO) {
                opts.msdp = true;
                msdpSend("SERVER_ID");

                std::clog << "Enabled MSDP" << std::endl;
            } else {
                std::clog << "Disabled MSDP" << std::endl;
                opts.msdp = false;
            }
            tState = NEG_NONE;
            break;
        default:
            tState = NEG_NONE;
            break;
    }
    return (true);
}

//********************************************************************
//                      handleNaws
//********************************************************************
// Return true if a state should be changed

bool Socket::handleNaws(int& colRow, unsigned char& chr, bool high) {
    // If we get an IAC here, we need a double IAC
    if (chr == IAC) {
        if (!oneIAC) {
            oneIAC = true;
            return (false);
        } else {
            oneIAC = false;
        }
    } else if (oneIAC && chr != IAC) {
        // Error!
        std::clog << "NAWS: BUG - Expecting a doubled IAC, got " << (unsigned int) chr << "\n";
        oneIAC = false;
    }

    if (high)
        colRow = chr << 8;
    else
        colRow += chr;

    return (true);
}

//********************************************************************
//                      processOneCommand
//********************************************************************
// Aka interpreter

int Socket::processOneCommand() {
    std::string cmd = input.front();
    input.pop();

    // Send the command to the people we're spying on
    if (!spying.empty()) {
        std::list<Socket*>::iterator it;
        for (const auto sock : spying) {
            if (sock) sock->write(fmt::format("[{}]\n", cmd), false);
        }
    }

    ((void(*)(Socket*, std::string)) (fn))(this, cmd);

    return (1);
}

//********************************************************************
//                      restoreState
//********************************************************************
// Returns a fd to its previous state

void Socket::restoreState() {
    setState(lastState);
    createPlayer(this, "");
}

//*********************************************************************
//                      pauseScreen
//*********************************************************************

void pauseScreen(Socket* sock, const std::string &str) {
    if(str == "quit")
        sock->disconnect();
    else
        sock->reconnect();
}

//*********************************************************************
//                      reconnect
//*********************************************************************

void Socket::reconnect(bool pauseScreen) {
    clearSpying();
    clearSpiedOn();
    msdpClearReporting();

    freePlayer();

    if (pauseScreen) {
        setState(LOGIN_PAUSE_SCREEN);
        printColor(
                "\nPress ^W[RETURN]^x to reconnect or type ^Wquit^x to disconnect.\n: ");
    } else {
        setState(LOGIN_GET_NAME);
        showLoginScreen();
    }
}


void viewFileReverse(Socket *sock, const std::string& file) {
    sock->viewFileReverse(file);
}


void handlePaging(Socket* sock, const std::string& inStr) {
    sock->handlePaging(inStr);
}

void Socket::sendPages(int numPages) {
    for(int i=numPages;i>0;i--) {
        println(pagerOutput.front());
        pagerOutput.pop_front();
        paged++;
    }
}

void Socket::handlePaging(const std::string& inStr) {
    if(inStr == "") {
        int numPages = MIN<int>(getMaxPages(), pagerOutput.size());
        sendPages(numPages);

        if(!pagerOutput.empty()) {
            askFor("\n[Hit Return, Any Key to Quit]: ");
        }
    } else {
        println("Aborting and clearing pager output");
        pagerOutput.clear();
    }

    if(pagerOutput.empty())
        paged = 0;

}
//*********************************************************************
//                      setState
//*********************************************************************
// Sets a fd's state and changes the interpreter to the appropriate function

void Socket::setState(int pState, char pFnParam) {
    // Only store the last state if we're changing states, used mainly in the viewing file states
    if (pState != connState)
        lastState = connState;
    connState = pState;

    if (pState == LOGIN_PAUSE_SCREEN)
        fn = pauseScreen;
    else if (pState > LOGIN_START && pState < LOGIN_END)
        fn = login;
    else if (pState > CREATE_START && pState < CREATE_END)
        fn = createPlayer;
    else if (pState > CON_START && pState < CON_END)
        fn = command;
    else if (pState > CON_STATS_START && pState < CON_STATS_END)
        fn = changingStats;
    else if (pState == CON_CONFIRM_SURNAME)
        fn = doSurname;
    else if (pState == CON_CONFIRM_TITLE)
        fn = doTitle;
    else if (pState == CON_SENDING_MAIL)
        fn = postedit;
    else if (pState == CON_EDIT_HISTORY)
        fn = histedit;
    else if (pState == CON_EDIT_PROPERTY)
        fn = Property::descEdit;
    else if (pState == CON_VIEWING_FILE_REVERSE)
        fn = ::viewFileReverse;
    else if (pState > CON_PASSWORD_START && pState < CON_PASSWORD_END)
        fn = changePassword;
    else if (pState == CON_CHOSING_WEAPONS)
        fn = convertNewWeaponSkills;
    else {
        std::clog << "Unknown connected state!\n";
        fn = login;
    }

    fnparam = (char) pFnParam;
}

std::string getMxpTag( std::string_view tag, std::string text ) {
    std::string::size_type n = text.find(tag);
    if(n == std::string::npos)
        return("");

    std::ostringstream oStr;
    // Add the legnth of the tag
    n += tag.length();
    if( n < text.length()) {
        // If our first char is a quote, advance
        if(text[n] == '\"')
            n++;
        while(n < text.length()) {
            char ch = text[n++];
            if(ch == '.' || isdigit(ch) || isalpha(ch) ) {
                oStr << ch;
           } else {
               return(oStr.str());
           }
        }
    }
    return("");
}

bool Socket::parseMXPSecure() {
    if(mxpEnabled()) {
        std::string toParse(reinterpret_cast<char*>(&cmdInBuf[0]), cmdInBuf.size());
        std::clog << toParse << std::endl;

        std::string client = getMxpTag("CLIENT=", toParse);
        if (!client.empty()) {
            // Overwrite the previous client name - this is harder to fake
            term.type = client;
        }

        std::string version = getMxpTag("VERSION=", toParse);
        if(!version.empty()) {
            term.version = version;
            if(boost::iequals(term.type, "mushclient")) {
                opts.xterm256 = (version >= "4.02");
            } else if (boost::iequals(term.type, "cmud")) {
                opts.xterm256 = (version >= "3.04");
            } else if (boost::iequals(term.type, "atlantis")) {
                // Any version of atlantis with MXP supports xterm256
                opts.xterm256 = true;
            }
        }

        std::string supports = getMxpTag("SUPPORT=", toParse);
        if(!supports.empty()) {
            std::clog << "Got <SUPPORT='" << supports << "'>" << std::endl;
        }

    }
    clearMxpClientSecure();
    cmdInBuf.clear();
    return(true);
}

bool Socket::parseMsdp() {
    if(msdpEnabled()) {
        std::string var, val;
        int nest = 0;

        var.reserve(15);
        val.reserve(30);
        ssize_t i = 0, n = cmdInBuf.size();
        while (i < n && cmdInBuf[i] != SE) {
            switch (cmdInBuf[i]) {
            case MSDP_VAR:
                i++;
                while (i < n && cmdInBuf[i] != MSDP_VAL) {
                    var += cmdInBuf[i++];
                }
                break;
            case MSDP_VAL:
                i++;
                val.erase();
                while (i < n && cmdInBuf[i] != IAC) {
                    if (cmdInBuf[i] == MSDP_TABLE_OPEN
                            || cmdInBuf[i] == MSDP_ARRAY_OPEN)
                        nest++;
                    else if (cmdInBuf[i] == MSDP_TABLE_CLOSE
                            || cmdInBuf[i] == MSDP_ARRAY_CLOSE)
                        nest--;
                    else if (nest == 0
                            && (cmdInBuf[i] == MSDP_VAR || cmdInBuf[i] == MSDP_VAL))
                        break;
                    val += cmdInBuf[i++];
                }
                if (nest == 0)
                    processMsdpVarVal(var, val);

                break;
            default:
                i++;
                break;
            }
        }
    }
    cmdInBuf.clear();

    return (true);
}

//********************************************************************
//                      bprint
//********************************************************************
// Append a string to the socket's paged output queue
void Socket::printPaged(std::string_view toPrint) {
    boost::char_separator<char> sep("\n");
    boost::tokenizer<boost::char_separator<char> > tokens(toPrint, sep);
    for(const auto& line : tokens) {
        pagerOutput.emplace_back(line);
    }
}

int Socket::getMaxPages() const {
    return MAX(term.rows - 2, MIN_PAGES);
}

void Socket::donePaging() {
    const int maxRows = getMaxPages();
    if (paged < maxRows) {
        // Send lines up to the first page size
        sendPages(MIN<int>(pagerOutput.size(), maxRows - paged));
        if(paged == maxRows)
            askFor("\n[Hit Return, Any Key to Quit]: ");
    }

    if(paged < maxRows) {
        // We're done paging and never hit the max pages, so clear paged
        paged = 0;
    }
}

void Socket::appendPaged(std::string_view toAppend) {
    if(pagerOutput.empty()) {
        // Paging output is empty, nothing to append to, use normal printPaged logic
        printPaged(toAppend);
        // And add an empty line for subsequent appendPaged to append to
        pagerOutput.emplace_back("");
    } else {
        // It's not empty, see if we're appending to what's already there, or if we're sending a multiline output
        auto pos = toAppend.find('\n');
        if (pos == std::string_view::npos) {
            // There is no newline, append to what we previously sent
            pagerOutput.back().append(toAppend);
        } else {
            // There is a newline, append everything before the newline to the previous page
            pagerOutput.back().append(toAppend.substr(0, pos));
            // Print the rest
            printPaged(toAppend.substr(pos + 1));
            // And add an empty line for subsequent appendPaged to append to
            pagerOutput.emplace_back("");
        }
    }
}
//********************************************************************
//                      bprint
//********************************************************************
// Append a string to the socket's output queue

void Socket::bprint(std::string_view toPrint) {
    if (!toPrint.empty())
        output.append(toPrint);
}

void Socket::bprintPython(const std::string& toPrint) {
    if (!toPrint.empty())
        output.append(toPrint);
}

//********************************************************************
//                      println
//********************************************************************
// Append a string to the socket's output queue with a \n

void Socket::println(std::string_view toPrint) {
    bprint(fmt::format("{}\n", toPrint));
}

//********************************************************************
//                      print
//********************************************************************

void Socket::print(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::string newFmt = stripColor(fmt);
    vprint( newFmt.c_str(), ap);
    va_end(ap);
}

//********************************************************************
//                      printColor
//********************************************************************

void Socket::printColor(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprint(fmt, ap);
    va_end(ap);
}

//********************************************************************
//                      flush
//********************************************************************
// Flush pending output and send a prompt

void Socket::flush() {
    ssize_t n;
    if(!processedOutput.empty()) {
        n = write(processedOutput, false, false);
    } else {
        if ((n = write(output)) == 0)
            return;
        output.clear();
    }
    // If we only wrote OOB data or partial data was written because of EWOULDBLOCK,
    // then n is -2, don't send a prompt in that case
    if (n != -2 && myPlayer && connState != CON_CHOSING_WEAPONS && pagerOutput.empty())
        myPlayer->sendPrompt();
}

//********************************************************************
//                      write
//********************************************************************
// Write a string of data to the socket's file descriptor

ssize_t Socket::write(std::string_view toWrite, bool pSpy, bool process) {
    ssize_t written = 0;
    ssize_t n = 0;
    size_t total = 0;

    // Parse any color, unicode, etc here
    std::string toOutput;
    if(process)
        toOutput = parseForOutput(toWrite);
    else {
        toOutput = toWrite;
    }

    total = toOutput.length();

    const char *str = toOutput.c_str();
    // Write directly to the socket, otherwise compress it and send it
    if (!opts.compressing) {
        do {
            n = ::write(fd, str + written, total - written);
            if (n < 0) {
                if(errno != EWOULDBLOCK)
                    return (n);
                else  {
                    // The write would have blocked
                    n = -2;
                    // If we haven't written the total number of bytes planned save the remaining string for the next go around
                    if(written < total) {
                        processedOutput = str + written;
                    }
                    break;
                }
            }
            written += n;
        } while (written < total);

        UnCompressedBytes += written;

        if(n == -2)
            written = -2;

        if(written >= total && !processedOutput.empty() && !process) {
            processedOutput.erase();
        }
    } else {
        UnCompressedBytes += total;

        outCompress->next_in = (unsigned char*) str;
        outCompress->avail_in = total;
        while (outCompress->avail_in) {
            outCompress->avail_out =
                    COMPRESSED_OUTBUF_SIZE
                            - ((char*) outCompress->next_out
                                    - (char*) outCompressBuf);
            if (deflate(outCompress, Z_SYNC_FLUSH) != Z_OK) {
                return (0);
            }
            written += processCompressed();
            if (written == 0)
                break;
        }
    }

    if (pSpy && !spying.empty()) {
        std::string forSpy = Socket::stripTelnet(toWrite);

        boost::replace_all(forSpy, "\n", "\n<Spy> ");
        if(!forSpy.empty()) {
            std::list<Socket*>::iterator it;
            for(const auto sock : spying) {
                if (sock)
                    sock->write("<Spy> " + forSpy, false);
            }
        }
    }
    // Keep track of total outbytes
    OutBytes += written;

    // If stripped len is 0, it means we only wrote OOB data, so adjust the return so we don't send another prompt
    if(!needsPrompt(toWrite))
        written = -2;

    return (written);
}

//--------------------------------------------------------------------
// MCCP

//********************************************************************
//                      startCompress
//********************************************************************

int Socket::startCompress(bool silent) {
    if (!opts.mccp)
        return (-1);

    if (opts.compressing)
        return (-1);

    outCompressBuf = new char[COMPRESSED_OUTBUF_SIZE];
    //out_compress = new z_stream;
    outCompress = (z_stream *) malloc(sizeof(*outCompress));
    outCompress->zalloc = telnet::zlib_alloc;
    outCompress->zfree = telnet::zlib_free;
    outCompress->opaque = nullptr;
    outCompress->next_in = nullptr;
    outCompress->avail_in = 0;
    outCompress->next_out = (Bytef*) outCompressBuf;
    outCompress->avail_out = COMPRESSED_OUTBUF_SIZE;

    if (deflateInit(outCompress, 9) != Z_OK) {
        // Problem with zlib, try to clean up
        delete outCompressBuf;
        free(outCompress);
        return (-1);
    }

    if (!silent) {
        if (opts.mccp == 2)
            write(reinterpret_cast<const char *>(telnet::start_mccp2), false);
        else
            write(reinterpret_cast<const char *>(telnet::start_mccp), false);
    }
    // We're compressing now
    opts.compressing = true;

    return (0);
}

//********************************************************************
//                      endCompress
//********************************************************************

int Socket::endCompress() {
    if (outCompress && opts.compressing) {
        unsigned char dummy[1] = { 0 };
        outCompress->avail_in = 0;
        outCompress->next_in = dummy;
        // process any remaining output first?
        if (deflate(outCompress, Z_FINISH) != Z_STREAM_END) {
            std::clog << "Error with deflate Z_FINISH\n";
            return (-1);
        }

        // Send any residual data
        if (processCompressed() < 0)
            return (-1);

        deflateEnd(outCompress);

        delete[] outCompressBuf;

        outCompress = nullptr;
        outCompressBuf = nullptr;

        opts.mccp = 0;
        opts.compressing = false;
    }
    return (-1);
}

//********************************************************************
//                      processCompressed
//********************************************************************

size_t Socket::processCompressed() {
    auto len = (size_t) ((char*) outCompress->next_out - (char*) outCompressBuf);
    size_t written = 0;
    size_t block;
    ssize_t n, i;

    if (len > 0) {
        for (i = 0, n = 0; i < len; i += n) {
            block = MIN<size_t>(len - i, 4096);
            if ((n = ::write(fd, outCompressBuf + i, block)) < 0)
                return (-1);
            written += n;
            if (n == 0)
                break;
        }
        if (i) {
            if (i < len)
                memmove(outCompressBuf, outCompressBuf + i, len - i);

            outCompress->next_out = (Bytef*) outCompressBuf + len - i;
        }
    }
    return (written);
}
// End - MCCP
//--------------------------------------------------------------------

// "Telopts"
bool Socket::saveTelopts(xmlNodePtr rootNode) {
    rootNode = xml::newStringChild(rootNode, "Telopts");
    xml::newNumChild(rootNode, "MCCP", mccpEnabled());
    xml::newNumChild(rootNode, "MSDP", msdpEnabled());
    xml::newBoolChild(rootNode, "MXP", mxpEnabled());
    xml::newBoolChild(rootNode, "DumbClient", isDumbClient());
    xml::newStringChild(rootNode, "Term", getTermType());
    xml::newNumChild(rootNode, "Color", getColorOpt());
    xml::newNumChild(rootNode, "TermCols", getTermCols());
    xml::newNumChild(rootNode, "TermRows", getTermRows());
    xml::newBoolChild(rootNode, "EOR", eorEnabled());
    xml::newBoolChild(rootNode, "Charset", charsetEnabled());
    xml::newBoolChild(rootNode, "UTF8", utf8Enabled());

    return (true);
}
bool Socket::loadTelopts(xmlNodePtr rootNode) {
    xmlNodePtr curNode = rootNode->children;

    while (curNode) {
        if (NODE_NAME(curNode, "MCCP")) {
            int mccp = 0;
            xml::copyToNum(mccp, curNode);
            if (mccp) {
                write(reinterpret_cast<const char *>(telnet::will_comp2), false);
            }
        }
        else if (NODE_NAME(curNode, "MXP")) xml::copyToBool(opts.mxp, curNode);
        else if (NODE_NAME(curNode, "Color")) xml::copyToNum(opts.color, curNode);
        else if (NODE_NAME(curNode, "MSDP"))  xml::copyToBool(opts.msdp, curNode);
        else if (NODE_NAME(curNode, "Term")) xml::copyToString(term.type, curNode);
        else if (NODE_NAME(curNode, "DumbClient")) xml::copyToBool(opts.dumb, curNode);
        else if (NODE_NAME(curNode, "TermCols")) xml::copyToNum(term.cols, curNode);
        else if (NODE_NAME(curNode, "TermRows")) xml::copyToNum(term.rows, curNode);
        else if (NODE_NAME(curNode, "EOR")) xml::copyToBool(opts.eor, curNode);
        else if (NODE_NAME(curNode, "Charset")) xml::copyToBool(opts.charset, curNode);
        else if (NODE_NAME(curNode, "UTF8")) xml::copyToBool(opts.utf8, curNode);

        curNode = curNode->next;
    }

    if (opts.msdp) {
        // Re-negotiate MSDP after a reboot
        write(reinterpret_cast<const char *>(telnet::will_msdp), false);
    }

    return (true);
}

//********************************************************************
//                      hasOutput
//********************************************************************

bool Socket::hasOutput() const {
    return (!processedOutput.empty() || !output.empty());
}

//********************************************************************
//                      hasCommand
//********************************************************************

bool Socket::hasCommand() const {
    return (!input.empty());
}

//********************************************************************
//                      canForce
//********************************************************************
// True if the socket is playing (ie: fn is command and fnparam is 1)

bool Socket::canForce() const {
    return (fn == (void(*)(Socket*, const std::string&)) ::command && fnparam == 1);
}

//********************************************************************
//                      get
//********************************************************************

int Socket::getState() const {
    return (connState);
}
bool Socket::isConnected() const {
    return (connState < LOGIN_START && connState != CON_DISCONNECTING);
}
bool Player::isConnected() const {
    return (getSock()->isConnected());
}

int Socket::getFd() const {
    return (fd);
}
bool Socket::mxpEnabled() const {
    return (opts.mxp);
}
bool Socket::getMxpClientSecure() const {
    return(opts.mxpClientSecure);
}
void Socket::clearMxpClientSecure() {
    opts.mxpClientSecure = false;
}
int Socket::mccpEnabled() const {
    return (opts.mccp);
}
bool Socket::msdpEnabled() const {
    return (opts.msdp);
}

bool Socket::mspEnabled() const {
    return (opts.msp);
}

bool Socket::charsetEnabled() const {
    return (opts.charset);
}

bool Socket::utf8Enabled() const {
    return (opts.utf8);
}
bool Socket::eorEnabled() const {
    return (opts.eor);
}
bool Socket::isDumbClient() const {
    return(opts.dumb);
}
bool Socket::nawsEnabled() const {
    return (opts.naws);
}
long Socket::getIdle() const {
    return (time(nullptr) - ltime);
}
std::string_view Socket::getIp() const {
    return (host.ip);
}
std::string_view Socket::getHostname() const {
    return (host.hostName);
}
std::string Socket::getTermType() const {
    return (term.type);
}
int Socket::getColorOpt() const {
    return(opts.color);
}
void Socket::setColorOpt(int opt) {
    opts.color = opt;
}
int Socket::getTermCols() const {
    return (term.cols);
}
int Socket::getTermRows() const {
    return (term.rows);
}

int Socket::getParam() {
    return (fnparam);
}
void Socket::setParam(int newParam) {
    fnparam = newParam;
}

void Socket::setHostname(std::string_view pName) {
    host.hostName = pName;
}
void Socket::setIp(std::string_view pIp) {
    host.ip = pIp;
}
void Socket::setPlayer(Player* ply) {
    myPlayer = ply;
}

bool Socket::hasPlayer() const {
    return myPlayer != nullptr;
}

Player* Socket::getPlayer() const {
    return (myPlayer);
}

//********************************************************************
//                      nonBlock
//********************************************************************

int nonBlock(int pFd) {
    int flags;
    flags = fcntl(pFd, F_GETFL, 0);
    flags |= O_NONBLOCK;
    if (fcntl(pFd, F_SETFL, flags) < 0)
        return (-1);
    return (0);
}

//*********************************************************************
//                      showLoginScreen
//*********************************************************************
const auto LOGIN_FILE = fmt::format("{}/login_screen.txt", Path::Config);

void Socket::showLoginScreen(bool dnsDone) {
    //*********************************************************************
    // As a part of the copyright agreement this section must be left intact
    //*********************************************************************
    print("The Realms of Hell (RoH v" VERSION ")\n\tBased on Mordor by Brett Vickers, Brooke Paul.\n");
    print("Programmed by: Jason Mitchell, Randi Mitchell and Tim Callahan.\n");
    print("Contributions by: Jordan Carr, Jonathan Hseu.");


    viewFile(LOGIN_FILE);

    if (dnsDone)
        checkLockOut();
    flush();
}

//********************************************************************
//                      askFor
//********************************************************************
const char EOR_STR[] = {(char) IAC, (char) EOR, '\0' };
const char GA_STR[] = {(char) IAC, (char) GA, '\0' };

void Socket::askFor(const char *str) {
    ASSERTLOG( str);
    if (eorEnabled()) {
        printColor(str);
        print(EOR_STR);
    } else {
        printColor(str);
        print(GA_STR);
    }
}

unsigned const char mssp_val[] = { MSSP_VAL, '\0' };
unsigned const char mssp_var[] = { MSSP_VAR, '\0' };

void addMSSPVar(std::ostringstream& msspStr, std::string_view var) {
    msspStr << mssp_var << var;
}

template<class T>
void addMSSPVal(std::ostringstream& msspStr, T val) {
    msspStr << mssp_val << val;
}

int Socket::sendMSSP() {
    std::clog << "Sending MSSP string\n";

    std::ostringstream msspStr;

    msspStr << telnet::sb_mssp_start;
    addMSSPVar(msspStr, "NAME");
    addMSSPVal<std::string>(msspStr, "The Realms of Hell");

    addMSSPVar(msspStr, "PLAYERS");
    addMSSPVal<int>(msspStr, gServer->getNumPlayers());

    addMSSPVar(msspStr, "UPTIME");
    addMSSPVal<long>(msspStr, StartTime);

    addMSSPVar(msspStr, "HOSTNAME");
    addMSSPVal<std::string>(msspStr, "mud.rohonline.net");

    addMSSPVar(msspStr, "PORT");
    addMSSPVal<std::string>(msspStr, "23");
    addMSSPVal<std::string>(msspStr, "3333");

    addMSSPVar(msspStr, "CODEBASE");
    addMSSPVal<std::string>(msspStr, "RoH beta v" VERSION);

    addMSSPVar(msspStr, "VERSION");
    addMSSPVal<std::string>(msspStr, "RoH beta v" VERSION);

    addMSSPVar(msspStr, "CREATED");
    addMSSPVal<std::string>(msspStr, "1998");

    addMSSPVar(msspStr, "LANGUAGE");
    addMSSPVal<std::string>(msspStr, "English");

    addMSSPVar(msspStr, "LOCATION");
    addMSSPVal<std::string>(msspStr, "United States");

    addMSSPVar(msspStr, "WEBSITE");
    addMSSPVal<std::string>(msspStr, "http://www.rohonline.net");

    addMSSPVar(msspStr, "FAMILY");
    addMSSPVal<std::string>(msspStr, "Mordor");

    addMSSPVar(msspStr, "GENRE");
    addMSSPVal<std::string>(msspStr, "Fantasy");

    addMSSPVar(msspStr, "GAMEPLAY");
    addMSSPVal<std::string>(msspStr, "Roleplaying");
    addMSSPVal<std::string>(msspStr, "Hack and Slash");
    addMSSPVal<std::string>(msspStr, "Adventure");

    addMSSPVar(msspStr, "STATUS");
    addMSSPVal<std::string>(msspStr, "Live");

    addMSSPVar(msspStr, "GAMESYSTEM");
    addMSSPVal<std::string>(msspStr, "Custom");

    addMSSPVar(msspStr, "AREAS");
    addMSSPVal<int>(msspStr, -1);

    addMSSPVar(msspStr, "HELPFILES");
    addMSSPVal<int>(msspStr, 1000);

    addMSSPVar(msspStr, "MOBILES");
    addMSSPVal<int>(msspStr, 5100);

    addMSSPVar(msspStr, "OBJECTS");
    addMSSPVal<int>(msspStr, 7500);

    addMSSPVar(msspStr, "ROOMS");
    addMSSPVal<int>(msspStr, 15000);

    addMSSPVar(msspStr, "CLASSES");
    addMSSPVal<size_t>(msspStr, gConfig->classes.size());

    addMSSPVar(msspStr, "LEVELS");
    addMSSPVal<int>(msspStr, MAXALVL);

    addMSSPVar(msspStr, "RACES");
    addMSSPVal<int>(msspStr, gConfig->getPlayableRaceCount());

    addMSSPVar(msspStr, "SKILLS");
    addMSSPVal<size_t>(msspStr, gConfig->skills.size());

    addMSSPVar(msspStr, "GMCP");
    addMSSPVal<std::string>(msspStr, "0");

    addMSSPVar(msspStr, "ATCP");
    addMSSPVal<std::string>(msspStr, "0");

    addMSSPVar(msspStr, "SSL");
    addMSSPVal<std::string>(msspStr, "0");

    addMSSPVar(msspStr, "ZMP");
    addMSSPVal<std::string>(msspStr, "0");

    addMSSPVar(msspStr, "PUEBLO");
    addMSSPVal<std::string>(msspStr, "0");

    addMSSPVar(msspStr, "MSDP");
    addMSSPVal<std::string>(msspStr, "1");

    addMSSPVar(msspStr, "MSP");
    addMSSPVal<std::string>(msspStr, "1");

    // TODO: UTF-8: Change to 1
    addMSSPVar(msspStr, "UTF-8");
    addMSSPVal<std::string>(msspStr, "0");

    addMSSPVar(msspStr, "VT100");
    addMSSPVal<std::string>(msspStr, "0");

    // TODO: XTERM 256: Change to 1
    addMSSPVar(msspStr, "XTERM 256 COLORS");
    addMSSPVal<std::string>(msspStr, "0");

    addMSSPVar(msspStr, "ANSI");
    addMSSPVal<std::string>(msspStr, "1");

    addMSSPVar(msspStr, "MCCP");
    addMSSPVal<std::string>(msspStr, "1");

    addMSSPVar(msspStr, "MXP");
    addMSSPVal<std::string>(msspStr, "1");

    addMSSPVar(msspStr, "PAY TO PLAY");
    addMSSPVal<std::string>(msspStr, "0");

    addMSSPVar(msspStr, "PAY FOR PERKS");
    addMSSPVal<std::string>(msspStr, "0");

    addMSSPVar(msspStr, "HIRING BUILDERS");
    addMSSPVal<std::string>(msspStr, "1");

    addMSSPVar(msspStr, "HIRING CODERS");
    addMSSPVal<std::string>(msspStr, "1");

    addMSSPVar(msspStr, "MULTICLASSING");
    addMSSPVal<std::string>(msspStr, "1");

    addMSSPVar(msspStr, "NEWBIE FRIENDLY");
    addMSSPVal<std::string>(msspStr, "1");

    addMSSPVar(msspStr, "PLAYER CLANS");
    addMSSPVal<std::string>(msspStr, "0");

    addMSSPVar(msspStr, "PLAYER CRAFTING");
    addMSSPVal<std::string>(msspStr, "1");

    addMSSPVar(msspStr, "PLAYER GUILDS");
    addMSSPVal<std::string>(msspStr, "1");

    addMSSPVar(msspStr, "EQUIPMENT SYSTEM");
    addMSSPVal<std::string>(msspStr, "Both");

    addMSSPVar(msspStr, "MULTIPLAYING");
    addMSSPVal<std::string>(msspStr, "Restricted");

    addMSSPVar(msspStr, "PLAYERKILLING");
    addMSSPVal<std::string>(msspStr, "Restricted");

    addMSSPVar(msspStr, "QUEST SYSTEM");
    addMSSPVal<std::string>(msspStr, "Integrated");

    addMSSPVar(msspStr, "ROLEPLAYING");
    addMSSPVal<std::string>(msspStr, "Encouraged");

    addMSSPVar(msspStr, "TRAINING SYSTEM");
    addMSSPVal<std::string>(msspStr, "Both");

    addMSSPVar(msspStr, "WORLD ORIGINALITY");
    addMSSPVal<std::string>(msspStr, "All Original");

    msspStr << telnet::sb_mssp_end;

    return (write(msspStr.str()));
}

int Socket::getNumSockets() {
    return numSockets;
}


//*********************************************************************
//                      viewFile
//*********************************************************************
// This function views a file whose name is given by the third
// parameter. If the file is longer than 20 lines, then the user is
// prompted to hit return to continue, thus dividing the output into
// several pages.

void Socket::viewFile(const std::string& str, bool shouldPage) {
    std::ifstream file(str);
    if(!file.is_open()) {
        bprint("File could not be opened.\n");
        return;
    }
    std::string line;
    while(std::getline(file, line)) {
        if(shouldPage)
            printPaged(line);
        else
            bprint(fmt::format("{}\n", line));
    }

    if(shouldPage)
        donePaging();

}


//*********************************************************************
//                      viewFileReverseReal
//*********************************************************************
// displays a file, line by line starting with the last
// similar to unix 'tac' command

void Socket::viewFileReverseReal(const std::string& str) {
    off_t oldpos;
    off_t newpos;
    off_t temppos;
    int i,more_file=1,count,amount=1621;
    char string[1622];
    char search[80];
    long offset;
    FILE *ff;
    int TACBUF = ( (81 * 20 * sizeof(char)) + 1 );

    if(strlen(tempstr[3]) > 0)
        strcpy(search, tempstr[3]);
    else
        strcpy(search, "\0");

    switch(getParam()) {
        case 1:
            strcpy(tempstr[1], str.c_str());
            if((ff = fopen(str.c_str(), "r")) == nullptr) {
                print("error opening file\n");
                restoreState();
                return;
            }

            fseek(ff, 0L, SEEK_END);
            oldpos = ftell(ff);
            if(oldpos < 1) {
                print("Error opening file\n");
                restoreState();
                return;
            }
            break;

        case 2:
            if(str[0] != 0) {
                print("Aborted.\n");
                getPlayer()->clearFlag(P_READING_FILE);
                restoreState();
                return;
            }

            if((ff = fopen(tempstr[1], "r")) == nullptr) {
                print("error opening file\n");
                getPlayer()->clearFlag(P_READING_FILE);
                restoreState();
                return;
            }

            offset = atol(tempstr[2]);
            fseek(ff, offset, SEEK_SET);
            oldpos = ftell(ff);
            if(oldpos < 1) {
                print("Error opening file\n");
                restoreState();
                return;
            }

    }

    nomatch:
    temppos = oldpos - TACBUF;
    if(temppos > 0)
        fseek(ff, temppos, SEEK_SET);
    else {
        fseek(ff, 0L, SEEK_SET);
        amount = oldpos;
    }

    newpos = ftell(ff);


    fread(string, amount,1, ff);
    string[amount] = '\0';
    i = strlen(string);
    i--;

    count = 0;
    while(count < 21 && i > 0) {
        if(string[i] == '\n') {
            if( (   strlen(search) > 0 && strstr(&string[i], search)) || search[0] == '\0') {
                printColor("%s", &string[i]);
                count++;
            }
            string[i]='\0';
            if(string[i-1] == '\r')
                string[i-1]='\0';
        }
        i--;
    }

    oldpos = newpos + i + 2;
    if(oldpos < 3)
        more_file = 0;

    sprintf(tempstr[2], "%ld", (long) oldpos);


    if(more_file && count == 0)
        goto nomatch;       // didnt find a match within a screenful
    else if(more_file) {
        askFor("\n[Hit Return, Q to Quit]: ");
        gServer->processOutput();
        intrpt &= ~1;

        fclose(ff);
        getPlayer()->setFlag(P_READING_FILE);
        setState(CON_VIEWING_FILE_REVERSE, 2);
        return;
    } else {
        if((strlen(search) > 0 && strstr(string, search)) || search[0] == '\0') {
            print("\n%s\n", string);
        }
        fclose(ff);
        getPlayer()->clearFlag(P_READING_FILE);
        restoreState();
        return;
    }

}

// Wrapper for viewFileReverse_real that properly sets the connected state
void Socket::viewFileReverse(const std::string& str) {
    if(getState() != CON_VIEWING_FILE_REVERSE)
        setState(CON_VIEWING_FILE_REVERSE);
    viewFileReverseReal(str);
}

bool Socket::hasPagerOutput() {
    return(!pagerOutput.empty());
}


