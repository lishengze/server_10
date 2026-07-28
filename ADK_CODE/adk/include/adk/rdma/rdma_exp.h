#ifndef ADK_IMPL_RDMA_EXP_H_
#define ADK_IMPL_RDMA_EXP_H_

#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <rdma/rdma_cma.h>
#include <infiniband/ib.h>

#include "constant.h"
#include "../error_code.h"
#include "../lock_free_queue_variant.h"

namespace adk_impl
{

namespace rdma
{

class Context;
class UDEndpoint;
class UcEndpoint;
class McEndpoint;

struct TxNodeEntry
{
    TxNodeEntry(uint32_t mr_lkey);

    uint32_t buffer_size() const
    {
        return sge.length;
    }

    void set_buffer_size(uint32_t size)
    {
        sge.length = size;
    }

    const char* const_buffer() const
    {
        return reinterpret_cast<char*>(sge.addr);
    }

    char* buffer() const
    {
        return reinterpret_cast<char*>(sge.addr);
    }

    struct ibv_sge* sg_list()
    {
        return &sge;
    }

    uint64_t       queue_sqn;
    struct ibv_sge sge;
};

struct RxNodeEntry
{
    RxNodeEntry(uint32_t mr_lkey);

    char* buffer() const
    {
        return reinterpret_cast<char*>(sge.addr);
    }

    const char* const_buffer() const
    {
        return reinterpret_cast<char*>(sge.addr);
    }

    uint32_t buffer_size() const
    {
        return sge.length;
    }

    void set_buffer_size(uint32_t size)
    {
        sge.length = size;
    }

    static inline RxNodeEntry* GetNodeEntry(uint64_t wr_id)
    {
        return reinterpret_cast<RxNodeEntry*>(wr_id);
    }

    struct ibv_recv_wr recv_wr;
    struct ibv_sge     sge;
};

struct DestHandler
{
    enum Status
    {
        kIniting = 0,
        kNormal,
        kError,
    };

    std::string             dest_ip;
    uint16_t                dest_port;
    struct sockaddr_storage sa_dest;
    volatile int32_t        status;

    struct ibv_pd*          pd;
    struct ibv_ah_attr      ah_attr;

    struct ibv_ah*          ah;
    uint32_t                remote_qpn;
    uint32_t                remote_qkey;

    DestHandler(struct ibv_pd* _pd);

    void set_dest_addr(const std::string& ip);

    void set_dest_addr(const std::string& ip, uint16_t port);

    struct sockaddr* dest_addr()
    {
        return (struct sockaddr*)(&sa_dest);
    }

    bool is_valid() const
    {
        return Status::kError != status;
    }

    bool is_ready() const
    {
        return Status::kNormal == status;
    }
};

namespace impl
{

using TxMessagePool = variant::SPSCQueue<TxNodeEntry*>;
using RespDhMap = std::unordered_map<uint64_t, DestHandler*>;

class EndpointBase
{
public:
    inline const uint32_t max_message_size() const
    {
        assert(active_mtu_ > sizeof(struct ibv_grh));
        return active_mtu_ - sizeof(struct ibv_grh);
    }

    static const char* GetLastError();

protected:
    EndpointBase(Context* const context);
    virtual ~EndpointBase();

    int32_t Init();

    void Exit();

    inline struct TxNodeEntry* NewTxMessage()
    {
        assert(tx_message_pool_);
        variant::VariantEntry* entry_ptr;
        if (ErrorCode::kSuccess == tx_message_pool_->TryWaitEntry(&entry_ptr))
        {
        entry_assign:
            char* const buffer = entry_ptr->buffer;
            TxNodeEntry* node_entry = *(TxNodeEntry**)buffer;
            node_entry->queue_sqn = entry_ptr->pos;
            node_entry->set_buffer_size(tx_entry_size_);

            tx_message_pool_->FreeEntry(entry_ptr);
            return node_entry;
        }
        else
        {
            if ((ErrorCode::kSuccess == RecycleTxEntries()) 
                && (ErrorCode::kSuccess == tx_message_pool_->TryWaitEntry(&entry_ptr)))
            {
                goto entry_assign;
            }
        }

        return nullptr;
    }

