// pnw-mail -- DECnet mail (Mail-11): send it, and take it in.
//
//     echo "Hello" | pnw-mail send -s "Greetings" VAXXY::SYSTEM
//     pnw-mail send BAJI::RICHARD,VAXXY::SYSTEM < letter.txt
//     pnw-mail listen                  # mail to PNW::anyone -> ~/Mail/decnet
//
// Sending goes as your login name, as VMS MAIL does: the far end sees it
// from THISNODE::YOU.  Listening takes object 27 (MAIL) on the node
// decnetd runs, and appends every message to one mbox file, whoever at
// this node it is for.

#include "pnw/mail11.h"

#include <cstdlib>
#include <ctime>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

std::string upper (std::string s)
{
    for (char &c : s) c = static_cast<char> (std::toupper (static_cast<unsigned char> (c)));
    return s;
}

std::string login_name ()
{
    if (const passwd *pw = ::getpwuid (::geteuid ())) return upper (pw->pw_name);
    return "USER";
}

std::vector<std::string> split (const std::string &s, char sep)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { if (!cur.empty ()) out.push_back (cur); cur.clear (); }
        else cur += c;
    }
    if (!cur.empty ()) out.push_back (cur);
    return out;
}

void usage ()
{
    std::cerr <<
        "usage: pnw-mail [options] send [-s subject] NODE::USER[,NODE::USER...]\n"
        "       (NODE\"user password\"::USER for a node that wants a login)\n"
        "       pnw-mail [options] listen [-o mbox]\n"
        "  send     mail standard input to users on DECnet nodes\n"
        "  listen   take mail for this node and append it to an mbox\n"
        "           (default ~/Mail/decnet)\n"
        "  --socket s  decnetd API socket (default $DECNETAPI or "
        "/tmp/decnetapi.sock)\n"
        "  --trace     show each Mail-11 record\n";
}

int send (pnw::Api &api, const std::string &subject,
          const std::string &to, bool trace)
{
    // Recipients, grouped by node (and the login to give it, if any):
    // NODE::USER or NODE"user password"::USER.
    std::map<std::string, std::vector<std::string>> by_node;
    std::map<std::string, pnw::MailLogin> logins;
    for (const std::string &r : split (to, ',')) {
        auto sep = r.find ("::");
        if (sep == std::string::npos || sep == 0 || sep + 2 >= r.size ()) {
            std::cerr << "pnw-mail: " << r << " is not NODE::USER\n";
            return 2;
        }
        std::string node = r.substr (0, sep);
        pnw::MailLogin login;
        if (auto q = node.find ('"'); q != std::string::npos) {
            auto words = split (node.substr (q + 1, node.rfind ('"') - q - 1), ' ');
            if (words.size () > 0) login.user = words[0];
            if (words.size () > 1) login.password = words[1];
            node.erase (q);
        }
        node = upper (node);
        if (!login.user.empty ()) logins[node] = login;
        by_node[node].push_back (upper (r.substr (sep + 2)));
    }
    std::vector<std::string> body;
    for (std::string line; std::getline (std::cin, line);) {
        if (!line.empty () && line.back () == '\r') line.pop_back ();
        body.push_back (line);
    }

    int failed = 0;
    for (const auto &[node, users] : by_node) {
        // The To: line lists the recipients as the sender addressed them.
        std::string line;
        for (const std::string &u : users) line += (line.empty () ? "" : ",") + node + "::" + u;
        try {
            for (const pnw::MailResult &r :
                 pnw::mail11_send (api, node, logins[node], login_name (), users, line,
                                   subject, body, trace)) {
                if (r.ok) std::cerr << "%PNW-S-SENT, to " << node << "::" << r.user << "\n";
                else {
                    ++failed;
                    std::cerr << "%PNW-E-NOTSENT, to " << node << "::" << r.user
                              << ": " << r.error << "\n";
                }
            }
        } catch (const std::exception &e) {
            failed += static_cast<int> (users.size ());
            std::cerr << "%PNW-E-NOTSENT, to " << node << ": " << e.what () << "\n";
        }
    }
    return failed ? 1 : 0;
}

int listen (pnw::Api &api, std::string mbox, bool trace)
{
    if (mbox.empty ()) {
        const char *home = std::getenv ("HOME");
        std::string dir = std::string (home ? home : ".") + "/Mail";
        ::mkdir (dir.c_str (), 0700);
        mbox = dir + "/decnet";
    }
    api.bind (27, "MAIL");
    std::cerr << "%PNW-I-LISTENING, for DECnet mail to " << api.system ()
              << "::, into " << mbox << "\n";
    for (;;) {
        auto in = api.incoming (std::chrono::hours (24));
        if (!in) continue;
        auto link = api.accept (*in, pnw::mail11_accept_data ());
        std::string error;
        bool ok = pnw::mail11_receive (*link, *in, [&] (const pnw::MailMessage &m) {
            try {
                pnw::append_mbox (mbox, m);
                std::time_t now = std::time (nullptr);
                char when[32];
                std::strftime (when, sizeof when, "%d-%b %H:%M", std::localtime (&now));
                std::cerr << when << "  from " << m.from << "  \"" << m.subject
                          << "\"  (" << m.body.size () << " lines)\n";
                return true;
            } catch (const std::exception &e) {
                error = e.what ();
                return false;
            }
        }, trace);
        if (!error.empty ()) std::cerr << "pnw-mail: " << error << "\n";
        if (!ok) std::cerr << "pnw-mail: a message from " << in->node
                           << " was cut short\n";
        if (link->open ()) link->disconnect ();
    }
}

}   // namespace

int main (int argc, char **argv)
{
    std::string sock = pnw::Api::default_path (), subject, mbox, cmd;
    bool trace = false;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--socket" && i + 1 < argc)  sock = argv[++i];
        else if (a == "--trace")              trace = true;
        else if (a == "-h" || a == "--help")  { usage (); return 0; }
        else if (cmd.empty ())                cmd = a;
        else if (a == "-s" && i + 1 < argc && cmd == "send") subject = argv[++i];
        else if (a == "-o" && i + 1 < argc && cmd == "listen") mbox = argv[++i];
        else                                  args.push_back (a);
    }
    try {
        if (cmd == "send" && args.size () == 1) {
            pnw::Api api (sock);
            return send (api, subject, args[0], trace);
        }
        if (cmd == "listen" && args.empty ()) {
            pnw::Api api (sock);
            return listen (api, mbox, trace);
        }
    } catch (const std::exception &e) {
        std::cerr << "pnw-mail: " << e.what () << "\n";
        return 1;
    }
    usage ();
    return 2;
}
