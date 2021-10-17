/*
 * socket.h
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
 *  Copyright (C) 2007-2020 Jason Mitchell, Randi Mitchell
 *     Contributions by Tim Callahan, Jonathan Hseu
 *  Based on Mordor (C) Brooke Paul, Brett J. Vickers, John P. Freeman
 *
 */

#ifndef SOCKET_H_
#define SOCKET_H_

// C Includes
#include <zlib.h>
#include <netinet/in.h>

// C++ Includes
#include <list>
#include <map>
#include <queue>
#include <vector>
#include <string>
#include <fmt/format.h>


// Defines needed
#define TELOPTS
#define TELCMDS
#define NAWS TELOPT_NAWS
#define TTYPE TELOPT_TTYPE
#define MSDP TELOPT_MSDP
#define CHARSET TELOPT_CHARSET

extern long InBytes;
extern long UnCompressedBytes;
extern long OutBytes;

class Player;
class ReportedMsdpVariable;

namespace telnet {
    #define TELOPT_CHARSET      42
    #define TELOPT_MSDP         69
    #define TELOPT_MSSP         70
    #define TELOPT_COMPRESS     85
    #define TELOPT_COMPRESS2    86
    #define TELOPT_MSP          90
    #define TELOPT_MXP          91
    #define TELOPT_GMCP         201

    #define SEND                1
    #define ACCEPTED            2
    #define REJECTED            3

    #define MSSP_VAR            1
    #define MSSP_VAL            2

    #define MSDP_VAR            1
    #define MSDP_VAL            2
    #define MSDP_TABLE_OPEN     3
    #define MSDP_TABLE_CLOSE    4
    #define MSDP_ARRAY_OPEN     5
    #define MSDP_ARRAY_CLOSE    6
    #define MAX_MSDP_SIZE       100

    #define UNICODE_MALE        9794
    #define UNICODE_FEMALE      9792
    #define UNICODE_NEUTER      9791

    #define MXP_BEG             "\x16"
    #define CH_MXP_BEG          '\x16'
    #define MXP_END             "\x04"
    #define CH_MXP_END          '\x04'
    #define MXP_AMP             "\x06"
    #define CH_MXP_AMP          '\x06'

    #define MXP_SECURE_OPEN "\033[1z"
    #define MXP_LOCK_CLOSE "\033[7z"


    extern unsigned const char will_msdp[];     // Mud Server Data Protocol support
    extern unsigned const char wont_msdp[];     // Stop MSDP support

    extern unsigned const char will_msdp[];     // Generic Mud Communication Protocol
    extern unsigned const char wont_msdp[];     // Stop GMCP support

    extern unsigned const char will_mxp[];      // MXP Support
    extern unsigned const char start_mxp[];     // Start MPX string

    extern unsigned const char will_comp2[];    // MCCP V2 support
    extern unsigned const char will_comp1[];    // MCCP V1 support
    extern unsigned const char start_mccp[];    // Start compress
    extern unsigned const char start_mccp2[];   // Start compress2

    extern unsigned const char will_echo[];     // Echo input

    extern unsigned const char will_eor[];      // EOR After every prompt

    extern unsigned const char will_mssp[];     // MSSP Support
    extern unsigned const char sb_mssp_start[]; // Start MSSP String
    extern unsigned const char sb_mssp_end[];   // End MSSP String

    extern unsigned const char do_ttype[];      // Terminal type negotation
    extern unsigned const char wont_ttype[];    // Terminal type negotation
    extern unsigned const char query_ttype[];   // Begin terminal type subnegotiations

    extern unsigned const char do_naws[];       // Window size negotation NAWS

    extern unsigned const char do_charset[];    // Window size negotation NAWS
    extern unsigned const char charset_utf8[];  // Negotiate UTF-8


    extern unsigned const char eor_str[];


    // For MCCP
    void *zlib_alloc(void *opaque, unsigned int items, unsigned int size);
    void zlib_free(void *opaque, void *address);
}

class Socket {
    friend class Server;
    struct Host {
        bstring hostName;
        bstring ip;
    };
    struct Term {
        int rows;
        int cols;
        bstring type;
        bstring firstType;
        bstring lastType;
        bstring version;
    };
    struct SockOptions {
        bool            dumb; // Dumb client, don't do telnet negotiations
        int             color;
        bool            xterm256;
        int             mccp;
        bool            mxp;
        bool            mxpClientSecure;
        unsigned char   lastColor;
        bool            msdp;
        bool            eor;
        bool            msp;
        bool            compressing;
        bool            naws;
        bool            charset;
        bool            utf8;
    };

private:
    static int numSockets;

public:
    // Static Methods
    static void resolveIp(const sockaddr_in &addr, bstring& ip);
    static bstring stripTelnet(std::string_view inStr);
    static bool needsPrompt(std::string_view inStr);
    void viewFile(const bstring& str, bool shouldPage=false);
    void viewFileReverse(const bstring& str);
    void viewFileReverseReal(const bstring& str);
public:
    explicit Socket(int pFd);
    Socket(int pFd, sockaddr_in pAddr, bool dnsDone);
    ~Socket();

    void cleanUp();
    void reset();

    void startTelnetNeg();
    void continueTelnetNeg(bool queryTType);
    void setState(int pState, char pFnParam = 1);
    void restoreState();
    void addToPlayerList();

    void finishLogin();


    ssize_t write(std::string_view toWrite, bool pSpy = true, bool process = true);
    void askFor(const char *str);

    void vprint(const char *fmt, va_list ap);

    void bprint(std::string_view toPrint);
    void bprintPython(const bstring& toPrint);

