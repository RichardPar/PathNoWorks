// pnwnice/nodes.cc -- the nodes a DECnet node knows, from NICE.

#include "pnw/nodes.h"

#include "decnet/nice/nml.h"
#include "decnet/nice/packets.h"

#include <cctype>

namespace pnw {

namespace nm = decnet::nice;

namespace {

// Node parameters, as NICE numbers them.
enum : std::uint16_t {
    p_state = 0, p_cost = 820, p_hops = 821, p_circuit = 822, p_next = 830
};

std::string text_of (const nm::ParamList &params, std::uint16_t number)
{
    const nm::Param *p = params.find (number);
    if (!p) return {};
    for (const nm::ParamDef &d : nm::node_params ())
        if (d.number == number && !d.counter) return p->value.format (d.labels);
    return p->value.format ();
}

std::optional<unsigned> number_of (const nm::ParamList &params,
                                   std::uint16_t number)
{
    const nm::Param *p = params.find (number);
    if (!p || !p->value.is_number ()) return std::nullopt;
    return static_cast<unsigned> (p->value.as_uint ());
}

}   // namespace

std::vector<NodeRow> known_nodes (Api &api, const std::string &target)
{
    nm::NiceRequest r;
    r.function = nm::fn_read;
    r.entity_type = nm::Entity::node;
    r.entity = nm::ReqEntity::make_wild (nm::Entity::node, nm::ReqEntity::known);
    r.info = nm::info_status;

    ConnectOptions o;
    o.dest = target.empty () ? api.system () : target;
    o.object = "19";
    o.data = Bytes (std::begin (nm::nice_version), std::end (nm::nice_version));
    auto link = api.connect (o);
    link->send (r.encode ());

    std::vector<NodeRow> rows;
    bool multiple = false;
    for (;;) {
        auto msg = link->recv ();
        if (!msg) throw ApiError ("no reply from " + o.dest);
        nm::NiceReply hdr = nm::NiceReply::parse_header (*msg);
        if (hdr.retcode == nm::rc_multiple) { multiple = true; continue; }
        if (hdr.retcode == nm::rc_done) break;
        if (hdr.retcode < 0) {
            std::string text = nm::retcode_text (hdr.retcode)
                ? nm::retcode_text (hdr.retcode)
                : "error " + std::to_string (hdr.retcode);
            throw ApiError (o.dest + ": " + text);
        }
        nm::NiceReply reply = nm::NiceReply::parse (*msg, nm::Entity::node);
        if (reply.has_entity && reply.entity.kind () == nm::Entity::node) {
            const nm::NiceNode &n = reply.entity.as_node ();
            NodeRow row;
            row.address = n.id.str ();
            row.name = n.name;
            row.executor = n.executor;
            row.state = text_of (reply.params, p_state);
            for (char &c : row.state)
                c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
            row.circuit = text_of (reply.params, p_circuit);
            row.next = text_of (reply.params, p_next);
            row.hops = number_of (reply.params, p_hops);
            row.cost = number_of (reply.params, p_cost);
            rows.push_back (std::move (row));
        }
        if (!multiple) break;
    }
    link->disconnect ();
    return rows;
}

}   // namespace pnw