    inline int32_t RecycleTxEntries()
    {
        const auto poll_res = ibv_poll_cq(send_cq_, 
                                          constant::kMaxTxRecycleSize, 
                                          send_wcs_);
        if (poll_res > 0)
        {
            struct ibv_wc& wc = send_wcs_[poll_res - 1];
            assert(IBV_WC_SUCCESS == wc.status);

            struct TxNodeEntry* node_entry = reinterpret_cast<struct TxNodeEntry*>(wc.wr_id);
            assert(node_entry);

            tx_message_pool_->UnsafeRecoveryBack(node_entry->queue_sqn);
            return ErrorCode::kSuccess;
        }

        return ErrorCode::kFailure;
    }

    inline int32_t SendMsg(struct TxNodeEntry* node_entry, const DestHandler* dest_handler)
    {
        const auto inline_bits = ((uint32_t)(node_entry->buffer_size() < max_inline_data_))
                                  << constant::kIbvSendInlineBits;
        const auto tx_batch_bits = ((uint32_t)(!(++tx_msg_counter_ & constant::kTxSignalBatchSizeMask)))
                                    << constant::kIbvSendSignaledBits;

        send_wr_.send_flags = inline_bits | tx_batch_bits;
        send_wr_.sg_list = node_entry->sg_list();
        send_wr_.wr_id = reinterpret_cast<uint64_t>(node_entry);

        send_wr_.wr.ud.ah = dest_handler->ah;
        send_wr_.wr.ud.remote_qkey = dest_handler->remote_qkey;
        send_wr_.wr.ud.remote_qpn = dest_handler->remote_qpn;

        struct ibv_send_wr *bad_wr;
        if (ADK_UNLIKELY(0 != ibv_post_send(cma_id_->qp, &send_wr_, &bad_wr)))
        {
            return ErrorCode::kFailure;
        }

        return ErrorCode::kSuccess;
    }

    inline int32_t SendMsg(const char* tx_buf, uint32_t len, const DestHandler* dest_handler)
    {
        TxNodeEntry* const node_entry = NewTxMessage();
        if (ADK_UNLIKELY(nullptr == node_entry))
        {
            return ErrorCode::kFailure;
        }

        const uint32_t copy_len = std::min<uint32_t>(len, max_message_size());
        memcpy(node_entry->buffer(), tx_buf, copy_len);
        node_entry->set_buffer_size(copy_len);

        return SendMsg(node_entry, dest_handler);
    }

    template<typename OnMsgFunc>
    inline int32_t RecvMMsg(const OnMsgFunc& on_msg_func, int32_t& msg_size)
    {
        return RecvMMsgRaw([&](struct ibv_grh* grh, char* buffer, uint32_t buffer_size) {
            on_msg_func(buffer, buffer_size);
        }, msg_size);
    }

    template<typename OnMsgFunc>
    inline int32_t RecvMMsg(const OnMsgFunc& on_msg_func)
    {
        int32_t unused_temp;
        return RecvMMsg(on_msg_func, unused_temp);
    }

    template<typename OnMsgFuncWithDA>
    inline int32_t RecvMMsgDA(const OnMsgFuncWithDA& on_msg_func, int32_t& msg_size)
    {
        return RecvMMsgRaw([&](struct ibv_grh* grh, char* buffer, uint32_t buffer_size) {
            on_msg_func(buffer, buffer_size, static_cast<uint32_t>(grh->dgid.global.interface_id >> 32));
        }, msg_size);
    }

    template<typename OnMsgFuncWithDA>
    inline int32_t RecvMMsgDA(const OnMsgFuncWithDA& on_msg_func)
    {
        int32_t unused_temp;
        return RecvMMsgDA(on_msg_func, unused_temp);
    }

