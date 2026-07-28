#ifndef DAS_TCP_DECODER_H_
#define DAS_TCP_DECODER_H_

#include "message_decoder.h"

#include "inet_layer/layer_eth.h"
#include "inet_layer/layer_tcp.h"
#include "inet_layer/layer_ipv4.h"

#include <list>
#include <vector>
#include <string>
#include <thread>
#include <utility>
#include <iostream>
#include <unordered_map>
#include <boost/format.hpp>

#include <adk/lock_free_stream_buffer.h>

namespace cap
{

class TcpStream;

struct StreamInfo
{
    uint32_t dst_ip;
    uint32_t src_ip;
    uint16_t dst_port;
    uint16_t src_port;

    bool operator == (const StreamInfo& stream_info) const
    {
        return (dst_ip == stream_info.dst_ip) 
            && (src_ip == stream_info.src_ip)
            && (dst_port == stream_info.dst_port)
            && (src_port == stream_info.src_port);
    }
};

template<class StInfo>
struct StreamHash
{
    std::size_t operator()(const StInfo& stream_info) const
    {
        // static_assert(false, "stream hasher undefined ...");
        return 0;
    }
};

template<>
struct StreamHash<StreamInfo>
{
    std::size_t operator()(const StreamInfo& stream_info) const
    {
        return ((std::size_t)(stream_info.dst_ip & stream_info.src_ip) << 32)
               + ((std::size_t)stream_info.dst_port << 16)
               + stream_info.src_port;
    }
};

using StreamMap = std::unordered_map<StreamInfo, TcpStream*, StreamHash<StreamInfo>>;

constexpr uint32_t kMaxUnorderSize = 8;
constexpr uint64_t kSeqMask = kuint32Max;
constexpr uint64_t kRoundAdden = kSeqMask + 1;

struct OutOfOrder
{
    uint64_t tcp_seq;
    uint8_t* data;
    uint16_t len;

    bool operator > (const OutOfOrder& pkt) const
    {
        return tcp_seq > pkt.tcp_seq;
    }

    bool operator < (const OutOfOrder& pkt) const
    {
        return tcp_seq < pkt.tcp_seq;
    }

    bool operator == (const OutOfOrder& pkt) const
    {
        return tcp_seq == pkt.tcp_seq;
    }
};

class TcpStream
{
private:
    TcpStream(MessageDecoder* message_decoder, 
              uint32_t dest_ip, 
              uint16_t dest_port, 
              uint32_t src_ip, 
              uint16_t src_port, 
              uint32_t tcp_seq)
    {
        struct in_addr ia_ip;
        ia_ip.s_addr = dest_ip;
        dest_ip_ = inet_ntoa(ia_ip);
        dest_port_ = ntohs(dest_port);

        ia_ip.s_addr = src_ip;
        src_ip_ = inet_ntoa(ia_ip);
        src_port_ = ntohs(src_port);

        stream_desc_ = (boost::format("%1%:%2%->%3%:%4%") 
                                      % dest_ip_ 
                                      % dest_port_ 
                                      % src_ip_ 
                                      % src_port_).str();
        message_decoder_ = message_decoder;

        expect_sqn_ = tcp_seq;
        stream_buffer_ = adk::StreamBuffer::Create();
    }

