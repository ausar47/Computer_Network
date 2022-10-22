#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    // look up the Ethernet destination address for the next hop
    const auto arp_iter = _arp_table.find(next_hop_ip);
    // case not found
    if (arp_iter == _arp_table.end()) {
        // broadcast an ARPMessage to get the address for the next hop if no same ARPMessage has been sent in the last 5 seconds
        if (_arp_msg_list.find(next_hop_ip) == _arp_msg_list.end()) {
            ARPMessage arp_request;
            arp_request.opcode = ARPMessage::OPCODE_REQUEST;
            arp_request.sender_ethernet_address = _ethernet_address;
            arp_request.sender_ip_address = _ip_address.ipv4_numeric();
            arp_request.target_ip_address = next_hop_ip;
            EthernetFrame eth_frame;
            eth_frame.header() = {
                ETHERNET_BROADCAST,
                _ethernet_address,
                EthernetHeader::TYPE_ARP
            };
            eth_frame.payload() = arp_request.serialize();
            _frames_out.push(eth_frame);
            _arp_msg_list[next_hop_ip] = ARP_MESSAGE_DEFAULT_TTL;
        }
        // IPv4 datagram begins waiting for ARP request
        _dgram_waiting_list.emplace_back(next_hop, dgram);
    }
    // case found, encapsulate in an Ethernet frame and push the frame onto the frames_out queue
    else {
        EthernetFrame eth_frame;
        eth_frame.header() = {
            arp_iter->second.eth_addr,
            _ethernet_address,
            EthernetHeader::TYPE_IPv4
        };
        eth_frame.payload() = dgram.serialize();
        _frames_out.push(eth_frame);
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    // filter irrelevant package
    if (frame.header().dst != _ethernet_address && frame.header().dst != ETHERNET_BROADCAST) {
        return nullopt;
    }
    // IPv4 frame
    if (frame.header().type == EthernetHeader::TYPE_IPv4) {
        InternetDatagram dgram;
        // parse fail
        if (dgram.parse(frame.payload()) != ParseResult::NoError) {
            return nullopt;
        }
        return dgram;
    }
    // ARP frame
    else if (frame.header().type == EthernetHeader::TYPE_ARP) {
        ARPMessage arp_msg;
        // parse fail
        if (arp_msg.parse(frame.payload()) != ParseResult::NoError) {
            return nullopt;
        }

        // case ARP request, ONLY IF TARGET IP ADDRESS FITS
        bool is_valid_arp_request = arp_msg.opcode == ARPMessage::OPCODE_REQUEST && arp_msg.target_ip_address == _ip_address.ipv4_numeric();
        if (is_valid_arp_request) {
            // reply is sent back only to the source
            ARPMessage arp_reply;
            arp_reply.opcode = ARPMessage::OPCODE_REPLY;
            arp_reply.sender_ethernet_address = _ethernet_address;
            arp_reply.sender_ip_address = _ip_address.ipv4_numeric();
            arp_reply.target_ethernet_address = arp_msg.sender_ethernet_address;
            arp_reply.target_ip_address = arp_msg.sender_ip_address;
            EthernetFrame eth_frame;
            eth_frame.header() = {/* dst  */ arp_msg.sender_ethernet_address,
                                  /* src  */ _ethernet_address,
                                  /* type */ EthernetHeader::TYPE_ARP};
            eth_frame.payload() = arp_reply.serialize();
            _frames_out.push(eth_frame);
        }

        // only REQUEST with UNFIT TARGET IP ADDRESS is rejected by this if
        if (is_valid_arp_request || arp_msg.opcode == ARPMessage::OPCODE_REPLY) {
            // learn a mapping, add it to ARP table for 30 seconds
            _arp_table[arp_msg.sender_ip_address] = {arp_msg.sender_ethernet_address, ARP_TABLE_DEFAULT_TTL};
            // remove corresponding IPv4 datagram from the waiting list and send it
            for (auto iter = _dgram_waiting_list.begin(); iter != _dgram_waiting_list.end(); ) {
                if (iter->first.ipv4_numeric() == arp_msg.sender_ip_address) {
                    send_datagram(iter->second, iter->first);
                    iter = _dgram_waiting_list.erase(iter);
                }
                else {
                    iter++;
                }
            }
            // got the reply, erase corresponding ARPMessage in the list if exists
            _arp_msg_list.erase(arp_msg.sender_ip_address);
        }
    }
    return nullopt;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    // erase outdated tuples from ARP table
    for (auto iter = _arp_table.begin(); iter != _arp_table.end(); ) {
        if (iter->second.ttl <= ms_since_last_tick)
            iter = _arp_table.erase(iter);
        else {
            iter->second.ttl -= ms_since_last_tick;
            ++iter;
        }
    }
    for (auto iter = _arp_msg_list.begin(); iter != _arp_msg_list.end(); ) {
        // case ARPMessage outdated
        if (iter->second <= ms_since_last_tick) {
            // resend ARPMessage
            ARPMessage arp_request;
            arp_request.opcode = ARPMessage::OPCODE_REQUEST;
            arp_request.sender_ethernet_address = _ethernet_address;
            arp_request.sender_ip_address = _ip_address.ipv4_numeric();
            arp_request.target_ip_address = iter->first;
            EthernetFrame eth_frame;
            eth_frame.header() = {
                ETHERNET_BROADCAST,
                _ethernet_address,
                EthernetHeader::TYPE_ARP
            };
            eth_frame.payload() = arp_request.serialize();
            _frames_out.push(eth_frame);
            // reset timer
            iter->second = ARP_MESSAGE_DEFAULT_TTL;
        }
        else {
            iter->second -= ms_since_last_tick;
            iter++;
        }
    }
}