    template<typename OnMsgDhFunc>
    inline int32_t RecvMMsgDh(const OnMsgDhFunc& on_msg_dh_func, int32_t& msg_size)
    {
        int32_t index;
        msg_size = ibv_poll_cq(recv_cq_, 
                               constant::kMaxRxBatchSize, 
                               recv_wcs_);

        try
        {
            for (index = 0; index < msg_size; ++index)
            {
                struct ibv_wc& wc = recv_wcs_[index];
                if (ADK_UNLIKELY(IBV_WC_SUCCESS != wc.status))
                {
                    return ErrorCode::kFailure;
                }

                RxNodeEntry* const node_entry = RxNodeEntry::GetNodeEntry(wc.wr_id);
                assert(node_entry);

                const uint64_t remote_key = ((uint64_t)wc.pkey_index << 48) | ((uint64_t)wc.slid << 32) | wc.src_qp;
                const auto iter = resp_dh_map_.find(remote_key);
                if (resp_dh_map_.end() != iter)
                {
                    on_msg_dh_func(node_entry->buffer() + sizeof(struct ibv_grh),
                                   wc.byte_len - sizeof(struct ibv_grh), 
                                   iter->second);
                }
                else
                {
                    auto* const dh = CreateDestHandler((struct ibv_grh*)(node_entry->buffer()), &wc);
                    resp_dh_map_[remote_key] = dh;
                    on_msg_dh_func(node_entry->buffer() + sizeof(struct ibv_grh),
                                   wc.byte_len - sizeof(struct ibv_grh), 
                                   dh);
                }

                node_entry->set_buffer_size(rx_entry_size_);
                __attribute__((unused)) const auto post_ec = PostRxEntry(node_entry);
                assert(ErrorCode::kSuccess == post_ec);
            }
        }
        catch (...)
        {
            for (; index < msg_size; ++index)
            {
                struct ibv_wc& wc = recv_wcs_[index];
                RxNodeEntry* const node_entry = RxNodeEntry::GetNodeEntry(wc.wr_id);
                assert(node_entry);

                node_entry->set_buffer_size(rx_entry_size_);
                __attribute__((unused)) const auto post_ec = PostRxEntry(node_entry);
                assert(ErrorCode::kSuccess == post_ec);
            }
            return ErrorCode::kFailure;
        }

        return ErrorCode::kSuccess;
    }

	template<typename OnMsgFunc>
    inline int32_t RecvMMsgDh(const OnMsgFunc& on_msg_dh_func)
    {
        int32_t unused_temp;
        return RecvMMsgDh(on_msg_dh_func, unused_temp);
    }

    inline int32_t PostRxEntry(RxNodeEntry* node_entry)
    {
        struct ibv_recv_wr* bad_wr;
        if (ADK_UNLIKELY(0 != ibv_post_recv(cma_id_->qp, &(node_entry->recv_wr), &bad_wr)))
        {
            return ErrorCode::kFailure;
        }

        return ErrorCode::kSuccess;
    }

    bool OnRxIdle(int timeout_milliseconds = 1);

    DestHandler* CreateDestHandler(DestHandler* const src_dh);

    DestHandler* CreateDestHandler(struct ibv_grh* grh, struct ibv_wc* wc);

    static void DestroyDestHandler(DestHandler* const dh);

    Context*           context_;

    uint32_t           active_mtu_;
    uint32_t           max_inline_data_;
    bool               has_cqe_pending_;
    struct ibv_comp_channel* recv_cq_channel_;

    struct rdma_cm_id* cma_id_;
    struct ibv_pd*     pd_;

    TxMessagePool*     tx_message_pool_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    uint32_t           tx_entry_size_;
    uint32_t           tx_entry_node_size_;
    uint64_t           tx_msg_counter_;

    struct ibv_send_wr send_wr_;
    struct ibv_cq*     send_cq_;
    struct ibv_mr*     send_mr_;
    void*              send_mem_;
    struct ibv_wc      send_wcs_[constant::kMaxTxRecycleSize];