    template <typename... Args>
    void bprint(std::string_view toPrint, Args &&... args) const {
        return bprint(fmt::format(toPrint, std::forward<Args>(args)...));
    }

    void printPaged(std::string_view toPrint);
    void appendPaged(std::string_view toPrint);

    template <typename... Args>
    void printPaged(std::string_view toPrint, Args &&... args) const {
        return printPaged(fmt::format(toPrint, std::forward<Args>(args)...));
    }
    void println(std::string_view toPrint = "");
    void print(const char* format, ...);
    void printColor(const char* format, ...);

    bstring parseForOutput(std::string_view outBuf);
    bstring getColorCode(unsigned char ch);

    int processInput();
    int processOneCommand();

    void reconnect(bool pauseScreen=false);
    void disconnect();
    void showLoginScreen(bool dnsDone=true);

    void flush(); // Flush any pending output


    int startCompress(bool silent = false);
    int endCompress();

    int sendMSSP(); // Send MSSP Variables

    bool saveTelopts(xmlNodePtr rootNode);
    bool loadTelopts(xmlNodePtr rootNode);

// End Telopt related

    [[nodiscard]] int getFd() const;
    [[nodiscard]] bool isConnected() const;
    [[nodiscard]] int getState() const;
    [[nodiscard]] std::string_view getIp() const;
    [[nodiscard]] std::string_view getHostname() const;

    void checkLockOut();

    void setHostname(std::string_view pName);
    void setIp(std::string_view pIp);

    [[nodiscard]] bool hasOutput() const;
    [[nodiscard]] bool hasCommand() const;

    [[nodiscard]] long getIdle() const;
    [[nodiscard]] int mccpEnabled() const;
    [[nodiscard]] bool mxpEnabled() const;
    [[nodiscard]] bool getMxpClientSecure() const;
    [[nodiscard]] bool msdpEnabled() const;
    [[nodiscard]] bool canForce() const;
    [[nodiscard]] bool eorEnabled() const;
    [[nodiscard]] bool isDumbClient() const;
    [[nodiscard]] bool mspEnabled() const;
    [[nodiscard]] bool nawsEnabled() const;
    [[nodiscard]] bool charsetEnabled() const;
    [[nodiscard]] bool utf8Enabled() const;

    [[nodiscard]] bstring getTermType() const;
    [[nodiscard]] int getColorOpt() const;
    [[nodiscard]] int getTermCols() const;
    [[nodiscard]] int getTermRows() const;

    void setColorOpt(int opt);

    [[nodiscard]] bool hasPlayer() const;
    [[nodiscard]] Player* getPlayer() const;
    void setPlayer(Player* ply);
    void freePlayer();



    void clearSpying();
    void clearSpiedOn();
    void setSpying(Socket *sock);
    void removeSpy(Socket *sock);
    void addSpy(Socket *sock);

    // MXP Support
    void clearMxpClientSecure();
    void defineMxp();

    // MSDP Support Functions
    ReportedMsdpVariable *getReportedMsdpVariable(std::string_view value);
    bool msdpSendPair(std::string_view variable, std::string_view value);
    void msdpSendList(std::string_view variable, const std::vector<bstring>& values);
    void msdpClearReporting();
    bstring getMsdpReporting();

protected:
    // Telopt related
    bool negotiate(unsigned char ch);
    //bool subNegotiate(unsigned char ch);
    bool handleNaws(int& colRow, unsigned char& chr, bool high);
    size_t processCompressed(); // Mccp

    bool parseMXPSecure();

    // MSDP Support Functions
    bool parseMsdp();
    bool processMsdpVarVal(bstring& variable, bstring& value);
    bool msdpSend(bstring variable);
    bool msdpList(bstring& value);
    ReportedMsdpVariable* msdpReport(bstring& value);
    bool msdpReset(bstring& value);
    bool msdpUnReport(bstring& value);

// TODO - Retool so they can be moved to protected
public:
    char tempstr[4][256]{};
    bstring tempbstr;

    int getParam();
    void setParam(int newParam);

protected:
    int         fd;                 // File Descriptor of this socket
    Host        host;
    Term        term;
    SockOptions opts{};
    bool inPlayerList{};

    int         lastState{};
    int         connState{};

    int         tState{};
    bool        oneIAC{};
    bool        watchBrokenClient{};

    bstring     output;
    bstring     processedOutput;   // Output that has been processed but not fully sent (in the case of EWOULDBLOCK for example)

    std::queue<bstring> input;      // Processed Input buffer

    // IAC buffer, we make it a vector so that it will handle NUL bytes and other characters and still report the correct size()/length()
    std::vector<unsigned char>  cmdInBuf;
    bstring     inBuf;              // Input Buffer
    bstring     inLast;             // Last command

    Player*     myPlayer{};


// For MCCP
    char        *outCompressBuf{};
    z_stream    *outCompress{};

// Old items from IOBUF that we might keep
    void        (*fn)(Socket*, const bstring&){};
    char        fnparam{};
    char        commands{};
    Socket      *spyingOn{};      // Socket we are spying on
    std::list<Socket*> spying;  // Sockets spying on us
    std::map<bstring, ReportedMsdpVariable*> msdpReporting;
// TEMP
public:
    long        ltime{};
    char        intrpt{};

private:
    std::deque<std::string> pagerOutput;

public:
    static const int COMPRESSED_OUTBUF_SIZE;

public:
    static int getNumSockets();
    void handlePaging(const bstring &inStr);
    bool hasPagerOutput();
    int getMaxPages() const;
    void donePaging();

private:
    int paged{};
    void sendPages(int numPages);

};


// Other socket related prototypes
int nonBlock(int pFd);


#endif /*SOCKET_H_*/
