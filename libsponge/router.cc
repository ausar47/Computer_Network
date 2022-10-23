#include "router.hh"

#include <iostream>

using namespace std;

// Dummy implementation of an IP router

// Given an incoming Internet datagram, the router decides
// (1) which interface to send it out on, and
// (2) what next hop address to send it to.

// For Lab 6, please replace with a real implementation that passes the
// automated checks run by `make check_lab6`.

// You will need to add private members to the class declaration in `router.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

//! \param[in] route_prefix The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
//! \param[in] prefix_length For this route to be applicable, how many high-order (most-significant) bits of the route_prefix will need to match the corresponding bits of the datagram's destination address?
//! \param[in] next_hop The IP address of the next hop. Will be empty if the network is directly attached to the router (in which case, the next hop address should be the datagram's final destination).
//! \param[in] interface_num The index of the interface to send the datagram out on.
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/" << int(prefix_length)
         << " => " << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num << "\n";

    // Your code here.
    _routing_table.push_back({route_prefix, prefix_length, next_hop, interface_num});
}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
    // Your code here.
    const uint32_t dst_addr = dgram.header().dst;
    auto max_matched_entry = _routing_table.end();
    // find the longest-prefix-match route
    for (auto entry_iter = _routing_table.begin(); entry_iter != _routing_table.end(); entry_iter++) {
        if (entry_iter->route_prefix == 0 || (entry_iter->route_prefix ^ dst_addr) >> (32 - entry_iter->prefix_length) == 0) {
            if (max_matched_entry->prefix_length < entry_iter->prefix_length || max_matched_entry == _routing_table.end()) {
                max_matched_entry = entry_iter;
            }
        }
    }
    // decrease TTL, send modified datagram on the appropriate interface to the appropriate next hop if datagram is still alive
    if (dgram.header().ttl-- > 1 && max_matched_entry != _routing_table.end()) {
        if (max_matched_entry->next_hop.has_value()) {
            interface(max_matched_entry->interface_num).send_datagram(dgram, max_matched_entry->next_hop.value());
        }
        else {
            interface(max_matched_entry->interface_num).send_datagram(dgram, Address::from_ipv4_numeric(dgram.header().dst));
        }
    }
    // otherwise do nothing to drop the datagram
}

void Router::route() {
    // Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
    for (auto &interface : _interfaces) {
        auto &queue = interface.datagrams_out();
        while (not queue.empty()) {
            route_one_datagram(queue.front());
            queue.pop();
        }
    }
}
