#include <poll.h>
#include <netdb.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <net/if_arp.h>
#include <linux/types.h>
#include <net/ethernet.h>
#include <linux/if_packet.h>

#include <type_traits>

#include <adk/rdma/rdma_exp.h>
#include <adk/rdma/rdma_raw_packet.h>

namespace adk_impl
{

namespace rdma
{

#define SET_LAST_ERROR(error_info)                               \
    adk_impl::rdma::impl::set_last_error(error_info);

#define SET_LAST_TF_ERROR(format, ...)                           \
    sprintf(adk_impl::rdma::impl::s_last_error, format, __VA_ARGS__); \
    PRINT_DEBUG(adk_impl::rdma::impl::s_last_error);

struct ibv_verbs
{
    struct ibv_mr* (*ibv_reg_mr)(struct ibv_pd *pd, void *addr, 
        size_t length, int access);
    int (*ibv_destroy_qp)(struct ibv_qp *qp);
    int (*ibv_destroy_ah)(struct ibv_ah *ah);
    int (*ibv_dealloc_pd)(struct ibv_pd *pd);
    int (*ibv_query_gid)(struct ibv_context *context, 
        uint8_t port_num, int index, union ibv_gid *gid);
    int (*ibv_dereg_mr)(struct ibv_mr *mr);
    int (*ibv_query_port_ex)(struct ibv_context *context,
        uint8_t port_num, struct ibv_port_attr *port_attr);
    int (*ibv_query_device)(struct ibv_context*, struct ibv_device_attr*);
    int (*ibv_destroy_cq)(struct ibv_cq *cq);
    struct ibv_cq* (*ibv_create_cq)(struct ibv_context *context, 
        int cqe, void *cq_context, struct ibv_comp_channel *channel, int comp_vector);
    struct ibv_pd* (*ibv_alloc_pd)(struct ibv_context *context);
    struct ibv_ah* (*ibv_create_ah)(struct ibv_pd *pd, struct ibv_ah_attr *attr);
    int (*ibv_init_ah_from_wc)(struct ibv_context *context,
                               uint8_t port_num,
                               struct ibv_wc *wc, 
                               struct ibv_grh *grh, 
                               struct ibv_ah_attr *ah_attr);
    int (*ibv_query_qp)(struct ibv_qp *qp, 
                        struct ibv_qp_attr *attr, 
                        int attr_mask, 
                        struct ibv_qp_init_attr *init_attr);
    int (*ibv_modify_qp)(struct ibv_qp *qp, struct ibv_qp_attr *attr, int attr_mask);
    struct ibv_qp* (*ibv_create_qp)(struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr);
    int (*ibv_get_cq_event)(struct ibv_comp_channel*, struct ibv_cq**, void**);
    void (*ibv_ack_cq_events)(struct ibv_cq*, unsigned int);
    struct ibv_comp_channel* (*ibv_create_comp_channel)(struct ibv_context*);
    int (*ibv_destroy_comp_channel)(struct ibv_comp_channel*);
};

struct rdma_verbs
{
    int (*rdma_resolve_addr)(struct rdma_cm_id *id, 
        struct sockaddr *src_addr, struct sockaddr *dst_addr, int timeout_ms);
    int (*rdma_join_multicast)(struct rdma_cm_id *id, struct sockaddr *addr, void *context);
    int (*rdma_create_qp)(struct rdma_cm_id *id, 
        struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr);
    int (*rdma_ack_cm_event)(struct rdma_cm_event *event);
    int (*rdma_resolve_route)(struct rdma_cm_id *id, int timeout_ms);
    void (*rdma_destroy_event_channel)(struct rdma_event_channel *channel);
    int (*rdma_get_cm_event)(struct rdma_event_channel *channel, 
        struct rdma_cm_event **event);
    int (*rdma_create_id)(struct rdma_event_channel *channel, 
        struct rdma_cm_id **id, void *context, enum rdma_port_space ps);
    struct rdma_event_channel* (*rdma_create_event_channel)();
    int (*rdma_destroy_id)(struct rdma_cm_id *id);
    int (*rdma_bind_addr)(struct rdma_cm_id *id, struct sockaddr *addr);
    int (*rdma_leave_multicast)(struct rdma_cm_id *id, struct sockaddr *addr);
    // int (*rdma_listen)(struct rdma_cm_id *id, int backlog);
    // int (*rdma_accept)(struct rdma_cm_id *id, struct rdma_conn_param *conn_param);
    // int (*rdma_connect)(struct rdma_cm_id *id, struct rdma_conn_param *conn_param);
    // int (*rdma_disconnect)(struct rdma_cm_id *id);
};

static ibv_verbs  s_ibv_verbs;
static rdma_verbs s_rdma_verbs;

static bool InitVerbsLib()
{
    static int32_t s_init_status = 0;
    if (1 == s_init_status)
    {
        return true;
    }
    else if (-1 == s_init_status)
    {
        return false;
    }

    static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;

    pthread_mutex_lock(&s_mutex);
    if (0 != s_init_status)
    {
        pthread_mutex_unlock(&s_mutex);
        return 1 == s_init_status;
    }

    void* ibv_verbs_handle = nullptr;
    void* rdma_verbs_handle = nullptr;
    ibv_verbs_handle = dlopen("libibverbs.so", RTLD_LAZY);
    if (nullptr == ibv_verbs_handle)
    {
        goto error;
    }

    rdma_verbs_handle = dlopen("librdmacm.so", RTLD_LAZY);
    if (nullptr == rdma_verbs_handle)
    {
        goto error;
    }

    *(void**)(&s_ibv_verbs.ibv_reg_mr) = dlsym(ibv_verbs_handle, "ibv_reg_mr");
    *(void**)(&s_ibv_verbs.ibv_destroy_qp) = dlsym(ibv_verbs_handle, "ibv_destroy_qp");
    *(void**)(&s_ibv_verbs.ibv_destroy_ah) = dlsym(ibv_verbs_handle, "ibv_destroy_ah");
    *(void**)(&s_ibv_verbs.ibv_dealloc_pd) = dlsym(ibv_verbs_handle, "ibv_dealloc_pd");
    *(void**)(&s_ibv_verbs.ibv_query_gid) = dlsym(ibv_verbs_handle, "ibv_query_gid");
    *(void**)(&s_ibv_verbs.ibv_dereg_mr) = dlsym(ibv_verbs_handle, "ibv_dereg_mr");
    *(void**)(&s_ibv_verbs.ibv_query_port_ex) = dlsym(ibv_verbs_handle, "ibv_query_port");
    *(void**)(&s_ibv_verbs.ibv_query_device) = dlsym(ibv_verbs_handle, "ibv_query_device");
    *(void**)(&s_ibv_verbs.ibv_destroy_cq) = dlsym(ibv_verbs_handle, "ibv_destroy_cq");
    *(void**)(&s_ibv_verbs.ibv_create_cq) = dlsym(ibv_verbs_handle, "ibv_create_cq");
    *(void**)(&s_ibv_verbs.ibv_alloc_pd) = dlsym(ibv_verbs_handle, "ibv_alloc_pd");
    *(void**)(&s_ibv_verbs.ibv_create_ah) = dlsym(ibv_verbs_handle, "ibv_create_ah");
    *(void**)(&s_ibv_verbs.ibv_init_ah_from_wc) = dlsym(ibv_verbs_handle, "ibv_init_ah_from_wc");
    *(void**)(&s_ibv_verbs.ibv_query_qp) = dlsym(ibv_verbs_handle, "ibv_query_qp");
    *(void**)(&s_ibv_verbs.ibv_modify_qp) = dlsym(ibv_verbs_handle, "ibv_modify_qp");
    *(void**)(&s_ibv_verbs.ibv_create_qp) = dlsym(ibv_verbs_handle, "ibv_create_qp");
    *(void**)(&s_ibv_verbs.ibv_get_cq_event) = dlsym(ibv_verbs_handle, "ibv_get_cq_event");
    *(void**)(&s_ibv_verbs.ibv_ack_cq_events) = dlsym(ibv_verbs_handle, "ibv_ack_cq_events");
    *(void**)(&s_ibv_verbs.ibv_create_comp_channel) = dlsym(ibv_verbs_handle, "ibv_create_comp_channel");
    *(void**)(&s_ibv_verbs.ibv_destroy_comp_channel) = dlsym(ibv_verbs_handle, "ibv_destroy_comp_channel");

    *(void**)(&s_rdma_verbs.rdma_resolve_addr) = dlsym(rdma_verbs_handle, "rdma_resolve_addr");
    *(void**)(&s_rdma_verbs.rdma_join_multicast) = dlsym(rdma_verbs_handle, "rdma_join_multicast");
    *(void**)(&s_rdma_verbs.rdma_create_qp) = dlsym(rdma_verbs_handle, "rdma_create_qp");
    *(void**)(&s_rdma_verbs.rdma_ack_cm_event) = dlsym(rdma_verbs_handle, "rdma_ack_cm_event");
    *(void**)(&s_rdma_verbs.rdma_resolve_route) = dlsym(rdma_verbs_handle, "rdma_resolve_route");
    *(void**)(&s_rdma_verbs.rdma_destroy_event_channel) = dlsym(rdma_verbs_handle, "rdma_destroy_event_channel");
    *(void**)(&s_rdma_verbs.rdma_get_cm_event) = dlsym(rdma_verbs_handle, "rdma_get_cm_event");
    *(void**)(&s_rdma_verbs.rdma_create_id) = dlsym(rdma_verbs_handle, "rdma_create_id");
    *(void**)(&s_rdma_verbs.rdma_create_event_channel) = dlsym(rdma_verbs_handle, "rdma_create_event_channel");
    *(void**)(&s_rdma_verbs.rdma_destroy_id) = dlsym(rdma_verbs_handle, "rdma_destroy_id");
    *(void**)(&s_rdma_verbs.rdma_bind_addr) = dlsym(rdma_verbs_handle, "rdma_bind_addr");
    *(void**)(&s_rdma_verbs.rdma_leave_multicast) = dlsym(rdma_verbs_handle, "rdma_leave_multicast");
    // *(void**)(&s_rdma_verbs.rdma_listen) = dlsym(rdma_verbs_handle, "rdma_listen");
    // *(void**)(&s_rdma_verbs.rdma_accept) = dlsym(rdma_verbs_handle, "rdma_accept");
    // *(void**)(&s_rdma_verbs.rdma_connect) = dlsym(rdma_verbs_handle, "rdma_connect");
    // *(void**)(&s_rdma_verbs.rdma_disconnect) = dlsym(rdma_verbs_handle, "rdma_disconnect");

    s_init_status = 1;
    pthread_mutex_unlock(&s_mutex);

    return true;

error:
    s_init_status = -1;
    pthread_mutex_unlock(&s_mutex);

    if (nullptr != ibv_verbs_handle)
    {
        dlclose(ibv_verbs_handle);
        ibv_verbs_handle = nullptr;
    }

    if (nullptr != rdma_verbs_handle)
    {
        dlclose(rdma_verbs_handle);
        rdma_verbs_handle = nullptr;
    }

    return false;
}

namespace impl
{

static thread_local char s_last_error[1024] = { 0 };

static void set_last_error(const char* error_info)
{
    strcpy(s_last_error, error_info);
    PRINT_DEBUG(s_last_error);
}

const char* EndpointBase::GetLastError()
{
    return s_last_error;
}

EndpointBase::EndpointBase(Context* const context)
{
    context_ = context;

    active_mtu_ = 0;
    max_inline_data_ = 0;
    has_cqe_pending_ = false;
    recv_cq_channel_ = nullptr;

    cma_id_ = nullptr;
    pd_ = nullptr;

    tx_message_pool_ = nullptr;
    tx_entry_size_ = 0;
    tx_entry_node_size_ = 0;
    tx_msg_counter_ = 0;

    bzero(&send_wr_, sizeof(struct ibv_send_wr));
    send_cq_ = nullptr;
    send_mr_ = nullptr;
    send_mem_ = nullptr;

    memset(&send_wcs_, 0, sizeof(struct ibv_wc) * constant::kMaxTxRecycleSize);

    rx_entry_size_ = 0;
    rx_entry_node_size_ = 0;

    recv_cq_ = nullptr;
    recv_mr_ = nullptr;
    recv_mem_ = nullptr;

    memset(&recv_wcs_, 0, sizeof(struct ibv_wc) * constant::kMaxRxBatchSize);
}

EndpointBase::~EndpointBase()
{
    Exit();
}

int32_t EndpointBase::Init()
{
    struct ibv_port_attr port_attr;

    /* For compatibility when running with old libibverbs */
    port_attr.link_layer = IBV_LINK_LAYER_UNSPECIFIED;
    port_attr.reserved = 0;
    if (ADK_UNLIKELY(0 != s_ibv_verbs.ibv_query_port_ex(cma_id_->verbs,
                                                        cma_id_->port_num,
                                                        &port_attr)))
    {
        SET_LAST_ERROR("ibv_query_port failed");
        return ErrorCode::kFailure;
    }

    active_mtu_ = 1 << (port_attr.active_mtu + 7);
    PRINT_DEBUG("active mtu <" << active_mtu_ << ">");

    pd_ = s_ibv_verbs.ibv_alloc_pd(cma_id_->verbs);
    if (ADK_UNLIKELY(nullptr == pd_))
    {
        SET_LAST_ERROR("unable to allocate PD");
        return ErrorCode::kFailure;
    }

    send_cq_ = s_ibv_verbs.ibv_create_cq(cma_id_->verbs, 
                                         constant::kTxQueuePairDepth, 
                                         this, 
                                         nullptr, 
                                         0);
    if (ADK_UNLIKELY(nullptr == send_cq_))
    {
        SET_LAST_ERROR("unable to create tx CQ");
        return ErrorCode::kFailure;
    }

    recv_cq_channel_ = s_ibv_verbs.ibv_create_comp_channel(cma_id_->verbs);
    if (ADK_UNLIKELY(nullptr == recv_cq_channel_))
    {
        SET_LAST_TF_ERROR("ibv_create_comp_channel failed, %s", strerror(errno));
        return false;
    }

    if (ADK_UNLIKELY(0 != fcntl(recv_cq_channel_->fd,
                                F_SETFL,
                                O_NONBLOCK | fcntl(recv_cq_channel_->fd, F_GETFL))))
    {
        SET_LAST_ERROR(strerror(errno));
        return false;
    }

    recv_cq_ = s_ibv_verbs.ibv_create_cq(cma_id_->verbs,
                                         constant::kRxQueuePairDepth,
                                         this, 
                                         recv_cq_channel_, 
                                         1);
    if (ADK_UNLIKELY(nullptr == recv_cq_))
    {
        SET_LAST_ERROR("unable to create rx CQ");
        return ErrorCode::kFailure;
    }

    struct ibv_qp_init_attr init_qp_attr;
    init_qp_attr.qp_context = this;
    init_qp_attr.send_cq = send_cq_;
    init_qp_attr.recv_cq = recv_cq_;
    init_qp_attr.srq = nullptr;

    init_qp_attr.cap.max_send_wr = constant::kTxQueuePairDepth;
    init_qp_attr.cap.max_recv_wr = constant::kRxQueuePairDepth;
    init_qp_attr.cap.max_send_sge = 1;
    init_qp_attr.cap.max_recv_sge = 1;
    init_qp_attr.cap.max_inline_data = 0;

    init_qp_attr.qp_type = IBV_QPT_UD;
    init_qp_attr.sq_sig_all = 1;
    init_qp_attr.xrc_domain = nullptr;

    if (ADK_UNLIKELY(0 != s_rdma_verbs.rdma_create_qp(cma_id_, 
                                                      pd_, 
                                                      &init_qp_attr)))
    {
        SET_LAST_ERROR("unable to create QP");
        return ErrorCode::kFailure;
    }

    PRINT_DEBUG("cma_id <" << (void*)cma_id_ << 
                "> | port_num <" << (uint32_t)(cma_id_->port_num) << 
                "> | qp_num <" << cma_id_->qp->qp_num << ">");

    struct ibv_qp_attr qp_attr;
    if (ADK_UNLIKELY(0 != s_ibv_verbs.ibv_query_qp(cma_id_->qp,
                                                   &qp_attr,
                                                   IBV_QP_CAP,
                                                   &init_qp_attr)))
    {
        SET_LAST_ERROR("query qp attribute <IBV_QP_CAP>");
        return ErrorCode::kFailure;
    }

    max_inline_data_ = init_qp_attr.cap.max_inline_data;
    PRINT_DEBUG("max inline data <" << max_inline_data_ << ">");

    tx_entry_size_ = active_mtu_;
    tx_entry_node_size_
        = ADK_ROUND_UP(sizeof(struct TxNodeEntry) + tx_entry_size_, 
                       ADK_CACHE_LINE_SIZE);

    const auto total_tx_size
        = ADK_ROUND_UP(tx_entry_node_size_ * constant::kTxMessagePoolSize, ADK_PAGE_SIZE);

    send_mem_ = memalign(ADK_PAGE_SIZE, total_tx_size);
    if (ADK_UNLIKELY(nullptr == send_mem_))
    {
        SET_LAST_ERROR("allocate tx memory");
        return ErrorCode::kFailure;
    }

    send_mr_ = s_ibv_verbs.ibv_reg_mr(pd_, send_mem_, total_tx_size, 0);
    if (ADK_UNLIKELY(nullptr == send_mr_))
    {
        SET_LAST_ERROR("register tx memory region failed");
        return ErrorCode::kFailure;
    }

    tx_message_pool_ = TxMessagePool::Create("tx message pool", 
                                             constant::kTxMessagePoolSize);
    if (ADK_UNLIKELY(nullptr == tx_message_pool_))
    {
        SET_LAST_ERROR("create tx message pool failed");
        return ErrorCode::kFailure;
    }

    char* tx_mem = (char*)(send_mem_);
    for (uint32_t index = 0; index < constant::kTxMessagePoolSize; ++index)
    {
        TxNodeEntry* node_entry = (TxNodeEntry*)tx_mem;

        new ((void*)node_entry) TxNodeEntry(send_mr_->lkey);

        __attribute__((unused)) const auto ec
            = tx_message_pool_->TryPush(node_entry);
        assert(ErrorCode::kSuccess == ec);

        tx_mem += tx_entry_node_size_;
    }

    send_wr_.next = nullptr;
    send_wr_.opcode = IBV_WR_SEND_WITH_IMM;
    send_wr_.send_flags = 0;
    send_wr_.num_sge = 1;
    send_wr_.imm_data = htobe32(cma_id_->qp->qp_num);

    rx_entry_size_ = active_mtu_;
    rx_entry_node_size_
        = ADK_ROUND_UP(sizeof(struct RxNodeEntry) + rx_entry_size_, 
                       ADK_PAGE_SIZE);

    const auto total_rx_size = rx_entry_node_size_ * constant::kRxQueuePairDepth;

    recv_mem_ = memalign(ADK_PAGE_SIZE, total_rx_size);
    if (ADK_UNLIKELY(nullptr == recv_mem_))
    {
        SET_LAST_ERROR("allocate rx memory failed");
        return ErrorCode::kFailure;
    }

    recv_mr_ = s_ibv_verbs.ibv_reg_mr(pd_, 
                                      recv_mem_, 
                                      total_rx_size, 
                                      IBV_ACCESS_LOCAL_WRITE);
    if (ADK_UNLIKELY(nullptr == recv_mr_))
    {
        SET_LAST_ERROR("register rx memory region failed");
        return ErrorCode::kFailure;
    }

    struct ibv_recv_wr *bad_wr;
    char* rx_mem = (char*)(recv_mem_);
    for (uint32_t index = 0; index < constant::kRxQueuePairDepth; ++index)
    {
        RxNodeEntry* node_entry = (RxNodeEntry*)rx_mem;
        new ((void*)node_entry) RxNodeEntry(recv_mr_->lkey);

        node_entry->set_buffer_size(rx_entry_size_);

        __attribute__((unused)) const auto ec
            = ibv_post_recv(cma_id_->qp, &(node_entry->recv_wr), &bad_wr);

        assert(0 == ec);

        rx_mem += rx_entry_node_size_;
    }

    return ErrorCode::kSuccess;
}

void EndpointBase::Exit()
{
    if (nullptr != cma_id_)
    {
        if (nullptr != cma_id_->qp)
        {
            s_ibv_verbs.ibv_destroy_qp(cma_id_->qp);
            cma_id_->qp = nullptr;
        }

        if (nullptr != send_cq_)
        {
            s_ibv_verbs.ibv_destroy_cq(send_cq_);
            send_cq_ = nullptr;
        }

        if (nullptr != send_mr_)
        {
            s_ibv_verbs.ibv_dereg_mr(send_mr_);
            send_mr_ = nullptr;
        }

        if (nullptr != recv_cq_)
        {
            s_ibv_verbs.ibv_destroy_cq(recv_cq_);
            recv_cq_ = nullptr;
        }

        if (nullptr != recv_mr_)
        {
            s_ibv_verbs.ibv_dereg_mr(recv_mr_);
            recv_mr_ = nullptr;
        }

        if (nullptr != pd_)
        {
            s_ibv_verbs.ibv_dealloc_pd(pd_);
            pd_ = nullptr;
        }

        if (nullptr != recv_cq_channel_)
        {
            s_ibv_verbs.ibv_destroy_comp_channel(recv_cq_channel_);
            recv_cq_channel_ = nullptr;
        }

        s_rdma_verbs.rdma_destroy_id(cma_id_);
        cma_id_ = nullptr;
    }

    if (nullptr != send_mem_)
    {
        free(send_mem_);
        send_mem_ = nullptr;
    }

    if (nullptr != tx_message_pool_)
    {
        TxMessagePool::Delete(tx_message_pool_);
        tx_message_pool_ = nullptr;
    }

    if (nullptr != recv_mem_)
    {
        free(recv_mem_);
        recv_mem_ = nullptr;
    }
}

bool EndpointBase::OnRxIdle(int timeout_milliseconds)
{
    assert(recv_cq_);
    assert(recv_cq_channel_);

    if (has_cqe_pending_)
    {
        void* context;
        struct ibv_cq* cq;
        if (ADK_UNLIKELY(0 == s_ibv_verbs.ibv_get_cq_event(recv_cq_channel_, &cq, &context)))
        {
            s_ibv_verbs.ibv_ack_cq_events(recv_cq_, 1);
        }
    }

    const auto ec = ibv_req_notify_cq(recv_cq_, 0);
    if (ADK_UNLIKELY(0 != ec))
    {
        has_cqe_pending_ = false;
        return false;
    }

    struct pollfd poll_fd;
    poll_fd.fd = recv_cq_channel_->fd;
    poll_fd.events = POLLIN;
    return has_cqe_pending_ = ((1 == poll(&poll_fd, 1, timeout_milliseconds)) && (POLLIN & poll_fd.revents));
}

DestHandler* EndpointBase::CreateDestHandler(DestHandler* const src_dh)
{
    if (DestHandler::Status::kNormal == src_dh->status)
    {
        auto* ah = s_ibv_verbs.ibv_create_ah(pd_, &src_dh->ah_attr);
        if (ADK_UNLIKELY(nullptr == ah))
        {
            return nullptr;
        }

        DestHandler* const dest_handler = new DestHandler(pd_);
        dest_handler->dest_ip = src_dh->dest_ip;
        dest_handler->dest_port = src_dh->dest_port;
        memcpy(&(dest_handler->sa_dest), 
               &(src_dh->sa_dest), 
               sizeof(struct sockaddr_storage));
        dest_handler->status = DestHandler::Status::kNormal;
        dest_handler->ah_attr = src_dh->ah_attr;
        dest_handler->ah = ah;
        dest_handler->remote_qpn = src_dh->remote_qpn;
        dest_handler->remote_qkey = src_dh->remote_qkey;

        return dest_handler;
    }

    return nullptr;
}

DestHandler* EndpointBase::CreateDestHandler(struct ibv_grh* grh, struct ibv_wc* wc)
{
    DestHandler* const dest_handler = new DestHandler(pd_);
    if (0 != s_ibv_verbs.ibv_init_ah_from_wc(cma_id_->verbs,
                                             cma_id_->port_num, 
                                             wc, 
                                             grh, 
                                             &(dest_handler->ah_attr)))
    {
        delete dest_handler;
        return nullptr;
    }

    dest_handler->ah = s_ibv_verbs.ibv_create_ah(pd_, &(dest_handler->ah_attr));
    if (nullptr == dest_handler->ah)
    {
        delete dest_handler;
        return nullptr;
    }
    
    dest_handler->status = DestHandler::Status::kNormal;
    dest_handler->pd = pd_;
    dest_handler->remote_qpn = wc->src_qp;
    dest_handler->remote_qkey = RDMA_UDP_QKEY;
    return dest_handler;
}

void EndpointBase::DestroyDestHandler(DestHandler* const dh)
{
    assert(dh);

    if (nullptr != dh->ah)
    {
        s_ibv_verbs.ibv_destroy_ah(dh->ah);
        dh->ah = nullptr;

        PRINT_DEBUG("destroy destination address handler <" << dh->dest_ip << 
                    ":" << dh->dest_port << ">");
    }

    delete dh;
}

}

TxNodeEntry::TxNodeEntry(uint32_t mr_lkey)
{
    sge.addr = reinterpret_cast<uint64_t>(this) + sizeof(struct TxNodeEntry);
    sge.lkey = mr_lkey;
}

RxNodeEntry::RxNodeEntry(uint32_t mr_lkey)
{
    recv_wr.wr_id = reinterpret_cast<uint64_t>(this);
    recv_wr.next = nullptr;
    recv_wr.sg_list = &sge;
    recv_wr.num_sge = 1;
    sge.addr = recv_wr.wr_id + sizeof(RxNodeEntry);
    sge.lkey = mr_lkey;
}

DestHandler::DestHandler(struct ibv_pd* _pd)
{
    dest_port = 0;
    bzero(&sa_dest, sizeof(struct sockaddr_storage));
    status = Status::kIniting;

    pd = _pd;

    bzero(&ah_attr, sizeof(struct ibv_ah_attr));

    ah = nullptr;
    remote_qpn = 0;
    remote_qkey = 0;
}

void DestHandler::set_dest_addr(const std::string& ip)
{
    set_dest_addr(ip, 0);
}

void DestHandler::set_dest_addr(const std::string& ip, uint16_t port)
{
    dest_ip = ip;
    dest_port = port;
    Context::GetAddrInfo(ip, port, (struct sockaddr&)sa_dest);
}

Context* Context::NewContext(const std::string& host_ip)
{
    static const char* kTentativeMcAddr = "224.0.0.1";
    static constexpr uint32_t kMcJoinWaitLoopTimes = 10000;

    if (ADK_UNLIKELY(!InitVerbsLib()))
    {
        SET_LAST_ERROR("load rdma library failed");
        return nullptr;
    }

    auto* _rdma_event_channel = s_rdma_verbs.rdma_create_event_channel();
    if (ADK_UNLIKELY(nullptr == _rdma_event_channel))
    {
        SET_LAST_ERROR("RDMA create event channel failed");
        return nullptr;
    }

    if (ADK_UNLIKELY(0 != fcntl(_rdma_event_channel->fd, F_SETFL, O_NONBLOCK)))
    {
        SET_LAST_ERROR("fail to set the RDMA event channel to nonblock mode");
        s_rdma_verbs.rdma_destroy_event_channel(_rdma_event_channel);
        return nullptr;
    }

    struct addrinfo* ai_dst;
    struct sockaddr_storage sa_storage;
    bzero(&sa_storage, sizeof(struct sockaddr_storage));
    if (ADK_UNLIKELY(0 != getaddrinfo(host_ip.c_str(), nullptr, nullptr, &ai_dst)))
    {
        return nullptr;
    }

    memcpy(&sa_storage, ai_dst->ai_addr, ai_dst->ai_addrlen);
    freeaddrinfo(ai_dst);

    struct rdma_cm_id* cm_id_temp;
    if (ADK_UNLIKELY(0 != s_rdma_verbs.rdma_create_id(_rdma_event_channel,
                                                      &cm_id_temp,
                                                      nullptr,
                                                      rdma_port_space::RDMA_PS_UDP)))
    {
        return nullptr;
    }

    int32_t sgid_index = -1;
    do 
    {
        if (ADK_UNLIKELY(0 != s_rdma_verbs.rdma_bind_addr(
            cm_id_temp,
            reinterpret_cast<struct sockaddr*>(&sa_storage))))
        {
            break;
        }

        if (ADK_UNLIKELY(nullptr == cm_id_temp->verbs))
        {
            SET_LAST_ERROR("load rdma verbs failed");
            break;
        }

        struct ibv_port_attr port_attr;
        /* For compatibility when running with old libibverbs */
        port_attr.link_layer = IBV_LINK_LAYER_UNSPECIFIED;
        port_attr.reserved = 0;

        if (ADK_UNLIKELY(0 != s_ibv_verbs.ibv_query_port_ex(cm_id_temp->verbs, 
                                                            cm_id_temp->port_num, 
                                                            &port_attr)))
        {
            break;
        }

        struct addrinfo* ai_dst;
        ADK_ASSERT(getaddrinfo(kTentativeMcAddr, nullptr, nullptr, &ai_dst), 0);
        memcpy(&sa_storage, ai_dst->ai_addr, ai_dst->ai_addrlen);
        freeaddrinfo(ai_dst);

        if (ADK_UNLIKELY(0 != s_rdma_verbs.rdma_join_multicast(
            cm_id_temp,
            reinterpret_cast<struct sockaddr*>(&sa_storage),
            nullptr)))
        {
            break;
        }

        struct rdma_cm_event *cm_event = nullptr;
        for (uint32_t index = 0; index < kMcJoinWaitLoopTimes; ++index)
        {
            if (ADK_UNLIKELY(0 != s_rdma_verbs.rdma_get_cm_event(_rdma_event_channel, &cm_event)))
            {
                usleep(0);
                continue;
            }

            if (RDMA_CM_EVENT_MULTICAST_JOIN == cm_event->event)
            {
                sgid_index = cm_event->param.ud.ah_attr.grh.sgid_index;
                index = kMcJoinWaitLoopTimes;
            }
            else if ((RDMA_CM_EVENT_MULTICAST_ERROR == cm_event->event)
                || (RDMA_CM_EVENT_DEVICE_REMOVAL == cm_event->event))
            {
                index = kMcJoinWaitLoopTimes;
            }

            s_rdma_verbs.rdma_ack_cm_event(cm_event);
        }

        s_rdma_verbs.rdma_leave_multicast(
            cm_id_temp,
            reinterpret_cast<struct sockaddr*>(&sa_storage));
    } while (false);

    s_rdma_verbs.rdma_destroy_id(cm_id_temp);
    if (ADK_UNLIKELY(-1 == sgid_index))
    {
        return nullptr;
    }

    PRINT_DEBUG("sgid_index = " + std::to_string(sgid_index));

    Context* context = new Context;

    context->host_ip_ = host_ip;
    context->sgid_index_ = sgid_index;
    context->rdma_event_channel_ = _rdma_event_channel;

    context->is_running_ = true;
    context->cma_thrd_ = std::thread(&Context::CmaEventDrive, context);
    return context;
}

void Context::DeleteContext(Context* context)
{
    if (nullptr != context)
    {
        context->is_running_ = false;

        assert(context->rdma_event_channel_);
        s_rdma_verbs.rdma_destroy_event_channel(context->rdma_event_channel_);

        if (context->cma_thrd_.joinable())
        {
            context->cma_thrd_.join();
        }

        context->rdma_event_channel_ = nullptr;
        delete context;
    }
}

McEndpoint* Context::CreateMcEndpoint()
{
    return CreateEndpointPrivate<McEndpoint>(0);
}

void Context::DestroyEndpoint(McEndpoint* const endpoint)
{
    DestroyEndpointPrivate(endpoint);
}

UcEndpoint* Context::CreateUcEndpoint(uint16_t host_port)
{
    return CreateEndpointPrivate<UcEndpoint>(host_port);
}

void Context::DestroyEndpoint(UcEndpoint* const endpoint)
{
    DestroyEndpointPrivate(endpoint);
}

const char* Context::GetLastError()
{
    return impl::s_last_error;
}

void Context::GetAddrInfo(const std::string& ip, uint16_t port, struct sockaddr& sa_dest)
{
    struct addrinfo* ai_dst;
    const auto ec = getaddrinfo(ip.c_str(), nullptr, nullptr, &ai_dst);
    if (ADK_UNLIKELY(0 != ec))
    {
        // SET_LAST_TF_ERROR("getaddrinfo failed <%s>", gai_strerror(ec));
        ((struct sockaddr_in*)(&sa_dest))->sin_family = AF_INET;
        ((struct sockaddr_in*)(&sa_dest))->sin_addr.s_addr = INADDR_ANY;
        ((struct sockaddr_in*)(&sa_dest))->sin_port = htobe16(port);
    }
    else
    {
        memcpy(&sa_dest, ai_dst->ai_addr, ai_dst->ai_addrlen);

        if (AF_INET == ai_dst->ai_family)
        {
            ((struct sockaddr_in*)(&sa_dest))->sin_port = htobe16(port);
        }
        else
        {
            ((struct sockaddr_in6*)(&sa_dest))->sin6_port = htobe16(port);
        }

        freeaddrinfo(ai_dst);
    }
}

Context::Context()
{
    is_running_ = false;
    sgid_index_ = -1;
    rdma_event_channel_ = nullptr;
}

template<typename EndpointType>
EndpointType* Context::CreateEndpointPrivate(uint16_t host_port)
{
    /*
    const auto port_space = std::is_same<EndpointType, RCEndpoint>::value
                             ? rdma_port_space::RDMA_PS_TCP
                             : rdma_port_space::RDMA_PS_UDP;
    */

    const auto port_space = rdma_port_space::RDMA_PS_UDP;
    EndpointType* endpoint = new EndpointType(this);

    struct rdma_cm_id* cma_id;
    if (ADK_UNLIKELY(0 != s_rdma_verbs.rdma_create_id(rdma_event_channel_,
                                                      &cma_id,
                                                      endpoint,
                                                      port_space)))
    {
        SET_LAST_TF_ERROR("RDMA create id failed, port space <%d>", port_space);
        goto err;
    }

    cma_id->qp = nullptr;
    endpoint->cma_id_ = cma_id;

    struct sockaddr_storage sa_host;
    bzero(&sa_host, sizeof(struct sockaddr_storage));
    GetAddrInfo(host_ip_, host_port, (struct sockaddr&)sa_host);
    if (ADK_UNLIKELY(0 != s_rdma_verbs.rdma_bind_addr(cma_id, 
                                                      (struct sockaddr*)&sa_host)))
    {
        SET_LAST_TF_ERROR("RDMA bind address <%s:%d> failed", 
                          host_ip_.c_str(), host_port);
        goto err;
    }
    else
    {
        PRINT_DEBUG("RDMA bind address <" << host_ip_.c_str() << 
                    ":" << host_port << "> success");
    }

    if (ADK_UNLIKELY(nullptr == cma_id->verbs))
    {
        SET_LAST_TF_ERROR("nic with address <%s> is not a rdma interface",
                          host_ip_.c_str());
        goto err;
    }

    if (ErrorCode::kSuccess != endpoint->Init(host_port))
    {
    err:
        DestroyEndpoint(endpoint);
        return nullptr;
    }

    return endpoint;
}

template<typename EndpointType>
void Context::DestroyEndpointPrivate(EndpointType* const endpoint)
{
    assert(endpoint);

    endpoint->Exit();
    delete endpoint;
}

void Context::CmaEventDrive()
{
    struct rdma_cm_event *cm_event = nullptr;
    PRINT_DEBUG("cma event drive thread start running");

    while (is_running_)
    {
        {
            std::lock_guard<std::mutex> _(rdma_event_lock_);
            if (0 == s_rdma_verbs.rdma_get_cm_event(rdma_event_channel_, &cm_event))
            {
                switch (cm_event->event)
                {
                case RDMA_CM_EVENT_MULTICAST_JOIN:
                    {
                        DestHandler* dest_handler = (DestHandler*)(cm_event->param.ud.private_data);
                        assert(dest_handler);

                    #ifdef _ADK_RDMA_DEBUG_
                        char gid_buf[INET6_ADDRSTRLEN];
                        inet_ntop(AF_INET6, 
                                cm_event->param.ud.ah_attr.grh.dgid.raw, 
                                gid_buf, 
                                INET6_ADDRSTRLEN);

                        PRINT_DEBUG("join multicast address <" << dest_handler->dest_ip <<
                                    "> | dgid <" << gid_buf <<
                                    "> | mlid <" << cm_event->param.ud.ah_attr.dlid <<
                                    "> | sl <" << cm_event->param.ud.ah_attr.sl <<
                                    "> | remote_qkey <" << cm_event->param.ud.qkey <<
                                    "> | remote_qpn <" << cm_event->param.ud.qp_num << ">");
                    #endif

                        struct ibv_ah* ah = s_ibv_verbs.ibv_create_ah(
                            dest_handler->pd, 
                            &(cm_event->param.ud.ah_attr));
                        if (ADK_UNLIKELY(nullptr == ah))
                        {
                            SET_LAST_ERROR("ibv create ah failed");
                            dest_handler->status = DestHandler::Status::kError;
                        }
                        else
                        {
                            dest_handler->ah_attr = cm_event->param.ud.ah_attr;

                            dest_handler->ah = ah;
                            dest_handler->remote_qkey = cm_event->param.ud.qkey;
                            dest_handler->remote_qpn = cm_event->param.ud.qp_num;

                            ADK_BARRIER();
                            dest_handler->status = DestHandler::Status::kNormal;
                        }
                    }
                    break;
                case RDMA_CM_EVENT_MULTICAST_ERROR:
                    {
                        DestHandler* dest_handler = (DestHandler*)(cm_event->param.ud.private_data);
                        assert(dest_handler);

                        SET_LAST_TF_ERROR("RDMA cm event multicast error: %s", 
                                        dest_handler->dest_ip.c_str());

                        ADK_BARRIER();
                        dest_handler->status = DestHandler::Status::kError;
                    }
                    break;
                case RDMA_CM_EVENT_DEVICE_REMOVAL:
                    PRINT_DEBUG("device removal detected");
                    break;
                default:
                    SET_LAST_TF_ERROR("RDMA cm event unknown <%d> | status <%d>", 
                                    cm_event->event, 
                                    cm_event->status);
                }

                s_rdma_verbs.rdma_ack_cm_event(cm_event);
                continue;
            }
        }

        if (EWOULDBLOCK != errno)
        {
            SET_LAST_ERROR("RDMA get cm event failed");
            break;
        }

        usleep(1000);
    }

    PRINT_DEBUG("cma event drive thread exit");
}

UDEndpoint::UDEndpoint(Context* const context)
    : impl::EndpointBase(context)
{
}

DestHandler* McEndpoint::JoinMcGroup(const std::string& mc_addr)
{
    DestHandler* const dest_handler = new DestHandler(pd_);
    dest_handler->set_dest_addr(mc_addr);

    std::lock_guard<std::mutex> _(context_->rdma_event_lock());
    if (ADK_UNLIKELY(0 != s_rdma_verbs.rdma_join_multicast(
        cma_id_, 
        dest_handler->dest_addr(), 
        dest_handler)))
    {
        SET_LAST_TF_ERROR("RDMA join multicast <%s> failed", 
                          dest_handler->dest_ip.c_str());

        delete dest_handler;
        return nullptr;
    }

    return dest_handler;
}

int32_t McEndpoint::LeaveMcGroup(const std::string& mc_addr)
{
    assert(cma_id_);

    struct sockaddr_storage sa_dest;
    Context::GetAddrInfo(mc_addr, 0, (struct sockaddr&)sa_dest);
    std::lock_guard<std::mutex> _(context_->rdma_event_lock());
    if (ADK_UNLIKELY(0 != s_rdma_verbs.rdma_leave_multicast(
        cma_id_, 
        (struct sockaddr*)(&sa_dest))))
    {
        PRINT_DEBUG("Cma id<" << (void*)cma_id_ << 
                    "> leave multicast address <" << mc_addr << "> failed");
        return ErrorCode::kFailure;
    }

    PRINT_DEBUG("Cma id<" << (void*)cma_id_ << 
                "> leave multicast address <" << mc_addr << "> success");
    return ErrorCode::kSuccess;
}

int32_t McEndpoint::LeaveMcGroup(DestHandler* const dest_handler)
{
    assert(cma_id_);
    assert(dest_handler);
    std::lock_guard<std::mutex> _(context_->rdma_event_lock());
    if (ADK_UNLIKELY(0 != s_rdma_verbs.rdma_leave_multicast(
        cma_id_, 
        dest_handler->dest_addr())))
    {
        PRINT_DEBUG("Cma id<" << (void*)cma_id_ << 
                    "> leave multicast address <" << dest_handler->dest_ip << 
                    "> failed");
        return ErrorCode::kFailure;
    }

    PRINT_DEBUG("Cma id<" << (void*)cma_id_ << 
                "> leave multicast address <" << dest_handler->dest_ip << 
                "> success");
    return ErrorCode::kSuccess;
}

void McEndpoint::DestroyEndpoint(McEndpoint* endpoint)
{
    if (nullptr != endpoint)
    {
        auto context = endpoint->context_;

        assert(context);
        context->DestroyEndpoint(endpoint);
    }
}

McEndpoint::McEndpoint(Context* const context)
    : UDEndpoint(context)
{
}

int32_t McEndpoint::Init(uint16_t host_port)
{
    const auto ec = impl::EndpointBase::Init();
    if (ADK_UNLIKELY(ErrorCode::kSuccess != ec))
    {
        return ec;
    }

    return ErrorCode::kSuccess;
}

void McEndpoint::Exit()
{
    impl::EndpointBase::Exit();
}

enum DhOpType
{
    kRequestDh = 0,
    kDhOpRespAh,
};

struct DhMsgHeader
{
    uint32_t length;
    uint32_t operate_type;
};

struct DhRequest
{
    DhRequest()
    {
        dh_header.length = sizeof(struct DhRequest);
        dh_header.operate_type = DhOpType::kRequestDh;
    }