    uint32_t           rx_entry_size_;
    uint32_t           rx_entry_node_size_;

    struct ibv_cq*     recv_cq_;
    struct ibv_mr*     recv_mr_;
    void*              recv_mem_;
    struct ibv_wc      recv_wcs_[constant::kMaxRxBatchSize];

    RespDhMap          resp_dh_map_;

private:
    template<typename OnMsgFunc>
    inline int32_t RecvMMsgRaw(const OnMsgFunc& on_msg_func, int32_t& msg_size)
    {
        int32_t index;
        msg_size = ibv_poll_cq(recv_cq_, 
                               constant::kMaxRxBatchSize, 
                               recv_wcs_);

        try
        {
            for (index = 0; index < msg_size; ++index)
            {
                struct ibv_wc& wc = recv_wcs_[index];
                if (ADK_UNLIKELY(IBV_WC_SUCCESS != wc.status))
                {
                    return ErrorCode::kFailure;
                }

                RxNodeEntry* const node_entry = RxNodeEntry::GetNodeEntry(wc.wr_id);
                assert(node_entry);

                on_msg_func(reinterpret_cast<struct ibv_grh*>(node_entry->buffer()), 
                            node_entry->buffer() + sizeof(struct ibv_grh), 
                            wc.byte_len - sizeof(struct ibv_grh));

                node_entry->set_buffer_size(rx_entry_size_);
                __attribute__((unused)) const auto post_ec = PostRxEntry(node_entry);
                assert(ErrorCode::kSuccess == post_ec);
            }
        }
        catch (...)
        {
            for (; index < msg_size; ++index)
            {
                struct ibv_wc& wc = recv_wcs_[index];
                RxNodeEntry* const node_entry = RxNodeEntry::GetNodeEntry(wc.wr_id);
                assert(node_entry);

                node_entry->set_buffer_size(rx_entry_size_);
                __attribute__((unused)) const auto post_ec = PostRxEntry(node_entry);
                assert(ErrorCode::kSuccess == post_ec);
            }
            return ErrorCode::kFailure;
        }

        return ErrorCode::kSuccess;
    }

    friend class Context;
};

}

class Context
{
public:
    static Context* NewContext(const std::string& host_ip);

    static void DeleteContext(Context* context);

    McEndpoint* CreateMcEndpoint();

    void DestroyEndpoint(McEndpoint* const endpoint);

    UcEndpoint* CreateUcEndpoint(uint16_t host_port);

    void DestroyEndpoint(UcEndpoint* const endpoint);

    uint8_t sgid_index() const
    {
        return static_cast<uint8_t>(sgid_index_);
    }

    const std::string& host_ip() const
    {
        return host_ip_;
    }

    inline std::mutex& rdma_event_lock()
    {
        return rdma_event_lock_;
    }

    struct rdma_event_channel* event_channel() const
    {
        return rdma_event_channel_;
    }

    static const char* GetLastError();

    static void GetAddrInfo(const std::string& ip, 
                            uint16_t port, 
                            struct sockaddr& sa_dest);

private:
    Context();
    virtual ~Context() = default;

    template<typename EndpointType>
    EndpointType* CreateEndpointPrivate(uint16_t host_port);

    template<typename EndpointType>
    void DestroyEndpointPrivate(EndpointType* const endpoint);

    void CmaEventDrive();

    std::thread                cma_thrd_;

    std::string                host_ip_;
    volatile bool              is_running_;
    int32_t                    sgid_index_;

    std::mutex                 rdma_event_lock_;
    struct rdma_event_channel* rdma_event_channel_;
};

class UDEndpoint : public impl::EndpointBase
{
public:
    struct TxNodeEntry* NewTxMessage()
    {
        return impl::EndpointBase::NewTxMessage();
    }

    inline int32_t RecycleTxEntries()
    {
        return impl::EndpointBase::RecycleTxEntries();
    }

