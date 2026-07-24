#include <string>

namespace recorder_data
{

enum class file_option_type
{
    kNoOpt = 0,
    kUseCrc = 1,
};

struct OrdinalIndex
{
    char file_pos[16];
};

struct RecordFileHdr
{
    uint8_t header_len = 0;
    uint8_t version = 0;
    uint16_t file_opt = 0;
};

struct MessageHeader
{
    uint32_t option_offset = 0;
    uint32_t stream_id = 0;
    uint64_t ancestor_id = 0;
};

struct RecorderMsgProp
{
    uint32_t msg_prop = 0;
};

struct ExMessageHeader
{
    RecorderMsgProp msg_prop;
    uint32_t pad1 = 0;
    uint64_t c_stream_sqn = 0;
    uint64_t c_topic_sqn = 0;
    uint64_t c_endpoint_sqn = 0;
    uint64_t ami_user_context_0 = 0;
    uint64_t ami_user_context_1 = 0;
    uint64_t recorder_receive_msg_time_ns = 0;
};

struct RecorderMessage
{
    uint64_t stream_sqn = 0;
    uint64_t topic_sqn = 0;
    uint32_t app_data_len = 0;
    ExMessageHeader ex_message_header;
    MessageHeader message_header;
    char app_data_begin[];

    char* data()
    {
        return app_data_begin;
    }
};

struct RxRecorderMessage
{
    uint32_t endpoint_id = 0;
    uint32_t transport_id = 0;
    RecorderMessage recorder_message;
};

struct TxRecorderMessage
{
    RecorderMessage recorder_message;
};

};