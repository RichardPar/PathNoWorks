// pnw-latsock -- open a LAT socket, then run pnw-lat with it.
//
// The only part of LAT that needs privilege is opening the raw Ethernet
// socket.  This does just that, as small as it can be, and passes the open
// socket to pnw-lat, which runs without the privilege: file capabilities are
// not inherited across exec.  Give it CAP_NET_RAW once:
//
//     sudo setcap cap_net_raw+ep pnw-latsock
//
// pnw-lat runs it by itself when it cannot open the socket.

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <sys/socket.h>
#include <unistd.h>

int main (int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf (stderr, "usage: pnw-latsock pnw-lat [arguments]\n");
        return 2;
    }
    int fd = ::socket (AF_PACKET, SOCK_DGRAM, htons (0x6004));
    if (fd < 0) {
        std::fprintf (stderr, "pnw-latsock: %s%s\n", std::strerror (errno),
                      errno == EPERM ? "; run: sudo setcap cap_net_raw+ep pnw-latsock" : "");
        return 1;
    }
    ::setenv ("PNW_LAT_FD", std::to_string (fd).c_str (), 1);
    ::execv (argv[1], argv + 1);
    std::fprintf (stderr, "pnw-latsock: cannot run %s: %s\n", argv[1], std::strerror (errno));
    return 1;
}
