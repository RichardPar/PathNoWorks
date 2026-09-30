// pnwdap/names.cc -- between local paths and remote file specifications.

#include "pnw/names.h"

#include <cctype>
#include <cstring>

namespace pnw {

namespace {

std::vector<std::string> split_path (const std::string &p)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : p) {
        if (c == '/') {
            if (!cur.empty ()) out.push_back (cur);
            cur.clear ();
        } else {
            cur += c;
        }
    }
    if (!cur.empty ()) out.push_back (cur);
    return out;
}

std::string upper (std::string s)
{
    for (char &c : s) c = static_cast<char> (std::toupper (static_cast<unsigned char> (c)));
    return s;
}

}   // namespace

RemoteTree::RemoteTree (std::string base) : base_ (std::move (base))
{
    auto lb = base_.find_first_of ("[<");
    vms_ = lb != std::string::npos
        || (base_.find (':') != std::string::npos
            && base_.find ('/') == std::string::npos);
    if (!vms_) {
        if (!base_.empty () && base_.back () != '/') base_ += '/';
        return;
    }
    if (lb == std::string::npos) {
        device_ = base_;                    // "DUA0:" alone
        return;
    }
    device_ = base_.substr (0, lb);
    char close = base_[lb] == '[' ? ']' : '>';
    auto rb = base_.find (close, lb);
    std::string d = base_.substr (lb + 1, rb == std::string::npos
                                              ? std::string::npos
                                              : rb - lb - 1);
    std::string cur;
    for (char c : d) {
        if (c == '.') { base_dirs_.push_back (cur); cur.clear (); }
        else cur += c;
    }
    base_dirs_.push_back (cur);
}

std::string RemoteTree::base_dir (const std::vector<std::string> &dirs) const
{
    if (!vms_) {
        std::string s = base_;
        for (const std::string &d : dirs) s += d + "/";
        return s;
    }
    // [USER] + SUB -> [USER.SUB].  With no base directory, a relative one:
    // [.SUB] is SUB under the default directory.
    std::vector<std::string> all = base_dirs_;
    bool relative = all.size () == 1 && all[0].empty ();
    if (relative) all.clear ();
    for (const std::string &d : dirs) all.push_back (upper (d));
    if (all.empty ()) return device_;
    std::string s = device_ + "[";
    if (relative || base_dirs_.empty ()) s += ".";
    for (std::size_t i = 0; i < all.size (); ++i) {
        if (i) s += ".";
        s += all[i];
    }
    return s + "]";
}

std::string RemoteTree::spec (const std::string &local) const
{
    auto parts = split_path (local);
    if (parts.empty ()) return base_dir ({});
    std::string name = parts.back ();
    parts.pop_back ();
    return base_dir (parts) + name;
}

std::string RemoteTree::listing (const std::string &local_dir) const
{
    // VMS: the highest version of everything.  The Unix FALs take "*".
    return base_dir (split_path (local_dir)) + (vms_ ? "*.*;0" : "*");
}

std::optional<LocalName> local_name (const std::string &remote, bool vms)
{
    std::string n = remote;
    LocalName out;
    if (!n.empty () && n.back () == '/') {
        n.pop_back ();
        out.directory = true;
    }
    if (auto semi = n.find (';'); semi != std::string::npos) n.erase (semi);
    // Some servers send the full specification; the name is after the
    // directory part.
    if (auto cut = n.find_last_of ("]>"); cut != std::string::npos)
        n.erase (0, cut + 1);
    if (vms && n.size () > 4 && upper (n.substr (n.size () - 4)) == ".DIR") {
        n.erase (n.size () - 4);
        out.directory = true;
    }
    // VMS writes a name with no type as "NAME."
    if (vms && n.size () > 1 && n.back () == '.') n.pop_back ();
    if (n.empty () || n == "." || n == ".." || n.find ('/') != std::string::npos)
        return std::nullopt;
    out.name = n;
    return out;
}

std::optional<std::time_t> parse_dap_date (const std::string &s)
{
    static const char *const months[] = {
        "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
    int day, year, h, m, sec;
    char mon[4] = {};
    if (std::sscanf (s.c_str (), "%d-%3s-%d %d:%d:%d", &day, mon, &year, &h,
                     &m, &sec) != 6)
        return std::nullopt;
    std::tm tm {};
    tm.tm_mon = -1;
    for (int i = 0; i < 12; ++i)
        if (upper (mon) == months[i]) tm.tm_mon = i;
    if (tm.tm_mon < 0) return std::nullopt;
    // Two digit years: 70 and later are 1900s, as PyDECnet reads them.
    if (year < 100) year += year < 70 ? 2000 : 1900;
    tm.tm_year = year - 1900;
    tm.tm_mday = day;
    tm.tm_hour = h;
    tm.tm_min = m;
    tm.tm_sec = sec;
    tm.tm_isdst = -1;
    std::time_t t = std::mktime (&tm);
    if (t == static_cast<std::time_t> (-1)) return std::nullopt;
    return t;
}

}   // namespace pnw
