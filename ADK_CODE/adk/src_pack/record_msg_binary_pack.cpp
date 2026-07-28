#include <adk/record_msg_binary.h>
#include <adk_pack/record_msg_binary.h>

namespace adk
{

using RecordMsgBinaryImpl = adk_impl::RecordMsgBinary;

RecordMsgBinary::RecordMsgBinary()
{
    record_msg_binary_impl_ = (void*)(new RecordMsgBinaryImpl());
}

RecordMsgBinary::RecordMsgBinary(boost::asio::io_service* ios, uint32_t periodic_timer, uint32_t deal_number)
{
    record_msg_binary_impl_ = (void*)(new RecordMsgBinaryImpl(ios, periodic_timer, deal_number));
}

RecordMsgBinary::~RecordMsgBinary()
{
    assert(record_msg_binary_impl_);
    delete reinterpret_cast<RecordMsgBinaryImpl*>(record_msg_binary_impl_);
}

void RecordMsgBinary::SetSerializeFunc(std::function<std::string(BinaryMsgObject*)> serialize_func)
{
    adk_impl::RecordMsg<adk_impl::RecordMsgBinary::BinaryMsgObject*>::SerializeMsgFunc* ptr_serialize_func
        = (adk_impl::RecordMsg<adk_impl::RecordMsgBinary::BinaryMsgObject*>::SerializeMsgFunc*)(&serialize_func);
    reinterpret_cast<RecordMsgBinaryImpl*>(record_msg_binary_impl_)->SetSerializeFunc(*ptr_serialize_func);
}

bool RecordMsgBinary::Init(const std::string& record_file_name, bool is_addtail_newline, 
    uint32_t que_buffer_size, bool is_throw_exception)
{
    return reinterpret_cast<RecordMsgBinaryImpl*>(record_msg_binary_impl_)->Init(record_file_name, 
        is_addtail_newline, que_buffer_size, is_throw_exception);
}

void RecordMsgBinary::Start(bool is_create_thread)
{
    reinterpret_cast<RecordMsgBinaryImpl*>(record_msg_binary_impl_)->Start(is_create_thread);
}

void RecordMsgBinary::Stop()
{
    reinterpret_cast<RecordMsgBinaryImpl*>(record_msg_binary_impl_)->Stop();
}

void RecordMsgBinary::PutMsg(const char* msg, uint32_t size)
{
    reinterpret_cast<RecordMsgBinaryImpl*>(record_msg_binary_impl_)->PutMsg(msg, size);
}

void RecordMsgBinary::RunIos()
{
    reinterpret_cast<RecordMsgBinaryImpl*>(record_msg_binary_impl_)->RunIos();
}

void RecordMsgBinary::Run()
{
    reinterpret_cast<RecordMsgBinaryImpl*>(record_msg_binary_impl_)->Run();
}

void RecordMsgBinary::RunOnce(int32_t number)
{
    reinterpret_cast<RecordMsgBinaryImpl*>(record_msg_binary_impl_)->RunOnce(number);
}

#ifdef __ADK_DEBUG__
uint64_t RecordMsgBinary::GetSysMem()
{
    return reinterpret_cast<RecordMsgBinaryImpl*>(record_msg_binary_impl_)->GetSysMem();
}
#endif

}