    inline int32_t SendMsg(struct TxNodeEntry* node_entry, 
                           const DestHandler* dest_handler)
    {
        return impl::EndpointBase::SendMsg(node_entry, dest_handler);
    }

    inline int32_t SendMsg(const char* tx_buf, 
                           uint32_t len, 
                           const DestHandler* dest_handler)
    {
        return impl::EndpointBase::SendMsg(tx_buf, len, dest_handler);
    }

    template<typename OnMsgFunc>
    inline int32_t RecvMMsg(const OnMsgFunc& on_msg_func, int32_t& msg_size)
    {
        return impl::EndpointBase::RecvMMsg(on_msg_func, msg_size);
    }

    template<typename OnMsgFunc>
    inline int32_t RecvMMsg(const OnMsgFunc& on_msg_func)
    {
        return impl::EndpointBase::RecvMMsg(on_msg_func);
    }

    template<typename OnMsgFunc>
    inline int32_t RecvMMsgDA(const OnMsgFunc& on_msg_func, int32_t& msg_size)
    {
        return impl::EndpointBase::RecvMMsgDA(on_msg_func, msg_size);
    }

    template<typename OnMsgFunc>
    inline int32_t RecvMMsgDA(const OnMsgFunc& on_msg_func)
    {
        return impl::EndpointBase::RecvMMsgDA(on_msg_func);
    }

    template<typename OnMsgFunc>
    inline int32_t RecvMMsgDh(const OnMsgFunc& on_msg_func, int32_t& msg_size)
    {
        return impl::EndpointBase::RecvMMsgDh(on_msg_func, msg_size);
    }

	template<typename OnMsgFunc>
    inline int32_t RecvMMsgDh(const OnMsgFunc& on_msg_func)
    {
        return impl::EndpointBase::RecvMMsgDh(on_msg_func);
    }

    inline int32_t PostRxEntry(RxNodeEntry* node_entry)
    {
        return impl::EndpointBase::PostRxEntry(node_entry);
    }

    inline bool OnRxIdle(int timeout_milliseconds = 1)
    {
        return impl::EndpointBase::OnRxIdle(timeout_milliseconds);
    }

    /**
     * @brief       ��������Endpoint��DestHandler�������ڵ�ǰEndpoint��DestHandler
     *
     * @param       src_dh��������Endpoint�Ŀ��õ�DestHandler
     * 
     * @return      �ɹ��������ض�Ӧ��DH/ʧ�ܷ���nullptr
     */
    inline DestHandler* CreateDestHandler(DestHandler* const src_dh)
    {
        return impl::EndpointBase::CreateDestHandler(src_dh);
    }

    static void DestroyDestHandler(DestHandler* const dh)
    {
        impl::EndpointBase::DestroyDestHandler(dh);
    }
protected:
    UDEndpoint(Context* const context);
};

class McEndpoint : public UDEndpoint
{
public:
    DestHandler* JoinMcGroup(const std::string& mc_addr);

    int32_t LeaveMcGroup(const std::string& mc_addr);

    int32_t LeaveMcGroup(DestHandler* const dest_handler);

    static void DestroyEndpoint(McEndpoint* endpoint);

protected:
    McEndpoint(Context* const context);

    int32_t Init(uint16_t host_port);

    void Exit();

    friend class Context;
};

class UcEndpoint : public McEndpoint
{
public:
    DestHandler* CreateDestHandler(const std::string& dest_ip, uint16_t dest_port);

    static void DestroyEndpoint(UcEndpoint* endpoint);

protected:
    UcEndpoint(Context* const context);

    int32_t Init(uint16_t host_port);

    void Exit();

    void RdmaDHAgent();

    std::thread        agent_thrd_;

    int                udp_socket_;
    uint64_t           dh_request_sqn_;
    std::mutex         requesting_map_lock_;
    std::map<uint64_t, DestHandler*> requesting_ah_map_;

    friend class Context;
};

}

}
#endif
