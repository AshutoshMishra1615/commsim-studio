#pragma once
/**
 * blocks/protocol/packet_encap.hpp  --  M7: OSI / TCP-IP Packet Encapsulation
 *
 * Implements:
 *   - Real RFC-compliant encapsulation across the 4 core TCP/IP stack layers:
 *       1. Application Data (payload string/bytes)
 *       2. Transport Layer (TCP header, 20 bytes, with RFC 793 checksum)
 *       3. Network Layer (IPv4 header, 20 bytes, with RFC 791 checksum)
 *       4. Data Link Layer (Ethernet II frame, 14 bytes header + IEEE 802.3 CRC-32 FCS)
 *   - Full packet serialization, checksum verification, and detailed field disassembler.
 */
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace blocks {

class PacketEncap {
public:
    struct FieldInfo {
        std::string layer;
        std::string name;
        size_t byte_offset;
        size_t byte_length;
        std::string hex_value;
        std::string decoded_value;
    };

    struct EncapResult {
        std::vector<uint8_t> raw_ethernet_frame;
        size_t app_offset = 0;
        size_t app_len = 0;
        size_t tcp_offset = 0;
        size_t tcp_len = 0;
        size_t ip_offset = 0;
        size_t ip_len = 0;
        size_t eth_offset = 0;
        size_t eth_len = 0;
        uint32_t fcs_crc32 = 0;
        uint16_t ip_checksum = 0;
        uint16_t tcp_checksum = 0;
        std::vector<FieldInfo> fields;
    };

    /// Calculate standard IEEE 802.3 Ethernet CRC-32 (poly 0xEDB88320)
    static uint32_t crc32(const uint8_t* data, size_t length) {
        uint32_t crc = 0xFFFFFFFF;
        for (size_t i = 0; i < length; ++i) {
            crc ^= data[i];
            for (int k = 0; k < 8; ++k) {
                crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
            }
        }
        return ~crc;
    }

    /// Calculate standard 1's complement Internet Checksum (RFC 1071)
    static uint16_t internetChecksum(const uint8_t* data, size_t length) {
        uint32_t sum = 0;
        for (size_t i = 0; i + 1 < length; i += 2) {
            uint16_t word = (static_cast<uint16_t>(data[i]) << 8) | data[i + 1];
            sum += word;
        }
        if (length & 1) {
            sum += (static_cast<uint16_t>(data[length - 1]) << 8);
        }
        while (sum >> 16) {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }
        return static_cast<uint16_t>(~sum);
    }

    /// Build and encapsulate Application Data -> TCP -> IPv4 -> Ethernet II
    static EncapResult encapsulate(const std::string& app_payload,
                                   const std::string& src_ip_str = "192.168.1.105",
                                   const std::string& dst_ip_str = "142.250.190.46",
                                   uint16_t src_port = 54321,
                                   uint16_t dst_port = 80,
                                   uint32_t seq_num = 10001,
                                   uint32_t ack_num = 5001) {
        EncapResult res;
        std::vector<uint8_t> app_bytes(app_payload.begin(), app_payload.end());

        // Parse IP addresses
        uint32_t src_ip = parseIp(src_ip_str);
        uint32_t dst_ip = parseIp(dst_ip_str);

        // 1. TCP Header (20 bytes)
        const size_t tcp_len = 20 + app_bytes.size();
        std::vector<uint8_t> tcp_seg(tcp_len, 0);

        // Source Port
        tcp_seg[0] = static_cast<uint8_t>((src_port >> 8) & 0xFF);
        tcp_seg[1] = static_cast<uint8_t>(src_port & 0xFF);
        // Destination Port
        tcp_seg[2] = static_cast<uint8_t>((dst_port >> 8) & 0xFF);
        tcp_seg[3] = static_cast<uint8_t>(dst_port & 0xFF);
        // Sequence Number
        tcp_seg[4] = static_cast<uint8_t>((seq_num >> 24) & 0xFF);
        tcp_seg[5] = static_cast<uint8_t>((seq_num >> 16) & 0xFF);
        tcp_seg[6] = static_cast<uint8_t>((seq_num >> 8) & 0xFF);
        tcp_seg[7] = static_cast<uint8_t>(seq_num & 0xFF);
        // Ack Number
        tcp_seg[8]  = static_cast<uint8_t>((ack_num >> 24) & 0xFF);
        tcp_seg[9]  = static_cast<uint8_t>((ack_num >> 16) & 0xFF);
        tcp_seg[10] = static_cast<uint8_t>((ack_num >> 8) & 0xFF);
        tcp_seg[11] = static_cast<uint8_t>(ack_num & 0xFF);
        // Data Offset = 5 (20 bytes), Flags: ACK | PSH (0x18)
        tcp_seg[12] = 0x50; // 5 words
        tcp_seg[13] = 0x18; // ACK + PSH
        // Window Size (64240)
        tcp_seg[14] = 0xFA;
        tcp_seg[15] = 0xF0;
        // Checksum placeholder at [16..17]
        // Urgent Pointer (0)
        tcp_seg[18] = 0x00;
        tcp_seg[19] = 0x00;

        // Copy application data into TCP payload
        std::copy(app_bytes.begin(), app_bytes.end(), tcp_seg.begin() + 20);

        // Compute TCP checksum over pseudo-header + TCP segment
        std::vector<uint8_t> pseudo;
        pseudo.reserve(12 + tcp_len);
        for (int i = 3; i >= 0; --i) pseudo.push_back(static_cast<uint8_t>((src_ip >> (i * 8)) & 0xFF));
        for (int i = 3; i >= 0; --i) pseudo.push_back(static_cast<uint8_t>((dst_ip >> (i * 8)) & 0xFF));
        pseudo.push_back(0);  // zero byte
        pseudo.push_back(6);  // protocol = TCP
        pseudo.push_back(static_cast<uint8_t>((tcp_len >> 8) & 0xFF));
        pseudo.push_back(static_cast<uint8_t>(tcp_len & 0xFF));
        pseudo.insert(pseudo.end(), tcp_seg.begin(), tcp_seg.end());

        res.tcp_checksum = internetChecksum(pseudo.data(), pseudo.size());
        tcp_seg[16] = static_cast<uint8_t>((res.tcp_checksum >> 8) & 0xFF);
        tcp_seg[17] = static_cast<uint8_t>(res.tcp_checksum & 0xFF);

        // 2. IPv4 Header (20 bytes)
        const size_t ip_total_len = 20 + tcp_seg.size();
        std::vector<uint8_t> ip_packet(ip_total_len, 0);

        ip_packet[0] = 0x45; // Version 4, IHL 5
        ip_packet[1] = 0x00; // DSCP / ECN
        ip_packet[2] = static_cast<uint8_t>((ip_total_len >> 8) & 0xFF);
        ip_packet[3] = static_cast<uint8_t>(ip_total_len & 0xFF);
        ip_packet[4] = 0x1C; // Identification
        ip_packet[5] = 0x2D;
        ip_packet[6] = 0x40; // Flags: Don't Fragment (DF)
        ip_packet[7] = 0x00;
        ip_packet[8] = 64;   // TTL = 64
        ip_packet[9] = 6;    // Protocol = TCP
        // Checksum placeholder at [10..11]
        for (int i = 3; i >= 0; --i) ip_packet[12 + (3 - i)] = static_cast<uint8_t>((src_ip >> (i * 8)) & 0xFF);
        for (int i = 3; i >= 0; --i) ip_packet[16 + (3 - i)] = static_cast<uint8_t>((dst_ip >> (i * 8)) & 0xFF);

        res.ip_checksum = internetChecksum(ip_packet.data(), 20);
        ip_packet[10] = static_cast<uint8_t>((res.ip_checksum >> 8) & 0xFF);
        ip_packet[11] = static_cast<uint8_t>(res.ip_checksum & 0xFF);

        // Copy TCP segment into IP payload
        std::copy(tcp_seg.begin(), tcp_seg.end(), ip_packet.begin() + 20);

        // 3. Ethernet II Frame (14 bytes header + payload + 4 bytes FCS CRC-32)
        // Dest MAC: 00:1A:2B:3C:4D:5E, Src MAC: 00:11:22:33:44:55, EtherType: 0x0800 (IPv4)
        std::vector<uint8_t> eth_frame;
        eth_frame.reserve(14 + ip_packet.size() + 4);

        const uint8_t dst_mac[6] = { 0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x5E };
        const uint8_t src_mac[6] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };
        eth_frame.insert(eth_frame.end(), dst_mac, dst_mac + 6);
        eth_frame.insert(eth_frame.end(), src_mac, src_mac + 6);
        eth_frame.push_back(0x08); // EtherType IPv4
        eth_frame.push_back(0x00);

        // Payload: IP packet
        eth_frame.insert(eth_frame.end(), ip_packet.begin(), ip_packet.end());

        // FCS (CRC-32) over Destination MAC through end of IP packet
        res.fcs_crc32 = crc32(eth_frame.data(), eth_frame.size());
        eth_frame.push_back(static_cast<uint8_t>((res.fcs_crc32 >> 24) & 0xFF));
        eth_frame.push_back(static_cast<uint8_t>((res.fcs_crc32 >> 16) & 0xFF));
        eth_frame.push_back(static_cast<uint8_t>((res.fcs_crc32 >> 8) & 0xFF));
        eth_frame.push_back(static_cast<uint8_t>(res.fcs_crc32 & 0xFF));

        // Offsets
        res.raw_ethernet_frame = eth_frame;
        res.eth_offset = 0;
        res.eth_len    = 14;
        res.ip_offset  = 14;
        res.ip_len     = 20;
        res.tcp_offset = 34;
        res.tcp_len    = 20;
        res.app_offset = 54;
        res.app_len    = app_bytes.size();

        // Build Field breakdown
        res.fields = {
            { "Ethernet II", "Destination MAC", 0, 6, "00:1A:2B:3C:4D:5E", "Target Gateway MAC" },
            { "Ethernet II", "Source MAC",      6, 6, "00:11:22:33:44:55", "Host Network Interface Card" },
            { "Ethernet II", "EtherType",      12, 2, "0x0800",             "IPv4 (Internet Protocol v4)" },
            { "IPv4",        "Version / IHL",  14, 1, "0x45",               "IPv4, Header Length: 20 bytes" },
            { "IPv4",        "Total Length",   16, 2, hex16(ip_total_len),  std::to_string(ip_total_len) + " bytes" },
            { "IPv4",        "TTL",            22, 1, "0x40",               "64 hops" },
            { "IPv4",        "Protocol",       23, 1, "0x06",               "TCP (Transmission Control Protocol)" },
            { "IPv4",        "Header Checksum",24, 2, hex16(res.ip_checksum), "RFC 791 valid checksum" },
            { "IPv4",        "Source IP",      26, 4, hexIp(src_ip),        src_ip_str },
            { "IPv4",        "Destination IP", 30, 4, hexIp(dst_ip),        dst_ip_str },
            { "TCP",         "Source Port",    34, 2, hex16(src_port),      std::to_string(src_port) + " (Ephemeral)" },
            { "TCP",         "Destination Port",36, 2, hex16(dst_port),     std::to_string(dst_port) + " (HTTP)" },
            { "TCP",         "Sequence Number",38, 4, hex32(seq_num),       std::to_string(seq_num) },
            { "TCP",         "Acknowledgment", 42, 4, hex32(ack_num),       std::to_string(ack_num) },
            { "TCP",         "Flags",          47, 1, "0x18",               "PSH, ACK (Push, Acknowledgment)" },
            { "TCP",         "Checksum",       50, 2, hex16(res.tcp_checksum), "RFC 793 valid checksum" },
            { "Application", "Data Payload",   54, res.app_len, "--",        "\"" + app_payload + "\"" },
            { "Ethernet II", "FCS (CRC-32)",   eth_frame.size() - 4, 4, hex32(res.fcs_crc32), "IEEE 802.3 32-bit CRC" }
        };

        return res;
    }

private:
    static uint32_t parseIp(const std::string& ip) {
        uint32_t a = 0, b = 0, c = 0, d = 0;
        std::sscanf(ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d);
        return (a << 24) | (b << 16) | (c << 8) | d;
    }

    static std::string hex16(uint16_t v) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%04X", v);
        return buf;
    }

    static std::string hex32(uint32_t v) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%08X", v);
        return buf;
    }

    static std::string hexIp(uint32_t ip) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%02X.%02X.%02X.%02X",
                      (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
        return buf;
    }
};

}  // namespace blocks