    DhMsgHeader dh_header;
    uint64_t rpc_sqn;
};

struct DhResponse
{
    DhResponse()
    {
        dh_header.length = sizeof(struct DhResponse);
        dh_header.operate_type = DhOpType::kDhOpRespAh;
    }

    DhMsgHeader dh_header;
    uint64_t ctx_sqn;
    uint32_t qpn;
};

DestHandler* UcEndpoint::CreateDestHandler(const std::string& dest_ip, uint16_t dest_port)
{
    DestHandler* const dest_handler = new DestHandler(pd_);
    dest_handler->set_dest_addr(dest_ip, dest_port);

    const auto dh_request_sqn = __sync_fetch_and_add(&dh_request_sqn_, 1);

    struct DhRequest dh_request;
    dh_request.rpc_sqn = dh_request_sqn;

    PRINT_DEBUG("Cma id<" << cma_id_ << 
                "> send rdma destination address request <" << dh_request_sqn << 
                "> to <" << dest_handler->dest_ip << 
                ":" << dest_handler->dest_port << ">");

    const auto ec = sendto(udp_socket_, 
                           &dh_request,
                           sizeof(struct DhRequest),
                           0, 
                           dest_handler->dest_addr(), 
                           sizeof(struct sockaddr_in));
    if (ADK_UNLIKELY(-1 == ec))
    {
        SET_LAST_TF_ERROR("Cma id <%p> send request to <%s:%d> failed, error information <%s>",
                          cma_id_, 
                          dest_ip.c_str(), 
                          dest_port, 
                          strerror(errno));
        delete dest_handler;
        return nullptr;
    }

    {
        std::lock_guard<std::mutex> _(requesting_map_lock_);
        requesting_ah_map_[dh_request_sqn] = dest_handler;
    }

    return dest_handler;
}

void UcEndpoint::DestroyEndpoint(UcEndpoint* endpoint)
{
    if (nullptr != endpoint)
    {
        auto conext = endpoint->context_;

        assert(conext);
        conext->DestroyEndpoint(endpoint);
    }

}

UcEndpoint::UcEndpoint(Context* const context) : McEndpoint(context)
{
    udp_socket_ = -1;
    dh_request_sqn_ = 0;
}

int32_t UcEndpoint::Init(uint16_t host_port)
{
    const auto ec = impl::EndpointBase::Init();
    if (ADK_UNLIKELY(ErrorCode::kSuccess != ec))
    {
        return ec;
    }

    udp_socket_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (-1 == udp_socket_)
    {
        SET_LAST_TF_ERROR("create dgram socket failed <%s>", strerror(errno));
        return ErrorCode::kFailure;
    }

    int32_t opts = fcntl(udp_socket_, F_GETFL);
    if (opts < 0)
    {
        SET_LAST_TF_ERROR("fcntl socket F_GETFL failed <%s>", strerror(errno));
        return ErrorCode::kFailure;
    }

    opts |= O_NONBLOCK;
    if (fcntl(udp_socket_, F_SETFL, opts) < 0)
    {
        SET_LAST_TF_ERROR("fcntl socket F_SETFL failed <%s>", strerror(errno));
        return ErrorCode::kFailure;
    }

    if (0 != host_port)
    {
        int32_t reuse_addr = 1;
        setsockopt(udp_socket_,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   reinterpret_cast<const char*>(&reuse_addr),
                   sizeof(int32_t));

        assert(context_);

        const char* host_ip = context_->host_ip().c_str();

        struct sockaddr_in sa_host;
        sa_host.sin_family = AF_INET;
        sa_host.sin_addr.s_addr = inet_addr(host_ip);
        sa_host.sin_port = htobe16(host_port);
        if (0 != bind(udp_socket_, 
                      (struct sockaddr*)&sa_host, 
                      sizeof(struct sockaddr_in)))
        {
            SET_LAST_TF_ERROR("bind address <%s:%d> failed <%s>", 
                              host_ip, 
                              host_port,
                              strerror(errno));
            return ErrorCode::kFailure;
        }
    }

    agent_thrd_ = std::thread(&UcEndpoint::RdmaDHAgent, this);
    return ErrorCode::kSuccess;
}

void UcEndpoint::Exit()
{
    if (-1 != udp_socket_)
    {
        close(udp_socket_);
        udp_socket_ = -1;
    }

    if (agent_thrd_.joinable())
    {
        agent_thrd_.join();
    }

    {
        std::lock_guard<std::mutex> _(requesting_map_lock_);
        for (auto& requeting_ah : requesting_ah_map_)
        {
            delete requeting_ah.second;
        }
        requesting_ah_map_.clear();
    }

    impl::EndpointBase::Exit();
}

void UcEndpoint::RdmaDHAgent()
{
    int32_t epoll_fd = epoll_create1(0);
    assert(-1 != epoll_fd);

    struct epoll_event epoll_ev;
    epoll_ev.events = EPOLLIN;
    epoll_ev.data.fd = epoll_fd;

    __attribute__((unused)) const auto ep_res
        = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, udp_socket_, &epoll_ev);
    assert(-1 != ep_res);

