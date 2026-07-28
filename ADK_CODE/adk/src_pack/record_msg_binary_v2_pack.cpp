#include <adk/record_msg_binary_v2.h>
#include <adk_pack/record_msg_binary_v2.h>
namespace adk
{

using RecordMsgBinaryV2Impl = adk_impl::RecordMsgBinaryV2;

RecordMsgBinaryV2::RecordMsgBinaryV2(uint8_t queue_num, uint32_t queue_size)
{
    impl_ = (void*)(new RecordMsgBinaryV2Impl(queue_num, queue_size));
}

RecordMsgBinaryV2::~RecordMsgBinaryV2()
{
    delete reinterpret_cast<RecordMsgBinaryV2Impl*>(impl_);
}

void RecordMsgBinaryV2::SetSerializeFunc(SerializeMsgFunc serialize_func)
{
    RecordMsgBinaryV2Impl::SerializeMsgFunc* ptr_serialize_func = (RecordMsgBinaryV2Impl::SerializeMsgFunc*)(&serialize_func);
    reinterpret_cast<RecordMsgBinaryV2Impl*>(impl_)->SetSerializeFunc(*ptr_serialize_func);
}

bool RecordMsgBinaryV2::Init(const std::string& record_file_name, std::string* err_msg)
{
    return reinterpret_cast<RecordMsgBinaryV2Impl*>(impl_)->Init(record_file_name, err_msg);
}

ErrorCode RecordMsgBinaryV2::Start(std::string* err_msg)
{
    auto ret = reinterpret_cast<RecordMsgBinaryV2Impl*>(impl_)->Start(err_msg);
    return static_cast<ErrorCode>(ret);
}

void RecordMsgBinaryV2::Stop()
{
    reinterpret_cast<RecordMsgBinaryV2Impl*>(impl_)->Stop();
}

ErrorCode RecordMsgBinaryV2::PutMsg(const void* msg, uint32_t size, uint8_t queue_index, std::string* err_msg)
{
    auto ret = reinterpret_cast<RecordMsgBinaryV2Impl*>(impl_)->PutMsg(msg, size, queue_index, err_msg);
    return static_cast<ErrorCode>(ret);
}

void* RecordMsgBinaryV2::AllocBuffer(uint32_t data_len, uint8_t queue_index)
{
    return reinterpret_cast<RecordMsgBinaryV2Impl*>(impl_)->AllocBuffer(data_len, queue_index);
}

void RecordMsgBinaryV2::PostBuffer(void* entry_data, uint8_t queue_index)
{
    reinterpret_cast<RecordMsgBinaryV2Impl*>(impl_)->PostBuffer(entry_data, queue_index);
}
ErrorCode RecordMsgBinaryV2::BatchConsumeMsg(uint32_t batch_num)
{
    auto ret = reinterpret_cast<RecordMsgBinaryV2Impl*>(impl_)->BatchConsumeMsg(batch_num);
    return static_cast<ErrorCode>(ret);
}

void RecordMsgBinaryV2::CollectIndicator(boost::property_tree::ptree& indicator_ptree)
{
    return reinterpret_cast<RecordMsgBinaryV2Impl*>(impl_)->CollectIndicator(indicator_ptree);
}

}
