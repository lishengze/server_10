/**
 * @brief      the definition of ami internal used message header
 * @author     zhaonan, zhaonan@archforce.com.cn
 * @note       本文件来源于AMI源码，为解决多分片功能编译失败问题
 */
#ifndef AMI_AMI_MESSAGE_H_
#define AMI_AMI_MESSAGE_H_

#include <stdint.h>

#include <adk/mem_pool.h>
#include <adk/shm_ptr.h>
#include <ami/message.h>
#include <adk/arch/generic.h>

namespace ami
{

struct ReferenceCounter
{
    uint32_t    slave_counter_rx;
    uint32_t    slave_counter_tx;
};


struct AmiMetaData
{
    typedef Message::IDType IDType;

    Message::SqnType    c_stream_sqn;
    Message::SqnType    c_topic_sqn;
    Message::SqnType    c_endpoint_sqn;
    void*               endpoint;
    IDType              endpoint_id;
    ReferenceCounter    ref_cnt;    ///< cache aligned
                                    ///< do not change the definition order
                                    ///< change the order if the definition
                                    ///< of MemoryBuffer is changed!
    void*               transport;
    IDType              transport_id;
    uint32_t            ami_flags;
    uint64_t            ami_recv_sqn;
    void*               gc;

    // egress message
    uint64_t            ami_persistent_context_0;
    uint64_t            ami_persistent_context_1;
    uint64_t            recorder_receive_msg_time_ns;

    // ami time slice clock
    uint64_t            ami_clock;
};

struct AmiMessage
{
    AmiMetaData     ami_meta_data;
    char            app_message[];

    Message* message() const noexcept
    { 
        return reinterpret_cast<Message*>(const_cast<char*>(app_message)); 
    }

    template<bool full_init = true>
    inline void reset(void* init_ep_ptr = NULL)
    {
        ami_meta_data.c_stream_sqn = 0;
        ami_meta_data.c_topic_sqn = 0;
        ami_meta_data.c_endpoint_sqn = 0;
        
        if (full_init)
        {   
            // when message is return to the RxTransport cache
            // do not init rx transport related variables
            // do this can improve performance in message receive path
            ami_meta_data.endpoint = init_ep_ptr;
            ami_meta_data.transport = NULL;
            ami_meta_data.transport_id = 0;
            ami_meta_data.endpoint_id = 0;
            ami_meta_data.gc = 0;
        }

        ami_meta_data.ref_cnt.slave_counter_rx = 0;
        ami_meta_data.ref_cnt.slave_counter_tx = 0;
        ami_meta_data.ami_flags = 0;
        ami_meta_data.ami_recv_sqn = 0;
        ami_meta_data.ami_persistent_context_0 = 0;
        ami_meta_data.ami_persistent_context_1 = 0;
        ami_meta_data.recorder_receive_msg_time_ns = 0;
    }

    void inc_slave_counter_rx()
    {
        ++ami_meta_data.ref_cnt.slave_counter_rx;
    }

    void dec_slave_counter_rx()
    {
        ADK_BARRIER();
        --ami_meta_data.ref_cnt.slave_counter_rx;
    }

    void inc_slave_counter_tx()
    {
        __sync_add_and_fetch(&ami_meta_data.ref_cnt.slave_counter_tx, 1);       // FIXME: optimization
    }

    void dec_slave_counter_tx()
    {
        __sync_sub_and_fetch(&ami_meta_data.ref_cnt.slave_counter_tx, 1);
    }

    static AmiMessage* ConvertFromShmPointer
    (const void* shm_ptr, const adk::MPManager& mp_manager) noexcept
    {
        adk::MPManager& mp_manager_not_const =
            const_cast<adk::MPManager&>(mp_manager);
        return reinterpret_cast<AmiMessage*>
            (mp_manager_not_const.ShmPtrToMemBuf
             (reinterpret_cast<adk::ShmPointer*>
              (const_cast<void*>(shm_ptr)))->data);
    }

    static AmiMessage* ConvertFromMessage(const Message* app_msg) noexcept
    {
        return ADK_CONTAINER_OF
            (const_cast<Message*>(app_msg), AmiMessage, app_message);
    }

    static AmiMessage* ConvertFromMB(const adk::MemoryBuffer* mb) noexcept
    {
        return reinterpret_cast<AmiMessage*>
            (const_cast<adk::MemoryBuffer*>(mb)->data);
    }
    
    /**
     * 如果从adk::MemoryBuffer生成的ami_message，可以使用
     * 本方法得到底层的adk::MemoryBuffer
     */
    adk::MemoryBuffer* GetLowLayerMemoryBuffer() const noexcept
    {
        return ADK_CONTAINER_OF
            (const_cast<AmiMessage*>(this), adk::MemoryBuffer, data);
    }
};


inline std::ostream& operator<<(std::ostream& os, const AmiMessage* msg)
{
    os << "shm_point_v=" << msg->GetLowLayerMemoryBuffer()->shm_ptr.value << " "
       << "endpoint_id=" << msg->ami_meta_data.endpoint_id << " "
       << "transport_id=" << msg->ami_meta_data.transport_id << " "
       << "tx_cnt=" << msg->ami_meta_data.ref_cnt.slave_counter_tx << " "
       << "rx_cnt=" << msg->ami_meta_data.ref_cnt.slave_counter_rx << " "
        ;

    return os;
}

inline std::ostream& operator<<(std::ostream& os, const AmiMessage& msg)
{ return os << &msg; }


} // ami

#endif // AMI_AMI_MESSAGE_H_
