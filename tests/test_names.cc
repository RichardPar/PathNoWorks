// Unit tests for pnw/names.h.  No framework: each CHECK prints what failed
// and the exit status says whether anything did.

#include "pnw/names.h"

#include <iostream>

namespace {

int failures = 0;

#define CHECK_EQ(a, b)                                                       \
    do {                                                                     \
        auto a_ = (a);                                                       \
        auto b_ = (b);                                                       \
        if (!(a_ == b_)) {                                                   \
            std::cerr << __FILE__ << ":" << __LINE__ << ": " #a " is \""     \
                      << a_ << "\", expected \"" << b_ << "\"\n";            \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

#define CHECK(x)                                                             \
    do {                                                                     \
        if (!(x)) {                                                          \
            std::cerr << __FILE__ << ":" << __LINE__ << ": " #x "\n";        \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

void vms_trees ()
{
    pnw::RemoteTree t ("DUA0:[USER]");
    CHECK (t.vms ());
    CHECK_EQ (t.spec ("/"), std::string ("DUA0:[USER]"));
    CHECK_EQ (t.spec ("/LOGIN.COM"), std::string ("DUA0:[USER]LOGIN.COM"));
    CHECK_EQ (t.spec ("/sub/deep/x.txt"), std::string ("DUA0:[USER.SUB.DEEP]x.txt"));
    CHECK_EQ (t.listing ("/"), std::string ("DUA0:[USER]*.*;0"));
    CHECK_EQ (t.listing ("/sub"), std::string ("DUA0:[USER.SUB]*.*;0"));

    // No base directory: the default one, and relative subdirectories.
    pnw::RemoteTree d ("[]");
    CHECK (d.vms ());
    CHECK_EQ (d.spec ("/A.TXT"), std::string ("A.TXT"));
    CHECK_EQ (d.spec ("/sub/A.TXT"), std::string ("[.SUB]A.TXT"));

    pnw::RemoteTree dotted ("[USER.PROJ]");
    CHECK_EQ (dotted.listing ("/x"), std::string ("[USER.PROJ.X]*.*;0"));
}

void unix_trees ()
{
    pnw::RemoteTree t ("");
    CHECK (!t.vms ());
    CHECK_EQ (t.spec ("/"), std::string (""));
    CHECK_EQ (t.spec ("/a/b.txt"), std::string ("a/b.txt"));
    CHECK_EQ (t.listing ("/"), std::string ("*"));
    CHECK_EQ (t.listing ("/a/b"), std::string ("a/b/*"));

    pnw::RemoteTree s ("pub");
    CHECK_EQ (s.spec ("/x"), std::string ("pub/x"));
    CHECK_EQ (s.listing ("/"), std::string ("pub/*"));
}

void names ()
{
    auto n = pnw::local_name ("LOGIN.COM;3", true);
    CHECK (n.has_value ());
    CHECK_EQ (n->name, std::string ("LOGIN.COM"));
    CHECK (!n->directory);

    n = pnw::local_name ("MAIL.DIR;1", true);
    CHECK_EQ (n->name, std::string ("MAIL"));
    CHECK (n->directory);

    n = pnw::local_name ("README.;1", true);
    CHECK_EQ (n->name, std::string ("README"));

    n = pnw::local_name ("DUA0:[USER]X.TXT;2", true);
    CHECK_EQ (n->name, std::string ("X.TXT"));

    n = pnw::local_name ("sub/", false);
    CHECK_EQ (n->name, std::string ("sub"));
    CHECK (n->directory);

    // A Unix FAL's ".dir" is just a name.
    n = pnw::local_name ("notes.dir", false);
    CHECK_EQ (n->name, std::string ("notes.dir"));
    CHECK (!n->directory);

    CHECK (!pnw::local_name ("", false));
    CHECK (!pnw::local_name ("../x", false));
}

void dates ()
{
    auto t = pnw::parse_dap_date ("30-SEP-26 13:24:26");
    CHECK (t.has_value ());
    std::tm tm {};
    ::localtime_r (&*t, &tm);
    CHECK_EQ (tm.tm_year, 126);
    CHECK_EQ (tm.tm_mon, 8);
    CHECK_EQ (tm.tm_mday, 30);
    CHECK_EQ (tm.tm_hour, 13);

    auto old = pnw::parse_dap_date ("01-JAN-85 00:00:00");
    ::localtime_r (&*old, &tm);
    CHECK_EQ (tm.tm_year, 85);

    CHECK (!pnw::parse_dap_date ("yesterday"));
    CHECK (!pnw::parse_dap_date ("30-XYZ-26 13:24:26"));
}

}   // namespace

int main ()
{
    vms_trees ();
    unix_trees ();
    names ();
    dates ();
    if (failures) {
        std::cerr << failures << " failed\n";
        return 1;
    }
    std::cout << "all passed\n";
    return 0;
}
