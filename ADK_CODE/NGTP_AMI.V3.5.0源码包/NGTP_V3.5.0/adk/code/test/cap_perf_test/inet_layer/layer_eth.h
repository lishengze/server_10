#ifndef INET_LAYER_ETH_H_
#define INET_LAYER_ETH_H_

#include "layer_base.h"
#include "layer_ipv4.h"

#include <boost/format.hpp>

namespace inet
{

/**
* @struct ether_header
* Represents an Ethernet header
*/
#pragma pack(push, 1)
struct EthHeader
{
    /** Destination MAC */
    uint8_t dst_mac[6];
    /** Source MAC */
    uint8_t src_mac[6];
    /** Ethernet protocol */
    uint16_t protocol;
};

constexpr uint32_t kEthHeaderLen = sizeof(EthHeader);
#pragma pack(pop)

typedef uint32_t LBEthHeader;
constexpr LBEthHeader kDLTLoopHeader = AF_INET;

/* Ethernet protocol ID's */
enum EthProtocol
{
    /** Xerox PUP */
    kEthProtocolPup = 0x0200,
    /** Sprite */
    kEthProtocolSprite = 0x0500,
    /** IP */
    kEthProtocolIP = 0x0800,
    /** IP host endian*/
    kEthProtocolIPHostEndian = 0x0008,
    /** Address resolution */
    kEthProtocolARP = 0x0806,
    /** Reverse ARP */
    kEthProtocolRevARP = 0x8035,
    /** AppleTalk protocol */
    kEthProtocolAT = 0x809B,
    /** AppleTalk ARP */
    kEthProtocolAARP = 0x80F3,
    /** IEEE 802.1Q VLAN tagging */
    kEthProtocolVLAN = 0x8100,
    /** IEEE 802.1Q VLAN tagging host endian */
    kEthProtocolVLANHostEndian = 0x0081,
    /** IPX */
    kEthProtocolIPX = 0x8137,
    /** IP protocol version 6 */
    kEthProtocolIPV6 = 0x86dd,
    /** used to test interfaces */
    kEthProtocolLoopback = 0x9000,
    /** PPPoE discovery */
    kEthProtocolPPPoED = 0x8863,
    /** PPPoE session */
    kEthProtocolPPPoES = 0x8864,
    /** MPLS */
    kEthProtocolMPLS = 0x8847,
    /** Point-to-point protocol (PPP) */
    kEthProtocolPPP = 0x880B,
};

#define MAC_STRING_LEN       19

constexpr uint32_t kVLANHeaderLen = 4;

template<bool is_dlt_loop>
class LayerEth : public LayerBase
{
public:
    LayerEth(const LayerData& layer_data) : LayerBase(layer_data)
    {
        if (is_dlt_loop)
        {
            if (layer_data_.second >= sizeof(LBEthHeader))
            {
                assert(kDLTLoopHeader == *((LBEthHeader*)layer_data_.first));
                header_len_ = sizeof(LBEthHeader);
                protocol_ = EthProtocol::kEthProtocolIPHostEndian;
                payload_len_ = (uint16_t)(layer_data_.second - sizeof(LBEthHeader));
            }
            else
            {
                throw InetParseException("layer eth: header is not completed");
            }
        }
        else
        {
            if (layer_data_.second >= kEthHeaderLen)
            {
                EthHeader* const eth_header = (EthHeader*)layer_data_.first;
                header_len_ = kEthHeaderLen;
                protocol_ = eth_header->protocol;
                if (EthProtocol::kEthProtocolVLANHostEndian == protocol_)
                {
                    protocol_ = *(uint16_t*)(((char*)(&(eth_header->protocol))) + kVLANHeaderLen);
                    header_len_ += kVLANHeaderLen;
                }

                payload_len_ = (uint16_t)(layer_data_.second - header_len_);
            }
            else
            {
                throw InetParseException("layer eth: header is not completed");
            }
        }
    }

    LayerEth(const uint8_t* packet, uint32_t len) : LayerEth(std::make_pair(packet, len))
    {
    }

    ~LayerEth() = default;

    const string GetDestMac() const
    {
        return LayerEth::ConvertMac(((EthHeader*)layer_data_.first)->dst_mac);
    }

    const string GetSourceMac() const
    {
        return LayerEth::ConvertMac(((EthHeader*)layer_data_.first)->src_mac);
    }

    static inline string ConvertMac(const uint8_t* mac)
    {
        return (boost::format("%02x:%02x:%02x:%02x:%02x:%02x")
            % (uint32_t)mac[0]
            % (uint32_t)mac[1]
            % (uint32_t)mac[2]
            % (uint32_t)mac[3]
            % (uint32_t)mac[4]
            % (uint32_t)mac[5]).str();
    }
};

}

#endif