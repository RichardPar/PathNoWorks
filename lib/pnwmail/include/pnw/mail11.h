// pnw/mail11.h -- DECnet mail (Mail-11), both ways.
//
// Mail-11 is how VMS MAIL and RSX MAIL send a message to NODE::USER.  It
// runs on a logical link to object 27 (MAIL), one record per DECnet
// message:
//
//     sender -> the sender's user name
//     sender -> a recipient's user name      <- a status for it
//     ...                                       (01 00 00 00: known)
//     sender -> NUL                             (no more recipients)
//     sender -> the To: line, as typed
//     sender -> the CC: line (*)
//     sender -> the subject
//     sender -> one more record, empty from VMS (*)
//     sender -> the body, one line a record
//     sender -> NUL                          <- a status per recipient
//
// (*) Only from a sender whose connect data sets option flags, as VMS
// does; VMS then sends a CC: line even if empty.  pnw-mail sends no
// connect data and neither record, and VMS takes that.  The sender closes
// the link once it has the statuses.
//
// Written from dnprogs' sendvmsmail and vmsmaild as a description, and
// checked against VMS.

#ifndef PNW_MAIL11_H
#define PNW_MAIL11_H

#include "pnw/api.h"

#include <functional>
#include <string>
#include <vector>

namespace pnw {

struct MailMessage {
    std::string              from;          // "PNW::RICHARD", or as VMS gives it
    std::string              node;          // the node it came from
    std::vector<std::string> recipients;    // user names at the receiving node
    std::string              to;            // the To: line
    std::string              cc;            // the CC: line, if any
    std::string              subject;
    std::vector<std::string> body;          // lines
};

// What happened to one recipient: delivered, or the far end's reason not.
struct MailResult {
    std::string user;
    bool        ok = false;
    std::string error;
};

// Send a message to users on one node.  from is our user name.  A node
// with no default DECnet account takes mail only with a login: login gives
// it (user, password), and the far end's mail server runs as that user.
// Throws ApiError or Rejected if the node cannot be reached.
struct MailLogin {
    std::string user, password;
};
std::vector<MailResult> mail11_send (Api &api, const std::string &node,
                                     const MailLogin &login,
                                     const std::string &from,
                                     const std::vector<std::string> &users,
                                     const std::string &to_line,
                                     const std::string &subject,
                                     const std::vector<std::string> &body,
                                     bool trace = false);

// Receive one message on an accepted link.  deliver is asked to store it
// and says whether it did; each recipient then gets that answer.
// Returns false if the sender went away part way.
bool mail11_receive (Link &link, const Incoming &in,
                     const std::function<bool (const MailMessage &)> &deliver,
                     bool trace = false);

// The accept data a mail receiver sends: which Mail-11 features it has.
Bytes mail11_accept_data ();

// Append a message to an mbox file, locked, with "From " lines quoted.
// Throws ApiError if the file cannot be written.
void append_mbox (const std::string &path, const MailMessage &m);

}   // namespace pnw

#endif  // PNW_MAIL11_H
