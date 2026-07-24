/**
 * @author 陈志(chenzhi@af.local)
 */

#include "record_channel.h"
#include <ami/ami_record_channel.h>

namespace ami
{

AmiRecordChannel::AmiRecordChannel(RecordChannel* impl)
    : impl_(impl)
{
}

AmiRecordChannel::~AmiRecordChannel()
{
    impl_ = nullptr;
}

AmiRecordChannel::AmiRecordChannel(const AmiRecordChannel& rhs)
    : impl_(rhs.impl_)
{
}

}  //namespace ami
