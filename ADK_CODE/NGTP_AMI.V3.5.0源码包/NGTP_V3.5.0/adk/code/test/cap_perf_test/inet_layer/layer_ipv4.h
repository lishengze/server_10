#ifndef INET_LAYER_IPV4_H_
#define INET_LAYER_IPV4_H_

#include "layer_base.h"

namespace inet
{
/**
* @struct IPv4Header
* Represents an IPv4 protocol header
*/
#pragma pack(push, 1)
struct IPv4Header 
{
#if (BYTE_ORDER == LITTLE_ENDIAN)
    /** IP header length, has the value of 5 for IPv4 */
    uint8_t header_len : 4,
        /** IP version number, has the value of 4 for IPv4 */
        version : 4;
#else
    /** IP version number, has the value of 4 for IPv4 */
    uint8_t version : 4,
        /** IP header length, has the value of 5 for IPv4 */
        header_len : 4;
#endif
    /** type of service, same as Differentiated Services Code Point (DSCP)*/
    uint8_t service_type;
    /** Entire packet (fragment) size, including header and data, in bytes */
    uint16_t total_len;
    /** Identification field. Primarily used for uniquely identifying the group of fragments of a single IP datagram*/
    uint16_t ip_identification;
    /** Fragment offset field, measured in units of eight-byte blocks (64 bits) */
    uint16_t fragment_offset;
    /** An eight-bit time to live field helps prevent datagrams from persisting (e.g. going in circles) on an internet.  In practice, the field has become a hop count */
    uint8_t time_to_live;
    /** Defines the protocol used in the data portion of the IP datagram. Must be one of ::IPProtocolTypes */
    uint8_t protocol;
    /** Error-checking of the header */
    uint16_t header_checksum;
    /** IPv4 address of the sender of the packet */
    uint32_t src_ip;
    /** IPv4 address of the receiver of the packet */
    uint32_t dst_ip;
    /*The options start here. */
};

constexpr uint32_t kIPv4HeaderLen = sizeof(IPv4Header);
#pragma pack(pop)

/**
* An enum for all possible IPv4 and IPv6 protocol types
*/
enum IPProtocol
{
    /** Dummy protocol for TCP        */
    kIPProtocolIP = 0,
    /** IPv6 Hop-by-Hop options        */
    kIPProtocolHopOpts = 0,
    /** Internet Control Message Protocol    */
    kIPProtocolICMP = 1,
    /** Internet Gateway Management Protocol */
    kIPProtocolIGMP = 2,
    /** IPIP tunnels (older KA9Q tunnels use 94) */
    kIPProtocolIPIP = 4,
    /** Transmission Control Protocol    */
    kIPProtocolTCP = 6,
    /** Exterior Gateway Protocol        */
    kIPProtocolEGP = 8,
    /** PUP protocol                */
    kIPProtocolPUP = 12,
    /** User Datagram Protocol        */
    kIPProtocolUDP = 17,
    /** XNS IDP protocol            */
    kIPProtocolIDP = 22,
    /** IPv6 header                */
    kIPProtocolIPV6 = 41,
    /** IPv6 Routing header            */
    kIPProtocolRouting = 43,
    /** IPv6 fragmentation header        */
    kIPProtocolFragment = 44,
    /** GRE protocol */
    kIPProtocolGRE = 47,
    /** encapsulating security payload    */
    kIPProtocolESP = 50,
    /** authentication header        */
    kIPProtocolAH = 51,
    /** ICMPv6                */
    kIPProtocolICMPv6 = 58,
    /** IPv6 no next header            */
    kIPProtocolNone = 59,
    /** IPv6 Destination options        */
    kIPProtocolDStopTS = 60,
    /** Raw IP packets            */
    kIPProtocolRAW = 255,
    /** Maximum value */
    kIPProtocolMAX
};

#define PCPP_IP_DONT_FRAGMENT    0x40
#define PCPP_IP_MORE_FRAGMENTS   0x20
#define MAX_ADDR_IPv4_STRING_LEN 16 
#define MAX_ADDR_IPv6_STRING_LEN 64 

class LayerIPv4 : public LayerBase
{
public:
    LayerIPv4(const LayerData& layder_data) : LayerBase(layder_data)
    {
        IPv4Header* const ipv4_header = (IPv4Header*)layer_data_.first;
        if (layer_data_.second >= kIPv4HeaderLen)
        {
            header_len_ = ipv4_header->header_len << 2;
            assert(header_len_ >= kIPv4HeaderLen);
        }
        else
        {
            throw InetParseException("layer ipv4: header is not completed");
        }

        uint16_t total_len = ntohs(ipv4_header->total_len);
		if (total_len == 0)
		{
			// tcp segmentation offload
			total_len = layder_data.second;
		}

        if (total_len <= layer_data_.second)
        {
            protocol_ = ipv4_header->protocol;
            assert(total_len > header_len_);
            payload_len_ = total_len - header_len_;
        }
        else
        {
            throw InetParseException("layer ipv4: body is not completed");
        }
    }

    ~LayerIPv4() = default;

    const string dest_ip() const
    {
        return ConvertIp(raw_dest_ip());
    }

    const string source_ip() const
    {
        return ConvertIp(raw_source_ip());
    }

    uint32_t raw_dest_ip() const
    {
        return ((IPv4Header*)layer_data_.first)->dst_ip;
    }

    uint32_t raw_source_ip() const
    {
        return ((IPv4Header*)layer_data_.first)->src_ip;
    }

    uint16_t fragment_offset() const
    {
        return ntohs(((IPv4Header*)layer_data_.first)->fragment_offset & (uint16_t)0xFF1F) << 3;
    }

    uint8_t fragment_flags() const
    {
        return ((IPv4Header*)layer_data_.first)->fragment_offset & 0xE0;
    }

    bool is_fragment() const
    {
        return ((fragment_offset() & PCPP_IP_MORE_FRAGMENTS) || (((IPv4Header*)layer_data_.first)->fragment_offset & (uint16_t)0xFF1F));
    }

    bool is_first_fragment() const
    {
        return is_fragment() && ((((IPv4Header*)layer_data_.first)->fragment_offset & (uint16_t)0xFF1F) == 0);
    }

    bool is_last_fragment() const
    {
        return is_fragment() && ((fragment_offset() & PCPP_IP_MORE_FRAGMENTS) == 0);
    }

    inline static string ConvertIp(uint32_t addr)
    {
        struct in_addr sin_addr;
        sin_addr.s_addr = addr;
        return inet_ntoa(sin_addr);
    }

    inline static uint32_t ConvertIp(const char* ip_str)
    {
        return inet_addr(ip_str);
    }
};

}

#endif // !INET_LAYER_IPV4_H_