    static constexpr int32_t kEpollWaitMilli = 100;
    static constexpr uint32_t kRecvBufferSize = 1024;

    char recv_buffer[kRecvBufferSize] = { 0 };
    char* const buffer = recv_buffer;

    struct sockaddr_in sa_recvfrom;
    bzero(&sa_recvfrom, 0);

    socklen_t sock_len = sizeof(struct sockaddr_in);

    struct DhResponse dh_response;
    struct rdma_cm_id* temp_id = nullptr;

    do 
    {
        if (ADK_UNLIKELY(1 != epoll_wait(epoll_fd, &epoll_ev, 1, kEpollWaitMilli)))
        {
            std::lock_guard<std::mutex> _(requesting_map_lock_);
            for (auto& dh_pair : requesting_ah_map_)
            {
                DestHandler* const dest_handler = dh_pair.second;

                struct DhRequest dh_request;
                dh_request.rpc_sqn = dh_pair.first;

                const auto sendto_res = sendto(udp_socket_, 
                                               &dh_request, 
                                               sizeof(struct DhRequest), 
                                               0, 
                                               dest_handler->dest_addr(),
                                               sizeof(struct sockaddr_in));

                PRINT_DEBUG("Cma id<" << cma_id_ << 
                            "> send rdma destination address request<" << dh_request.rpc_sqn << 
                            "> to <" << dest_handler->dest_ip << 
                            ":" << dest_handler->dest_port << ">");

                if (ADK_UNLIKELY((-1 == sendto_res) && (EAGAIN != errno)))
                {
                    SET_LAST_TF_ERROR("send message failed, error info <%s>", 
                                      strerror(errno));
                    goto exit;
                }

                assert(sizeof(struct DhRequest) == sendto_res);
            }
        }

        const auto recv_len = recvfrom(udp_socket_, 
                                       recv_buffer, 
                                       kRecvBufferSize, 
                                       0, 
                                       (struct sockaddr*)(&sa_recvfrom),
                                       &sock_len);

        if (ADK_UNLIKELY(recv_len < 0))
        {
            if (EAGAIN == errno)
            {
                continue;
            }
            else
            {
                break;
            }
        }

        assert(recv_len == ((DhMsgHeader*)buffer)->length);

        switch (((DhMsgHeader*)buffer)->operate_type)
        {
        case DhOpType::kRequestDh:
            {
                PRINT_DEBUG("recv address handler request from <" 
                            << inet_ntoa(sa_recvfrom.sin_addr) << ":" 
                            << ntohs(sa_recvfrom.sin_port) << "> with request sqn <"
                            << ((DhRequest*)buffer)->rpc_sqn << ">");

                dh_response.ctx_sqn = ((DhRequest*)buffer)->rpc_sqn;
                dh_response.qpn = cma_id_->qp->qp_num;

                const auto sendto_res = sendto(
                    udp_socket_, 
                    &dh_response,
                    sizeof(struct DhResponse),
                    0,
                    (struct sockaddr*)(&sa_recvfrom),
                    sock_len);
                if (ADK_UNLIKELY((-1 == sendto_res) && (EAGAIN != errno)))
                {
                    SET_LAST_TF_ERROR("send message failed, error info <%s>", 
                                      strerror(errno));
                    goto exit;
                }

                assert(sizeof(struct DhResponse) == sendto_res);
            }
            break;
        case DhOpType::kDhOpRespAh:
            {
            PRINT_DEBUG("recv address handler response from <" 
                        << inet_ntoa(sa_recvfrom.sin_addr) << ":"
                        << ntohs(sa_recvfrom.sin_port) << "> with request sqn <"
                        << ((DhRequest*)buffer)->rpc_sqn << ">");

                DestHandler* dest_handler = nullptr;
                {
                    std::lock_guard<std::mutex> _(requesting_map_lock_);
                    const auto iter
                        = requesting_ah_map_.find(((DhRequest*)buffer)->rpc_sqn);
                    if (requesting_ah_map_.end() != iter)
                    {
                        dest_handler = iter->second;
                        requesting_ah_map_.erase(iter);
                    }
                    else
                    {
                        break;
                    }
                }

                assert(dest_handler);
                if (DestHandler::Status::kIniting == dest_handler->status)
                {
                    if (ADK_UNLIKELY(0 != s_rdma_verbs.rdma_create_id(
                        nullptr, 
                        &temp_id, 
                        nullptr, 
                        cma_id_->ps)))
                    {
                        dest_handler->status = DestHandler::Status::kError;
                        goto exit;
                    }

                    struct sockaddr* sa_local_ptr = rdma_get_local_addr(cma_id_);
                    if (AF_INET == sa_local_ptr->sa_family)
                    {
                        struct sockaddr_in sa_local;
                        memcpy(&sa_local, sa_local_ptr, sizeof(struct sockaddr_in));

                        sa_local.sin_port = 0;
                        if (ADK_UNLIKELY(0 != s_rdma_verbs.rdma_resolve_addr(
                            temp_id, 
                            (struct sockaddr*)(&sa_local), 
                            (struct sockaddr*)(&sa_recvfrom), 
                            2000)))
                        {
                            dest_handler->status = DestHandler::Status::kError;
                            SET_LAST_TF_ERROR("RDMA resolve address <%s> to <%s> failed", 
                                inet_ntoa(sa_local.sin_addr), 
                                inet_ntoa(sa_recvfrom.sin_addr));
                            goto exit;
                        }
                    }
                    else if (AF_INET6 == sa_local_ptr->sa_family)
                    {
                        struct sockaddr_in6 sa6_local;
                        memcpy(&sa6_local, sa_local_ptr, sizeof(struct sockaddr_in6));

                        sa6_local.sin6_port = 0;
                        if (ADK_UNLIKELY(0 != s_rdma_verbs.rdma_resolve_addr(
                            temp_id, 
                            (struct sockaddr*)(&sa6_local),
                            (struct sockaddr*)(&sa_recvfrom),
                            2000)))
                        {
                            dest_handler->status = DestHandler::Status::kError;

                            char addr_str[INET6_ADDRSTRLEN];
                            inet_ntop(AF_INET6, &sa6_local.sin6_addr, addr_str, INET6_ADDRSTRLEN);
                            SET_LAST_TF_ERROR("RDMA resolve address <%s> to <%s> failed", 
                                              addr_str, 
                                              inet_ntoa(sa_recvfrom.sin_addr));
                            goto exit;
                        }
                    }
                    else
                    {
                        assert(false);
                    }

                    if (ADK_UNLIKELY(0 != s_rdma_verbs.rdma_resolve_route(temp_id, 2000)))
                    {
                        dest_handler->status = DestHandler::Status::kError;
                        SET_LAST_ERROR("RDMA resolve route failed");
                        goto exit;
                    }

                    memset(&(dest_handler->ah_attr), 0, sizeof(struct ibv_ah_attr));
                    if (temp_id->route.path_rec->hop_limit > 1)
                    {
                        dest_handler->ah_attr.is_global = 1;
                        dest_handler->ah_attr.grh.dgid
                            = temp_id->route.path_rec->dgid;
                        dest_handler->ah_attr.grh.flow_label
                            = be32toh(temp_id->route.path_rec->flow_label);
#if 1
                        dest_handler->ah_attr.grh.sgid_index = context_->sgid_index();
#else
                        dest_handler->ah_attr.grh.sgid_index = [=]()->uint8_t {
                            union ibv_gid gid;
                            for (uint8_t index = 0; index < 16; ++index)
                            {
                                s_ibv_verbs.ibv_query_gid(cma_id_->verbs, 
                                                          cma_id_->port_num, 
                                                          index, 
                                                          &gid);
                                if (0 == memcmp(&(temp_id->route.path_rec->sgid), 
                                                &gid, 
                                                sizeof(union ibv_gid)))
                                {
                                    return index;
                                }
                            }
                            return 0;
                        }();
#endif
                        dest_handler->ah_attr.grh.hop_limit
                            = temp_id->route.path_rec->hop_limit;
                        dest_handler->ah_attr.grh.traffic_class
                            = temp_id->route.path_rec->traffic_class;
                    }

                    dest_handler->ah_attr.dlid
                        = be16toh(temp_id->route.path_rec->dlid);
                    dest_handler->ah_attr.sl = temp_id->route.path_rec->sl;
                    dest_handler->ah_attr.src_path_bits = [=]()->uint8_t {
                        uint8_t path_bits = 0x7f;

                        struct ibv_port_attr port_attr;

                        /* For compatibility when running with old libibverbs */
                        port_attr.link_layer = IBV_LINK_LAYER_UNSPECIFIED;
                        port_attr.reserved = 0;
                        if (0 == s_ibv_verbs.ibv_query_port_ex(cma_id_->verbs, 
                                                               cma_id_->port_num,
                                                               &port_attr))
                        {
                            path_bits = (uint8_t)((1 << port_attr.lmc) - 1);
                        }

                        return be16toh(temp_id->route.path_rec->slid) & path_bits;
                    }();

                    dest_handler->ah_attr.static_rate = temp_id->route.path_rec->rate;
                    dest_handler->ah_attr.port_num = temp_id->port_num;

                    dest_handler->ah = s_ibv_verbs.ibv_create_ah(
                        cma_id_->pd, 
                        &(dest_handler->ah_attr));
                    if (nullptr != dest_handler->ah)
                    {
                        dest_handler->remote_qkey = RDMA_UDP_QKEY;
                        dest_handler->remote_qpn = ((DhResponse*)buffer)->qpn;
                        ADK_BARRIER();
                        dest_handler->status = DestHandler::Status::kNormal;
                    }
                    else
                    {
                        ADK_BARRIER();
                        dest_handler->status = DestHandler::Status::kError;
                    }

                    if (nullptr != temp_id)
                    {
                        s_rdma_verbs.rdma_destroy_id(temp_id);
                        temp_id = nullptr;
                    }
                }
            }
            break;
        default:
            ;
        }
    } while (true);

exit:
    if (nullptr != temp_id)
    {
        s_rdma_verbs.rdma_destroy_id(temp_id);
        temp_id = nullptr;
    }

