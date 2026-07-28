/**
 * @author 陈志(chenzhi@af.local)
 */

#include "record_file_header.h"

namespace ami
{

LOG_DEFINE(ami::RecordFileHdr)

constexpr uint8_t RecordFileHdr::kVersion;
constexpr FileOpts::FileOpt FileOpts::kNoOpt;
constexpr FileOpts::FileOpt FileOpts::kCrc;

}  //namespace ami
