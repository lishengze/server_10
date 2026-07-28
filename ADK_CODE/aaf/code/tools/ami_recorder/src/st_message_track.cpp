/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */

///< std cpp

///< boost
#include <boost/filesystem.hpp>
#include <boost/locale/format.hpp>

///< impl
#include "st_message_track.h"

namespace ami
{

namespace bl = boost::locale;
namespace bf = boost::filesystem;
namespace bt = boost::property_tree;

LOG_DEFINE(ami::StMessageTrack)

ErrorCode_def StMessageTrack::OpenIndexDataFiles()
{
    IF_ERR_RET(OpenMsgDataFilebuf((std::ios_base::binary
                                   | std::ios_base::out)));

    return kSuccess;
}

void StMessageTrack::DoDump(std::ostream& os) const
{
    os << "track(" << path_ << "): "
       << "message_memory_pool = " << app_msg_mp_manager_.GetMPTableName() << ", "
       << "message_queue = " << msg_ptr_queue_->name() << ", "
       << "last_update_time = " << FormatLastUpdateTP() << ", "
       << "error_happened = " << std::boolalpha << HasError() << ", "
       << "recovery_ok = " << std::boolalpha << IsRecoveryOk() << ", "
       << "no_work_sleep_count = " << no_work_sleep_cnt_ << ", "
       << "yield_count = " << yield_cnt_;
}

void StMessageTrack::DoDumpToPtree(bt::ptree& status_tree) const
{
    bt::ptree& track_status_tree = status_tree.add_child(path_, bt::ptree());

    track_status_tree.put("last_update_time", FormatLastUpdateTP());
    track_status_tree.put("error_happend", HasError());
    track_status_tree.put("recovery_ok", IsRecoveryOk());
    track_status_tree.put("no_work_sleep_count", no_work_sleep_cnt_);
    track_status_tree.put("yield_count", yield_cnt_);
}

void StMessageTrack::ClearQueueMsgAtRecovery()
{
    if (msgdata_filebuf_to_recover_ == nullptr)
    {
        return;
    }

    // FIXME: dump the queue cursors
    // add the following code to prevent dead locking
    if (msg_ptr_queue_ != nullptr)
    {
        msg_ptr_queue_->Consistent();    
    }

    uint32_t cnt = 0;
    while (msg_ptr_queue_ != nullptr)
    {
        adk::Entry* entry = nullptr;
        auto ec = msg_ptr_queue_->WaitEntry(&entry);
        if (ec == adk::ErrorCode::kSuccess)
        {
            msg_ptr_queue_->FreeEntry(entry);
            ++cnt;
        }
        else
        {
            break;
        }
    }
    LOG_INFO("clear the track: {1} msg queue at recovery mode, clear msg cnt: {2}",
              GetTrackPath(),
              cnt);
}

}  // namespace ami
