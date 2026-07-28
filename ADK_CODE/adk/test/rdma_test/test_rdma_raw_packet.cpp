#include <infiniband/verbs.h>
#include <infiniband/verbs_exp.h>

#include <rdma/rdma_cma.h>
#include <infiniband/ib.h>

#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <iostream>

constexpr uint8_t  kPortNum = 1;
constexpr uint32_t kEntrySize = 1500;
constexpr uint32_t kRecvBufferNum = 8192;

/* The MAC we are listening to. In case your setup is via a network switch, you may need to change the MAC address to suit the network port MAC */
// #define DEST_MAC {  0x00, 0x00, 0x00, 0x00, 0x00, 0x00  }

int main() 
{
#if 0
    /* Get the list of offload capable devices */
    struct ibv_device **dev_list = ibv_get_device_list(NULL);

    if (!dev_list) 
    {
        perror("Failed to get IB devices list");
        exit(1);
    }

    /* 1. Get Device */
    /* In this example, we will use the first adapter (device) we find on the list (dev_list[0]).
       You may change the code in case you have a setup with more than one adapter installed. 
     */
    struct ibv_device *ib_dev = dev_list[0];
    if (!ib_dev)
    {
        fprintf(stderr, "IB device not found\n");
        exit(1);
    }

    /* 2. Get the device context */
    /* Get context to device. The context is a descriptor and needed for resource tracking and operations */
    struct ibv_context *context = ibv_open_device(ib_dev);
    if (!context) 
    {
        fprintf(stderr, "Couldn't get context for %s\n", ibv_get_device_name(ib_dev));
        exit(1);
    }

#else

    struct rdma_cm_id* cma_id;
    if (0 != rdma_create_id(nullptr, &cma_id, nullptr, rdma_port_space::RDMA_PS_UDP))
    {
        fprintf(stderr, "RDMA create id failed, port space <rdma_port_space::RDMA_PS_UDP>");
        return -1;
    }

    cma_id->qp = nullptr;

    struct sockaddr_storage sa_host;
    bzero(&sa_host, sizeof(struct sockaddr_storage));

    struct addrinfo* ai_dst;
    getaddrinfo("10.128.8.40", nullptr, nullptr, &ai_dst);
    memcpy(&sa_host, ai_dst->ai_addr, ai_dst->ai_addrlen);
    freeaddrinfo(ai_dst);

    if (0 != rdma_bind_addr(cma_id, (struct sockaddr*)&sa_host))
    {
        fprintf(stderr, "RDMA bind address <10.128.8.40> failed");
        return -1;
    }

    if (nullptr == cma_id->verbs)
    {
        fprintf(stderr, "nic with address <10.128.8.40> is not a rdma interface");
        return -1;
    }

    struct ibv_context *context = cma_id->verbs;
    struct ibv_device *ib_dev = context->device;

#endif

    std::cout << ib_dev->name << std::endl;
    std::cout << ib_dev->dev_name << std::endl;
    std::cout << ib_dev->dev_path << std::endl;
    std::cout << ib_dev->ibdev_path << std::endl;
    std::cout << "open device <" << ibv_get_device_name(ib_dev) << "> success" << std::endl;

    /* 3. Allocate Protection Domain */
    /* Allocate a protection domain to group memory regions (MR) and rings */
    struct ibv_pd *pd = ibv_alloc_pd(context);
    if (!pd) 
    {
        fprintf(stderr, "Couldn't allocate PD, error info: %s\n", strerror(errno));
        exit(1);
    }

    /* 4. Create Complition Queue (CQ) */
    struct ibv_cq *cq = ibv_create_cq(context, kRecvBufferNum, NULL, NULL, 0);
    if (!cq) 
    {
        fprintf(stderr, "Couldn't create CQ, error info: %s\n", strerror(errno));
        exit(1);
    }

    /* 5. Initialize QP */
    struct ibv_qp *qp;
    struct ibv_qp_init_attr qp_init_attr;
    qp_init_attr.qp_context = nullptr;
    qp_init_attr.send_cq = cq;
    qp_init_attr.recv_cq = cq;
    qp_init_attr.srq = nullptr;

    qp_init_attr.cap.max_send_wr = 0;
    qp_init_attr.cap.max_recv_wr = kRecvBufferNum;
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;
    qp_init_attr.cap.max_inline_data = 0;

    qp_init_attr.qp_type = IBV_QPT_RAW_PACKET;
    qp_init_attr.sq_sig_all = 1;
    qp_init_attr.xrc_domain = nullptr;

    /* 6. Create Queue Pair (QP) - Receive Ring */
    qp = ibv_create_qp(pd, &qp_init_attr);
    if (!qp) 
    {
        fprintf(stderr, "Couldn't create RSS QP, error info: %s\n", strerror(errno));
        exit(1);
    }

    /* 7. Initialize the QP (receive ring) and assign a port */
    struct ibv_qp_attr qp_attr;
    memset(&qp_attr, 0, sizeof(qp_attr));

    qp_attr.qp_state = IBV_QPS_INIT;
    qp_attr.port_num = 1;
    int ret = ibv_modify_qp(qp, &qp_attr, IBV_QP_STATE | IBV_QP_PORT);
    if (ret < 0) 
    {
        fprintf(stderr, "failed modify qp to init, error info: %s\n", strerror(errno));
        exit(1);
    }

    memset(&qp_attr, 0, sizeof(qp_attr));

    /* 8. Move ring state to ready to receive, this is needed in order to be able to receive packets */
    qp_attr.qp_state = IBV_QPS_RTR;
    ret = ibv_modify_qp(qp, &qp_attr, IBV_QP_STATE);
    if (ret < 0) 
    {
        fprintf(stderr, "failed modify qp to receive, error info: %s\n", strerror(errno));
        exit(1);
    }

    /* 9. Allocate Memory */
    int buf_size = kEntrySize * kRecvBufferNum; /* maximum size of data to be accessed by hardware */
    void *buf = malloc(buf_size);
    if (!buf) 
    {
        fprintf(stderr, "Couldn't allocate memory, error info: %s\n", strerror(errno));
        exit(1);
    }

    /* 10. Register the user memory so it can be accessed by the HW directly */
    struct ibv_mr *mr = ibv_reg_mr(pd, buf, buf_size, IBV_ACCESS_LOCAL_WRITE);
    if (!mr) 
    {
        fprintf(stderr, "Couldn't register mr, error info: %s\n", strerror(errno));
        exit(1);
    }

    /* 11. Attach all buffers to the ring */
    struct ibv_sge sg_entry;
    struct ibv_recv_wr wr, *bad_wr;

    /* pointer to packet buffer size and memory key of each packet buffer */
    sg_entry.length = kEntrySize;
    sg_entry.lkey = mr->lkey;

    /*
     * descriptor for receive transaction - details:
     * - how many pointers to receive buffers to use
     * - if this is a single descriptor or a list (next == NULL single)
     */
    wr.num_sge = 1;
    wr.sg_list = &sg_entry;
    wr.next = NULL;
    for (uint32_t n = 0; n < kRecvBufferNum; n++)
    {
        /* each descriptor points to max MTU size buffer */
        sg_entry.addr = (uint64_t)buf + kEntrySize * n;

        /* index of descriptor returned when packet arrives */
        wr.wr_id = n;

        /* post receive buffer to ring */
        ibv_post_recv(qp, &wr, &bad_wr);
    }

#if 0
    /* 12. Register steering rule to intercept packet to DEST_MAC and place packet in ring pointed by ->qp */
    struct raw_eth_flow_attr
    {
        struct ibv_flow_attr attr;
        struct ibv_flow_spec_eth spec_eth;
    } __attribute__((packed)) flow_attr;

    flow_attr.attr.comp_mask = 0;
    flow_attr.attr.type = (ibv_flow_attr_type)3/*IBV_FLOW_ATTR_NORMAL*/;
    flow_attr.attr.size = sizeof(struct raw_eth_flow_attr);
    flow_attr.attr.priority = 0;
    flow_attr.attr.num_of_specs = 1;
    flow_attr.attr.port = kPortNum;
    flow_attr.attr.flags = 0;

    flow_attr.spec_eth.type = IBV_FLOW_SPEC_ETH;
    flow_attr.spec_eth.size = sizeof(struct ibv_flow_spec_eth);

    // uint8_t val_dst_mac[] = { 0x01, 0x80, 0xC2, 0x00, 0x00, 0x00 };
    uint8_t val_dst_mac[] = { 0x7C, 0xFE, 0x90, 0x95, 0x50, 0xF0 };
    // uint8_t val_dst_mac[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    // uint8_t val_dst_mac[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    memcpy(flow_attr.spec_eth.val.dst_mac, val_dst_mac, sizeof(val_dst_mac));

    uint8_t val_src_mac[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    // uint8_t val_src_mac[] = { 0xB4, 0x43, 0x26, 0xAF, 0xAA, 0x72 };
    memcpy(flow_attr.spec_eth.val.src_mac, val_src_mac, sizeof(val_src_mac));

    flow_attr.spec_eth.val.ether_type = 0;
    flow_attr.spec_eth.val.vlan_tag = 0;

    uint8_t mask_dst_mac[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    memcpy(flow_attr.spec_eth.mask.dst_mac, mask_dst_mac, sizeof(mask_dst_mac));
    uint8_t mask_src_mac[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    memcpy(flow_attr.spec_eth.mask.src_mac, mask_src_mac, sizeof(mask_src_mac));

    flow_attr.spec_eth.mask.ether_type = 0;
    flow_attr.spec_eth.mask.vlan_tag = 0;
#else
    struct raw_ipv4_flow_attr
    {
        struct ibv_flow_attr attr;
        struct ibv_flow_spec_eth spec_eth;
        struct ibv_flow_spec_ipv4 spec_ipv4;
    } __attribute__((packed)) flow_attr;

    flow_attr.attr.comp_mask = 0;
    flow_attr.attr.type = IBV_FLOW_ATTR_NORMAL;
    flow_attr.attr.size = sizeof(struct raw_ipv4_flow_attr);
    flow_attr.attr.priority = 0;
    flow_attr.attr.num_of_specs = 1;
    flow_attr.attr.port = kPortNum;
    flow_attr.attr.flags = 0;

    flow_attr.spec_eth.type = IBV_FLOW_SPEC_ETH;
    flow_attr.spec_eth.size = sizeof(struct ibv_flow_spec_eth);

    // uint8_t val_dst_mac[] = { 0x01, 0x80, 0xC2, 0x00, 0x00, 0x00 };
    uint8_t val_dst_mac[] = { 0x7C, 0xFE, 0x90, 0x95, 0x50, 0xF0 };
    // uint8_t val_dst_mac[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    memcpy(flow_attr.spec_eth.val.dst_mac, val_dst_mac, sizeof(val_dst_mac));

    // uint8_t val_src_mac[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint8_t val_src_mac[] = { 0xB4, 0x43, 0x26, 0xAF, 0xAA, 0x73 };
    memcpy(flow_attr.spec_eth.val.src_mac, val_src_mac, sizeof(val_src_mac));

    flow_attr.spec_eth.val.ether_type = 0;
    flow_attr.spec_eth.val.vlan_tag = 0;

	uint8_t mask_dst_mac[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    memcpy(flow_attr.spec_eth.mask.dst_mac, mask_dst_mac, sizeof(mask_dst_mac));
    uint8_t mask_src_mac[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    memcpy(flow_attr.spec_eth.mask.src_mac, mask_src_mac, sizeof(mask_src_mac));

    flow_attr.spec_eth.mask.ether_type = 0;
    flow_attr.spec_eth.mask.vlan_tag = 0;

    flow_attr.spec_ipv4.type = IBV_FLOW_SPEC_IPV4;
    flow_attr.spec_ipv4.size = sizeof(struct ibv_flow_spec_ipv4);

    flow_attr.spec_ipv4.val.src_ip = 0x2800800A/*0x0A800028*/;
    flow_attr.spec_ipv4.val.dst_ip = 0;
    flow_attr.spec_ipv4.mask.src_ip = 0xFFFFFFFF;
    flow_attr.spec_ipv4.mask.dst_ip = 0;
#endif
    /* 13. Create steering rule */
    struct ibv_flow *eth_flow = ibv_create_flow(qp, &flow_attr.attr);
    if (!eth_flow)
    {
        fprintf(stderr, "Couldn't attach steering flow, error info: %s\n", strerror(errno));
        exit(1);
    }

    /* 14. Wait for CQ event upon message received, and print a message */
    int msgs_completed;
    struct ibv_wc wc;

    while (1) 
    {
        /* wait for completion */
        msgs_completed = ibv_poll_cq(cq, 1, &wc);
        if (msgs_completed > 0)
        {
            /**
             * completion includes:
             * -status of descriptor
             * -index of descriptor completing
             * -size of the incoming packets
             */
            printf("message %ld received size %d\n", wc.wr_id, wc.byte_len);

            sg_entry.addr = (uint64_t)buf + wc.wr_id * kEntrySize;
            for (int32_t index = 0; index < wc.byte_len; ++index)
            {
                printf("%02x", (uint32_t)(*reinterpret_cast<unsigned char*>(sg_entry.addr + index)));
                if (!((index + 1) & 1))
                {
                    std::cout << " ";
                }

                if (0 == (index + 1) % 16)
                {
                    std::cout << std::endl;
                }
            }

            std::cout << std::endl;

            wr.wr_id = wc.wr_id;

            /* after processed need to post back buffer */
            ibv_post_recv(qp, &wr, &bad_wr);
        }
        else if (msgs_completed < 0)
        {
            printf("Polling error\n");
            exit(1);
        }
    }

    printf("We are done\n");
    return 0;
}