    void OnMessage(uint32_t tcp_seq, const void* stream_data, uint16_t len)
    {
        const uint64_t expect_mod = expect_sqn_ & kSeqMask;
        if (tcp_seq == expect_mod)
        {
            expect_sqn_ += len;
            OnMessage(stream_data, len);

        retry:
            if (ADK_UNLIKELY(0 != ooo_cache_list_.size()))
            {
                auto& pkt = ooo_cache_list_.front();
                if (ADK_UNLIKELY(pkt.tcp_seq < expect_sqn_))
                {
                    delete[] pkt.data;
                    ooo_cache_list_.pop_front();
                    goto retry;
                }
                else if (pkt.tcp_seq == expect_sqn_)
                {
                    expect_sqn_ += pkt.len;
                    OnMessage(pkt.data, pkt.len);
                    delete[] pkt.data;
                    ooo_cache_list_.pop_front();
                    goto retry;
                }
            }
        }
        else if (tcp_seq > expect_mod)
        {
            CacheOutOfOrder(expect_sqn_ + (tcp_seq - expect_mod), stream_data, len);
            if (ooo_cache_list_.size() >= kMaxUnorderSize)
            {
                const auto& pkt = ooo_cache_list_.front();
                OnMessageLost(expect_sqn_, pkt.tcp_seq);
                expect_sqn_ = pkt.tcp_seq;
                goto retry;
            }
        }
        else
        {
            // retransmit message larger than message with same seq last received
            const uint64_t message_end = tcp_seq + len;
            if (ADK_UNLIKELY(message_end > expect_mod))
            {
                const auto message_delivered = expect_mod - tcp_seq;
                const auto message_len = len - message_delivered;
                expect_sqn_ += message_len;
                OnMessage((const char*)stream_data + message_delivered, (uint32_t)message_len);
                goto retry;
            }

            if (expect_mod - tcp_seq > (kSeqMask >> 1))
            {
                const uint64_t round_seq = (uint64_t)tcp_seq + kRoundAdden;
                CacheOutOfOrder(expect_sqn_ + (round_seq - expect_mod), stream_data, len);

                std::cout << (boost::format("<%1%> tcp packet is unordered, expect sequence <%2%> is smaller than captured <%3%>")
                                            % stream_desc_
                                            % expect_mod
                                            % tcp_seq).str()
                          << std::endl;

                if (ooo_cache_list_.size() >= kMaxUnorderSize)
                {
                    const auto& packet_info = ooo_cache_list_.front();
                    OnMessageLost(expect_sqn_, packet_info.tcp_seq);
                    expect_sqn_ = packet_info.tcp_seq;
                    goto retry;
                }
            }
        }
    }

    const std::string& dest_ip() const
    {
        return dest_ip_;
    }

    uint16_t dest_port() const
    {
        return dest_port_;
    }

    const std::string& src_ip() const
    {
        return src_ip_;
    }

    uint16_t src_port() const
    {
        return src_port_;
    }

    void CacheOutOfOrder(uint64_t tcp_seq, const void* data, uint16_t len)
    {
        auto insert_pos = ooo_cache_list_.begin();
        while (insert_pos != ooo_cache_list_.end())
        {
            if (insert_pos->tcp_seq > tcp_seq)
            {
                break;
            }

            ++insert_pos;
        }

        OutOfOrder ooo_packet;
        ooo_packet.tcp_seq = tcp_seq;
        ooo_packet.data = new uint8_t[len];
        memcpy(ooo_packet.data, data, len);
        ooo_packet.len = len;
        ooo_cache_list_.insert(insert_pos, ooo_packet);
    }

    void OnMessage(const void* data, uint16_t len)
    {
        stream_buffer_->Push((const char*)data, len);
        DoStreamDecode();
    }

    void OnMessageLost(uint64_t range_low, uint64_t range_high)
    {
        DoStreamDecode();
        stream_buffer_->Reset();

        std::cout << "message lost <" << range_low << ":" << range_high << ">" << std::endl;
    }


    void DoStreamDecode()
    {
        assert(stream_buffer_);
        assert(message_decoder_);
        do 
        {
            auto buffer = stream_buffer_->WaitBuffer();
            if (buffer.second == 0)
            {
                break;
            }

            auto result = message_decoder_->OnMessage(buffer.first, buffer.second);
            if (result <= 0)
            {
                break;
            }
            else
            {
                stream_buffer_->FreeBuffer(result);
            }
        } while (true);
    }