    close(epoll_fd);

    PRINT_DEBUG("agent thread exit");
}

RawPktEndpoint::RawPktEndpoint()
{
    sk_tool_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    memset(nic_mac_, 0, sizeof(nic_mac_));

    active_mtu_ = 0;
    max_inline_data_ = 0;

    cma_id_ = nullptr;
    pd_ = nullptr;

    tx_message_pool_ = nullptr;
    tx_entry_size_ = 0;
    tx_entry_node_size_ = 0;
    tx_msg_counter_ = 0;

    bzero(&send_wr_, sizeof(struct ibv_send_wr));
    send_cq_ = nullptr;
    send_mr_ = nullptr;
    send_mem_ = nullptr;

    memset(&send_wcs_, 0, sizeof(struct ibv_wc) * constant::kMaxTxRecycleSize);

    rx_entry_size_ = 0;
    rx_entry_node_size_ = 0;
    eth_flow_ = nullptr;
    recv_cq_ = nullptr;
    recv_mr_ = nullptr;
    recv_mem_ = nullptr;

    memset(&recv_wcs_, 0, sizeof(struct ibv_wc) * constant::kMaxRxBatchSize);
}

RawPktEndpoint::~RawPktEndpoint()
{
    close(sk_tool_);
    Exit();
}

RawPktEndpoint* RawPktEndpoint::Create(const std::string& host_ip, uint16_t host_port, RawPktFlow flow)
{
    if (ADK_UNLIKELY(!InitVerbsLib()))
    {
        SET_LAST_ERROR("load rdma library failed");
        return nullptr;
    }

    std::string nic_name;
    RawPktEndpoint* raw_endpoint = nullptr;
    std::map<std::string, std::array<char, 6>> host_mac_map;

    struct ifaddrs* if_infos;
    getifaddrs(&if_infos);

    for (auto if_node = if_infos; if_node != nullptr; if_node = if_node->ifa_next)
    {
        if (if_node->ifa_addr && (if_node->ifa_flags & IFF_UP))
        {
            if (AF_INET == if_node->ifa_addr->sa_family)
            {
                auto* const sa_if = (struct sockaddr_in*)(if_node->ifa_addr);
                if (host_ip == inet_ntoa(sa_if->sin_addr))
                {
                    nic_name = if_node->ifa_name;
                    if (host_mac_map.end() != host_mac_map.find(nic_name))
                    {
                        break;
                    }
                }
            }
            else if (AF_PACKET == if_node->ifa_addr->sa_family)
            {
                auto* const sa_if = (struct sockaddr_ll*)(if_node->ifa_addr);
                auto& mac = host_mac_map[if_node->ifa_name];
                memcpy(mac.data(), sa_if->sll_addr, 6);
                if (nic_name == if_node->ifa_name)
                {
                    break;
                }
            }
        }
    }

    const auto mac_iter = host_mac_map.find(nic_name);
    if (ADK_UNLIKELY(host_mac_map.end() == mac_iter))
    {
        goto exit;
    }

    raw_endpoint = new RawPktEndpoint;
    raw_endpoint->host_ip_ = host_ip;
    raw_endpoint->host_port_ = host_port;
    memcpy(raw_endpoint->nic_mac_, mac_iter->second.data(), 6);
    raw_endpoint->nic_name_ = nic_name;

    if (ADK_UNLIKELY(ErrorCode::kSuccess != raw_endpoint->Init(flow)))
    {
        raw_endpoint->Exit();
        delete raw_endpoint;
        raw_endpoint = nullptr;
    }

exit:
    freeifaddrs(if_infos);
    return raw_endpoint;
}

RawDestHandler* RawPktEndpoint::CreateDestHandler(const std::string& dest_ip, uint16_t dest_port)
{
    struct arpreq arp_req;
    struct sockaddr_in* sa_in = (struct sockaddr_in*)(&arp_req.arp_pa);
    bzero(&arp_req, sizeof(struct arpreq));

    sa_in->sin_family = AF_INET;
    sa_in->sin_addr.s_addr = inet_addr(dest_ip.c_str());
    strncpy(arp_req.arp_dev, nic_name_.c_str(), IFNAMSIZ - 1);

    if (0 != ioctl(sk_tool_, SIOCGARP, &arp_req))
    {
        SET_LAST_TF_ERROR("Get ARP table failed dev <%s> address <%s>", nic_name_.c_str(), dest_ip.c_str());
        return nullptr;
    }

    if (!(arp_req.arp_flags & ATF_COM))
    {
        SET_LAST_TF_ERROR("MAC: Not in the ARP cache, dev <%s> address <%s>", nic_name_.c_str(), dest_ip.c_str());
        return nullptr;
    }

    RawDestHandler* dest_handler = new RawDestHandler;
    memcpy(dest_handler->message_header_.eth_header.src_mac, nic_mac_, 6);
    memcpy(dest_handler->message_header_.eth_header.dst_mac, arp_req.arp_ha.sa_data, 6);
    dest_handler->message_header_.eth_header.protocol = 0x0008;
    dest_handler->message_header_.ipv4_header.header_ver_len = kHeaderVerLen;
    dest_handler->message_header_.ipv4_header.service_type = kServiceType;
    dest_handler->message_header_.ipv4_header.fragment_offset = kFragmentOffset;
    dest_handler->message_header_.ipv4_header.time_to_live = kTimeToLive;
    dest_handler->message_header_.ipv4_header.protocol = kProtocolUdp;
    dest_handler->message_header_.ipv4_header.src_ip = inet_addr(host_ip_.c_str());
    dest_handler->message_header_.ipv4_header.dst_ip = inet_addr(dest_ip.c_str());
    dest_handler->message_header_.udp_header.src_port = htons(host_port_);
    dest_handler->message_header_.udp_header.dst_port = htons(dest_port);
    dest_handler->ip_id_generic_ = (uint16_t)rand();
    return dest_handler;
}

std::string RawPktEndpoint::GetHWInfo()
{
    struct ibv_device_attr device_attr;

    assert(cma_id_->verbs);
    if (ADK_UNLIKELY(0 != s_ibv_verbs.ibv_query_device(cma_id_->verbs, &device_attr)))
    {
        SET_LAST_ERROR("ibv query device failed");
        return std::string();
    }

    std::string hw_info;
    hw_info.resize(20);
    sprintf(const_cast<char*>(hw_info.data()), 
            "%02x%02x:%02x%02x:%02x%02x:%02x%02x", 
            (unsigned)((device_attr.sys_image_guid >> 0) & 0xff),
            (unsigned)((device_attr.sys_image_guid >> 8) & 0xff),
            (unsigned)((device_attr.sys_image_guid >> 16) & 0xff),
            (unsigned)((device_attr.sys_image_guid >> 24) & 0xff),
            (unsigned)((device_attr.sys_image_guid >> 32) & 0xff),
            (unsigned)((device_attr.sys_image_guid >> 40) & 0xff),
            (unsigned)((device_attr.sys_image_guid >> 48) & 0xff),
            (unsigned)((device_attr.sys_image_guid >> 56) & 0xff));

    return hw_info;
}

int32_t RawPktEndpoint::Init(RawPktFlow flow)
{
    if (0 != s_rdma_verbs.rdma_create_id(nullptr, &cma_id_, nullptr, rdma_port_space::RDMA_PS_UDP))
    {
        SET_LAST_ERROR("RDMA create id failed");
        return ErrorCode::kFailure;
    }

    cma_id_->qp = nullptr;

    struct sockaddr_storage sa_host;
    bzero(&sa_host, sizeof(struct sockaddr_storage));

    struct addrinfo* ai_dst;
    getaddrinfo(host_ip_.c_str(), nullptr, nullptr, &ai_dst);
    memcpy(&sa_host, ai_dst->ai_addr, ai_dst->ai_addrlen);
    freeaddrinfo(ai_dst);

    if (ADK_UNLIKELY(0 != s_rdma_verbs.rdma_bind_addr(cma_id_, (struct sockaddr*)&sa_host)))
    {
        SET_LAST_TF_ERROR("RDMA bind address <%s> failed", host_ip_.c_str());
        return ErrorCode::kFailure;
    }

    if (ADK_UNLIKELY(nullptr == cma_id_->verbs))
    {
        SET_LAST_TF_ERROR("interface <%s> is not suporrt rdma", host_ip_.c_str());
        return ErrorCode::kFailure;
    }

    struct ibv_port_attr port_attr;
    port_attr.link_layer = IBV_LINK_LAYER_UNSPECIFIED;
    port_attr.reserved = 0;
    if (ADK_UNLIKELY(0 != s_ibv_verbs.ibv_query_port_ex(cma_id_->verbs,
                                                        cma_id_->port_num,
                                                        &port_attr)))
    {
        SET_LAST_ERROR("ibv_query_port failed");
        return ErrorCode::kFailure;
    }

    active_mtu_ = 1 << (port_attr.active_mtu + 7);
    PRINT_DEBUG("active mtu <" << active_mtu_ << ">");

    pd_ = s_ibv_verbs.ibv_alloc_pd(cma_id_->verbs);
    if (ADK_UNLIKELY(nullptr == pd_))
    {
        SET_LAST_ERROR("unable to allocate PD");
        return ErrorCode::kFailure;
    }

    send_cq_ = s_ibv_verbs.ibv_create_cq(cma_id_->verbs,
                                         constant::kTxQueuePairDepth,
                                         this,
                                         nullptr,
                                         0);
    if (ADK_UNLIKELY(nullptr == send_cq_))
    {
        SET_LAST_ERROR("unable to create tx CQ");
        return ErrorCode::kFailure;
    }

    recv_cq_ = s_ibv_verbs.ibv_create_cq(cma_id_->verbs,
                                         constant::kRxQueuePairDepth,
                                         this, 
                                         nullptr, 
                                         0);
    if (ADK_UNLIKELY(nullptr == recv_cq_))
    {
        SET_LAST_ERROR("unable to create rx CQ");
        return ErrorCode::kFailure;
    }

    struct ibv_qp_init_attr qp_init_attr;
    qp_init_attr.qp_context = this;
    qp_init_attr.send_cq = send_cq_;
    qp_init_attr.recv_cq = recv_cq_;
    qp_init_attr.srq = nullptr;

    qp_init_attr.cap.max_send_wr = constant::kTxQueuePairDepth;
    qp_init_attr.cap.max_recv_wr = constant::kRxQueuePairDepth;
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;
    qp_init_attr.cap.max_inline_data = 0;

    qp_init_attr.qp_type = IBV_QPT_RAW_PACKET;
    qp_init_attr.sq_sig_all = 1;
    qp_init_attr.xrc_domain = nullptr;

    cma_id_->qp = s_ibv_verbs.ibv_create_qp(pd_, &qp_init_attr);
    if (nullptr == cma_id_->qp)
    {
        SET_LAST_TF_ERROR("unable to create QP, error information <%s>", strerror(errno));
        return ErrorCode::kFailure;
    }

    PRINT_DEBUG("cma_id <" << (void*)cma_id_ <<
                "> | port_num <" << (uint32_t)(cma_id_->port_num) <<
                "> | qp_num <" << cma_id_->qp->qp_num << ">");

    struct ibv_qp_attr qp_attr;
    if (ADK_UNLIKELY(0 != s_ibv_verbs.ibv_query_qp(cma_id_->qp,
                                                   &qp_attr,
                                                   IBV_QP_CAP,
                                                   &qp_init_attr)))
    {
        SET_LAST_ERROR("query qp attribute <IBV_QP_CAP>");
        return ErrorCode::kFailure;
    }

    max_inline_data_ = qp_init_attr.cap.max_inline_data;
    PRINT_DEBUG("max inline data <" << max_inline_data_ << ">");

    memset(&qp_attr, 0, sizeof(qp_attr));

    qp_attr.qp_state = IBV_QPS_INIT;
    qp_attr.port_num = cma_id_->port_num;
    if (ADK_UNLIKELY(0 != s_ibv_verbs.ibv_modify_qp(cma_id_->qp, 
                                                    &qp_attr, 
                                                    IBV_QP_STATE | IBV_QP_PORT)))
    {
        SET_LAST_TF_ERROR("failed modify qp to init, error info <%s>", strerror(errno));
        return ErrorCode::kFailure;
    }

    memset(&qp_attr, 0, sizeof(qp_attr));
    qp_attr.qp_state = IBV_QPS_RTR;
    if (ADK_UNLIKELY(0 != s_ibv_verbs.ibv_modify_qp(cma_id_->qp,
                                                    &qp_attr,
                                                    IBV_QP_STATE)))
    {
        SET_LAST_TF_ERROR("failed modify qp ready to receive, error info <%s>", strerror(errno));
        return ErrorCode::kFailure;
    }

    memset(&qp_attr, 0, sizeof(qp_attr));
    qp_attr.qp_state = IBV_QPS_RTS;
    if (ADK_UNLIKELY(0 != s_ibv_verbs.ibv_modify_qp(cma_id_->qp,
                                                    &qp_attr,
                                                    IBV_QP_STATE)))
    {
        SET_LAST_TF_ERROR("failed modify qp ready to send, error info <%s>", strerror(errno));
        return ErrorCode::kFailure;
    }

    tx_entry_size_ = active_mtu_;
    tx_entry_node_size_ = ADK_ROUND_UP(sizeof(struct TxNodeEntry) + tx_entry_size_,
                                       ADK_CACHE_LINE_SIZE);

    const auto total_tx_size
        = ADK_ROUND_UP(tx_entry_node_size_ * constant::kTxMessagePoolSize, ADK_PAGE_SIZE);

    send_mem_ = memalign(ADK_PAGE_SIZE, total_tx_size);
    if (ADK_UNLIKELY(nullptr == send_mem_))
    {
        SET_LAST_ERROR("allocate tx memory");
        return ErrorCode::kFailure;
    }

    send_mr_ = s_ibv_verbs.ibv_reg_mr(pd_, send_mem_, total_tx_size, 0);
    if (ADK_UNLIKELY(nullptr == send_mr_))
    {
        SET_LAST_ERROR("register tx memory region failed");
        return ErrorCode::kFailure;
    }

    tx_message_pool_ = TxMessagePool::Create("tx message pool",
                                             constant::kTxMessagePoolSize);
    if (ADK_UNLIKELY(nullptr == tx_message_pool_))
    {
        SET_LAST_ERROR("create tx message pool failed");
        return ErrorCode::kFailure;
    }

    char* tx_mem = (char*)(send_mem_);
    for (uint32_t index = 0; index < constant::kTxMessagePoolSize; ++index)
    {
        TxRawEntry* node_entry = (TxRawEntry*)tx_mem;
        new ((void*)node_entry) TxRawEntry(send_mr_->lkey);
        ADK_UNUSED const auto ec = tx_message_pool_->TryPush(node_entry);
        assert(ErrorCode::kSuccess == ec);
        tx_mem += tx_entry_node_size_;
    }

    send_wr_.next = nullptr;
    send_wr_.opcode = IBV_WR_SEND;
    send_wr_.send_flags = 0;
    send_wr_.num_sge = 1;
    send_wr_.imm_data = 0;

    struct ifreq ifr;
    ifr.ifr_mtu = 0;
    strcpy(ifr.ifr_name, nic_name_.c_str());
    ioctl(sk_tool_, SIOCGIFMTU, (caddr_t)&ifr);
    rx_entry_size_ = std::max<uint32_t>(ifr.ifr_mtu, active_mtu_) + 64;
    rx_entry_node_size_ = ADK_ROUND_UP(sizeof(struct RxNodeEntry) + rx_entry_size_,
                                       ADK_CACHE_LINE_SIZE);

    const auto total_rx_size = ADK_ROUND_UP(rx_entry_node_size_ * constant::kRxQueuePairDepth, ADK_PAGE_SIZE);

    recv_mem_ = memalign(ADK_PAGE_SIZE, total_rx_size);
    if (ADK_UNLIKELY(nullptr == recv_mem_))
    {
        SET_LAST_ERROR("allocate rx memory failed");
        return ErrorCode::kFailure;
    }

    recv_mr_ = s_ibv_verbs.ibv_reg_mr(pd_,
                                      recv_mem_,
                                      total_rx_size,
                                      IBV_ACCESS_LOCAL_WRITE);
    if (ADK_UNLIKELY(nullptr == recv_mr_))
    {
        SET_LAST_ERROR("register rx memory region failed");
        return ErrorCode::kFailure;
    }

    struct ibv_recv_wr *bad_wr;
    char* rx_mem = (char*)(recv_mem_);
    for (uint32_t index = 0; index < constant::kRxQueuePairDepth; ++index)
    {
        RxNodeEntry* node_entry = (RxNodeEntry*)rx_mem;
        new ((void*)node_entry) RxNodeEntry(recv_mr_->lkey);
        node_entry->set_buffer_size(rx_entry_size_);
        ADK_UNUSED const auto ec = ibv_post_recv(cma_id_->qp, 
                                                 &(node_entry->recv_wr), 
                                                 &bad_wr);
        assert(0 == ec);
        rx_mem += rx_entry_node_size_;
    }

    struct raw_eth_flow_attr
    {
        struct ibv_flow_attr attr;
        struct ibv_flow_spec_eth spec_eth;
    } __attribute__((packed)) flow_attr;

    flow_attr.attr.comp_mask = 0;
    if (RawPktFlow::kDstToThis == flow)
    {
        flow_attr.attr.type = IBV_FLOW_ATTR_NORMAL;
    }
    else if (RawPktFlow::kSniffer == flow)
    {
        // sniffer
        flow_attr.attr.type = (ibv_flow_attr_type)3;
    }
    else
    {
        return ErrorCode::kSuccess;
    }

    flow_attr.attr.size = sizeof(struct raw_eth_flow_attr);
    flow_attr.attr.priority = 0;
    flow_attr.attr.num_of_specs = 1;
    flow_attr.attr.port = cma_id_->port_num;
    flow_attr.attr.flags = 0;

    flow_attr.spec_eth.type = IBV_FLOW_SPEC_ETH;
    flow_attr.spec_eth.size = sizeof(struct ibv_flow_spec_eth);

    memcpy(flow_attr.spec_eth.val.dst_mac, nic_mac_, sizeof(nic_mac_));
    uint8_t val_src_mac[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    memcpy(flow_attr.spec_eth.val.src_mac, val_src_mac, sizeof(val_src_mac));
    flow_attr.spec_eth.val.ether_type = 0;
    flow_attr.spec_eth.val.vlan_tag = 0;

    uint8_t mask_dst_mac[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    memcpy(flow_attr.spec_eth.mask.dst_mac, mask_dst_mac, sizeof(mask_dst_mac));
    uint8_t mask_src_mac[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    memcpy(flow_attr.spec_eth.mask.src_mac, mask_src_mac, sizeof(mask_src_mac));
    flow_attr.spec_eth.mask.ether_type = 0;
    flow_attr.spec_eth.mask.vlan_tag = 0;

    eth_flow_ = ibv_create_flow(cma_id_->qp, &flow_attr.attr);
    if (ADK_UNLIKELY(nullptr == eth_flow_))
    {
        SET_LAST_TF_ERROR("Couldn't attach steering flow, error info <%s>", strerror(errno));
        return ErrorCode::kFailure;
    }

    return ErrorCode::kSuccess;
}

void RawPktEndpoint::Exit()
{
    if (nullptr != cma_id_)
    {
        if (nullptr != cma_id_->qp)
        {
            s_ibv_verbs.ibv_destroy_qp(cma_id_->qp);
            cma_id_->qp = nullptr;
        }

        if (nullptr != send_cq_)
        {
            s_ibv_verbs.ibv_destroy_cq(send_cq_);
            send_cq_ = nullptr;
        }

        if (nullptr != send_mr_)
        {
            s_ibv_verbs.ibv_dereg_mr(send_mr_);
            send_mr_ = nullptr;
        }

        if (nullptr != eth_flow_)
        {
            ibv_destroy_flow(eth_flow_);
            eth_flow_ = nullptr;
        }

        if (nullptr != recv_cq_)
        {
            s_ibv_verbs.ibv_destroy_cq(recv_cq_);
            recv_cq_ = nullptr;
        }

        if (nullptr != recv_mr_)
        {
            s_ibv_verbs.ibv_dereg_mr(recv_mr_);
            recv_mr_ = nullptr;
        }

        if (nullptr != pd_)
        {
            s_ibv_verbs.ibv_dealloc_pd(pd_);
            pd_ = nullptr;
        }

        s_rdma_verbs.rdma_destroy_id(cma_id_);
        cma_id_ = nullptr;
    }

    if (nullptr != send_mem_)
    {
        free(send_mem_);
        send_mem_ = nullptr;
    }

    if (nullptr != tx_message_pool_)
    {
        TxMessagePool::Delete(tx_message_pool_);
        tx_message_pool_ = nullptr;
    }

    if (nullptr != recv_mem_)
    {
        free(recv_mem_);
        recv_mem_ = nullptr;
    }
}

const char* RawPktEndpoint::last_error() const
{
    return impl::s_last_error;
}

}

}