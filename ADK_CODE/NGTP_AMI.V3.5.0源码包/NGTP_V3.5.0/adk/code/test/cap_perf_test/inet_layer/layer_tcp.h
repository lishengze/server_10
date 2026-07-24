#ifndef INET_LAYER_TCP_H_
#define INET_LAYER_TCP_H_

#include "layer_base.h"

#include <vector>

namespace inet
{

using std::vector;

/**
 * @struct tcphdr
 * Represents an TCP protocol header
 */
#pragma pack(push,1)
struct TCPHeader 
{
    /** Source TCP port */
    uint16_t port_src;
    /** Destination TCP port */
    uint16_t port_dst;
    /** Sequence number */
    uint32_t sequence_number;
    /** Acknowledgment number */
    uint32_t ack_number;
#if (BYTE_ORDER == LITTLE_ENDIAN)
    uint16_t reserved : 4,
        /** Specifies the size of the TCP header in 32-bit words */
        data_offset : 4,
        /** FIN flag */
        fin_flag : 1,
        /** SYN flag */
        syn_flag : 1,
        /** RST flag */
        rst_flag : 1,
        /** PSH flag */
        psh_flag : 1,
        /** ACK flag */
        ack_flag : 1,
        /** URG flag */
        urg_flag : 1,
        /** ECE flag */
        ece_flag : 1,
        /** CWR flag */
        cwr_flag : 1;
#elif (BYTE_ORDER == BIG_ENDIAN)
    /** Specifies the size of the TCP header in 32-bit words */
    uint16_t dataOffset : 4,
        reserved : 4,
        /** CWR flag */
        cwrFlag : 1,
        /** ECE flag */
        eceFlag : 1,
        /** URG flag */
        urgFlag : 1,
        /** ACK flag */
        ackFlag : 1,
        /** PSH flag */
        pshFlag : 1,
        /** RST flag */
        rstFlag : 1,
        /** SYN flag */
        synFlag : 1,
        /** FIN flag */
        finFlag : 1;
#else
#error  "Endian is not LE nor BE..."
#endif
    /** The size of the receive window, which specifies the number of window size units (by default, bytes) */
    uint16_t    window_size;
    /** The 16-bit checksum field is used for error-checking of the header and data */
    uint16_t    header_checksum;
    /** If the URG flag (@ref tcphdr#urgFlag) is set, then this 16-bit field is an offset from the sequence number indicating the last urgent data byte */
    uint16_t    urgent_pointer;
};

constexpr uint32_t kTCPHeaderLen = sizeof(TCPHeader);
#pragma pack(pop)

/**
 * TCP options enum
 */
enum TCPOption 
{
    /** Padding */
    kTCPOptionNOP = 1,
    /** End of options */
    kTCPOptionEOL = 0,
    /** Segment size negotiating */
    kTCPOptionMSS = 2,
    /** Window scaling */
    kTCPOptionWindow = 3,
    /** SACK Permitted */
    kTCPOptionPerm = 4,
    /** SACK Block */
    kTCPOptionSack = 5,
    /** Echo (obsoleted by option ::kTCPOptionTimeStamp) */
    kTCPOptionEcho = 6,
    /** Echo Reply (obsoleted by option ::kTCPOptionTimeStamp) */
    kTCPOptionEchoReply = 7,
    /** TCP Timestamps */
    kTCPOptionTimeStamp = 8,
    /** CC (obsolete) */
    kTCPOptionCC = 11,
    /** CC.NEW (obsolete) */
    kTCPOptionCCNew = 12,
    /** CC.ECHO(obsolete) */
    kTCPOptionCCEcho = 13,
    /** MD5 Signature Option */
    kTCPOptionMD5 = 19,
    /** Multipath TCP */
    kTCPOptionMPTCP = 0x1e,
    /** SCPS Capabilities */
    kTCPOptionSCPS = 20,
    /** SCPS SNACK */
    kTCPOptionSNACK = 21,
    /** SCPS Record Boundary */
    kTCPOptionRecBound = 22,
    /** SCPS Corruption Experienced */
    kTCPOptionCorrExp = 23,
    /** Quick-Start Response */
    kTCPOptionQuickStart = 27,
    /** User Timeout Option (also, other known unauthorized use) */
    kTCPOptionUserTimeout = 28,
    /** RFC3692-style Experiment 1 (also improperly used for shipping products) */
    kTCPOptionExpFD = 0xfd,
    /** RFC3692-style Experiment 2 (also improperly used for shipping products) */
    kTCPOptionExpFE = 0xfe,
    /** Riverbed probe option, non IANA registered option number */
    kTCPOptionRvbdProbe = 76,
    /** Riverbed transparency option, non IANA registered option number */
    kTCPOptionRvbdTrpy = 78
};

// TCP option lengths

/** pcpp::PCPP_TCPOPT_NOP length */
#define PCPP_TCPOLEN_NOP            1
/** pcpp::PCPP_TCPOPT_EOL length */
#define PCPP_TCPOLEN_EOL            1
/** pcpp::TCPOPT_MSS length */
#define PCPP_TCPOLEN_MSS            4
/** pcpp::PCPP_TCPOPT_WINDOW length */
#define PCPP_TCPOLEN_WINDOW         3
/** pcpp::TCPOPT_SACK_PERM length */
#define PCPP_TCPOLEN_SACK_PERM      2
/** pcpp::PCPP_TCPOPT_SACK length */
#define PCPP_TCPOLEN_SACK_MIN       2
/** pcpp::TCPOPT_ECHO length */
#define PCPP_TCPOLEN_ECHO           6
/** pcpp::TCPOPT_ECHOREPLY length */
#define PCPP_TCPOLEN_ECHOREPLY      6
/** pcpp::PCPP_TCPOPT_TIMESTAMP length */
#define PCPP_TCPOLEN_TIMESTAMP     10
/** pcpp::TCPOPT_CC length */
#define PCPP_TCPOLEN_CC             6
/** pcpp::TCPOPT_CCNEW length */
#define PCPP_TCPOLEN_CCNEW          6
/** pcpp::TCPOPT_CCECHO length */
#define PCPP_TCPOLEN_CCECHO         6
/** pcpp::TCPOPT_MD5 length */
#define PCPP_TCPOLEN_MD5           18
/** pcpp::TCPOPT_MPTCP length */
#define PCPP_TCPOLEN_MPTCP_MIN      8
/** pcpp::TCPOPT_SCPS length */
#define PCPP_TCPOLEN_SCPS           4
/** pcpp::TCPOPT_SNACK length */
#define PCPP_TCPOLEN_SNACK          6
/** pcpp::TCPOPT_RECBOUND length */
#define PCPP_TCPOLEN_RECBOUND       2
/** pcpp::TCPOPT_CORREXP length */
#define PCPP_TCPOLEN_CORREXP        2
/** pcpp::TCPOPT_QS length */
#define PCPP_TCPOLEN_QS             8
/** pcpp::TCPOPT_USER_TO length */
#define PCPP_TCPOLEN_USER_TO        4
/** pcpp::TCPOPT_RVBD_PROBE length */
#define PCPP_TCPOLEN_RVBD_PROBE_MIN 3
/** pcpp::TCPOPT_RVBD_TRPY length */
#define PCPP_TCPOLEN_RVBD_TRPY_MIN 16
/** pcpp::TCPOPT_EXP_FD and pcpp::TCPOPT_EXP_FE length */
#define PCPP_TCPOLEN_EXP_MIN        2

#ifndef __GNUC__
#pragma warning(disable:4200)
#endif
/**
 * @struct TCPOptionDetail
 * Representing a TCP option in a TLV (type-length-value) type
 */
struct TCPOptionDetail
{
public:

    /**
     * @return The total size in bytes of this TCP option which includes: 1[Byte] (option type) + 1[Byte]
     * (option length) + X[Bytes] (option data length)
     */
    inline uint32_t option_len() const
    {
        if ((option_ == (uint8_t)kTCPOptionNOP) || (option_ == (uint8_t)kTCPOptionEOL))
        {
            return (uint32_t)sizeof(uint8_t);
        }
        return (uint32_t)len_;
    }

    /**
     * A templated method to retrieve the TCP option data as a certain type T. For example, if option data is 4B
     * (integer) then this method should be used as getValueAs<int>() and it will return the TCP option data as an integer.<BR>
     * Notice this return value is a copy of the data, not a pointer to the actual data
     * @param[in] valueOffset An optional parameter that specifies where to start copy the TCP option data. For example:
     * if option data is 20 bytes and you need only the 4 last bytes as integer then use this method like this:
     * getValueAs<int>(16). The default is 0 - start copy from the beginning of option data
     * @return The TCP option data as type T
     */
    template<typename _Vty>
    _Vty option_value(int value_offset = 0)
    {
        return *((_Vty*)(value_ + value_offset));
    }

    /**
     * @return TCP option type casted as TcpOption enum
     */
    inline TCPOption option() const
    {
        return (TCPOption)option_;
    }

private:
    /** TCP option type, should be on of ::TcpOption */
    uint8_t option_;
    /** TCP option length */
    uint8_t len_;
    /** TCP option value */
    uint8_t value_[];
};

class LayerTcp : public LayerBase
{
public:
    LayerTcp(const LayerData& layder_data) : LayerBase(layder_data)
    {
        assert(layder_data.second >= kTCPHeaderLen);
        header_len_ = (((TCPHeader*)layer_data_.first)->data_offset << 2);
        assert(header_len_ >= kTCPHeaderLen);
        assert(layer_data_.second >= header_len_);
        payload_len_ = layer_data_.second - header_len_;
    }

    ~LayerTcp() = default;

    uint16_t dest_port() const
    {
        return ntohs(raw_dest_port());
    }

    uint16_t source_port() const
    {
        return ntohs(raw_source_port());
    }

    uint32_t sequence_number() const
    {
        return ntohl(((TCPHeader*)layer_data_.first)->sequence_number);
    }

    uint16_t raw_dest_port() const
    {
        return ((TCPHeader*)layer_data_.first)->port_dst;
    }

    uint16_t raw_source_port() const
    {
        return ((TCPHeader*)layer_data_.first)->port_src;
    }

    bool syn_flag() const
    {
        return ((TCPHeader*)layer_data_.first)->syn_flag;
    }

    bool fin_flag() const
    {
        return ((TCPHeader*)layer_data_.first)->fin_flag;
    }

    bool rst_flag() const
    {
        return ((TCPHeader*)layer_data_.first)->rst_flag;
    }

    const vector<TCPOptionDetail*> GetOptions()
    {
        vector<TCPOptionDetail*> options;

        uint32_t pos = kTCPHeaderLen;
        while (pos < header_len_)
        {
            TCPOptionDetail* option_detail = (TCPOptionDetail*)(layer_data_.first + pos);
            options.push_back(option_detail);
            pos += option_detail->option_len();
        }

        return options;
    }
};

}

#endif // !INET_LAYER_TCP_H_