    std::string dest_ip_;
    uint16_t    dest_port_;
    std::string src_ip_;
    uint16_t    src_port_;

    std::string stream_desc_;
    MessageDecoder* message_decoder_;

    uint64_t    expect_sqn_;
    adk::StreamBuffer*    stream_buffer_;
    std::list<OutOfOrder> ooo_cache_list_;

    friend class TcpDecoder;
};


class TcpDecoder
{
public:
    TcpDecoder(MessageDecoder* const message_decoder)
    {
        message_decoder_ = message_decoder;
        filter_dest_ip_ = 0;
        filter_dest_port_ = 0;
    }

    void OnRawPacket(const void* pkt, uint32_t len)
    {
        inet::LayerEth<false> layer_eth((uint8_t*)pkt, len);
        if (ADK_UNLIKELY(inet::EthProtocol::kEthProtocolIPHostEndian != layer_eth.protocol()))
        {
            return;
        }

        inet::LayerIPv4 layer_ipv4(layer_eth.GetPayload());
        if (ADK_UNLIKELY(inet::IPProtocol::kIPProtocolTCP != layer_ipv4.protocol()))
        {
            return;
        }

        inet::LayerTcp layer_tcp(layer_ipv4.GetPayload());
        const uint32_t source_ip = layer_ipv4.raw_source_ip();
        const uint16_t source_port = layer_tcp.raw_source_port();

        const uint32_t dest_ip = layer_ipv4.raw_dest_ip();
        const uint16_t dest_port = layer_tcp.raw_dest_port();

        auto payload = layer_tcp.GetPayload();
        if (ADK_UNLIKELY(0 == payload.second))
        {
            if (layer_tcp.rst_flag() || layer_tcp.fin_flag())
            {
                const StreamInfo stream_info = { dest_ip, source_ip, dest_port, source_port };
                const auto iter = stream_map_.find(stream_info);
                if (stream_map_.end() != iter)
                {
                    auto* tcp_stream = iter->second;
                    std::cout << (boost::format("stream %1%:%2%->%3%:%4% finished") 
                                                % tcp_stream->src_ip()
                                                % tcp_stream->src_port()
                                                % tcp_stream->dest_ip()
                                                % tcp_stream->dest_port()).str()
                              << std::endl;
                    stream_map_.erase(iter);
                }
            }
            return;
        }

        //std::cout << inet_addr("10.128.8.42") << std::endl;
        if (((0 == filter_dest_ip_) || (dest_ip == filter_dest_ip_)) 
            && ((0 == filter_dest_port_) || (dest_port == filter_dest_port_)))
        {
            TcpStream* tcp_stream;
            const auto tcp_seq = layer_tcp.sequence_number();

            const StreamInfo stream_info = { dest_ip, source_ip, dest_port, source_port };
            const auto iter = stream_map_.find(stream_info);
            if (ADK_UNLIKELY(stream_map_.end() == iter))
            {
                tcp_stream = new TcpStream(message_decoder_, dest_ip, dest_port, source_ip, source_port, tcp_seq);

                stream_map_[stream_info] = tcp_stream;
                std::cout << (boost::format("catch new stream %1%:%2%->%3%:%4%")
                    % tcp_stream->src_ip()
                    % tcp_stream->src_port()
                    % tcp_stream->dest_ip()
                    % tcp_stream->dest_port()).str()
                    << std::endl;
            }
            else
            {
                tcp_stream = iter->second;
            }

            tcp_stream->OnMessage(tcp_seq, payload.first, payload.second);
        }
    }

    void set_dest_filter(const std::string& dest_ip, uint16_t dest_port)
    {
        filter_dest_ip_ = inet_addr(dest_ip.c_str());
        filter_dest_port_ = htons(dest_port);
    }

private:
    uint32_t        filter_dest_ip_;
    uint16_t        filter_dest_port_;
    StreamMap       stream_map_;
    MessageDecoder* message_decoder_;
};

}

#endif
