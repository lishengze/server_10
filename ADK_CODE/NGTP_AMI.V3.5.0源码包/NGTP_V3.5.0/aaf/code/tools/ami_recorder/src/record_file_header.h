/**
 * @author 陈志(chenzhi@af.local)
 */

#ifndef AMI_RECORD_FILE_OPT_H_
#define AMI_RECORD_FILE_OPT_H_

///< cpp std
#include <fstream>
#include <string>

///< boost
#include <boost/filesystem.hpp>

///< ami impl
#include "../log.h"
#include "../util.h"

///< impl
#include "recorder_base.h"

#pragma pack(push, 1)

namespace ami
{

enum class RecordFileOptEnum
{
    kNoOpt  = 0,
    kUseCrc = 1u
};

typedef uint16_t RCDFFlagType;

inline RecordFileOptEnum operator&(RecordFileOptEnum a, RecordFileOptEnum b)
{
    return RecordFileOptEnum(static_cast<RCDFFlagType>(a)
                             & static_cast<RCDFFlagType>(b));
}

inline RecordFileOptEnum operator|(RecordFileOptEnum a, RecordFileOptEnum b)
{
    return RecordFileOptEnum(static_cast<RCDFFlagType>(a)
                             | static_cast<RCDFFlagType>(b));
}

inline RecordFileOptEnum operator^(RecordFileOptEnum a, RecordFileOptEnum b)
{
    return RecordFileOptEnum(static_cast<RCDFFlagType>(a)
                             ^ static_cast<RCDFFlagType>(b));
}

inline RecordFileOptEnum operator~(RecordFileOptEnum a)
{
    return RecordFileOptEnum(~static_cast<RCDFFlagType>(a));
}

inline RCDFFlagType& operator&=(RCDFFlagType& a, RecordFileOptEnum b)
{
    return a = (RCDFFlagType)((RecordFileOptEnum)a & b);
}

inline RCDFFlagType& operator|=(RCDFFlagType& a, RecordFileOptEnum b)
{
    return a = (RCDFFlagType)((RecordFileOptEnum)a | b);
}

inline RCDFFlagType& operator^=(RCDFFlagType& a, RecordFileOptEnum b)
{
    return a = (RCDFFlagType)((RecordFileOptEnum)a ^ b);
}

inline bool operator!(RecordFileOptEnum a)
{
    return RecordFileOptEnum::kNoOpt == a;
}

class FileOpts
{
private:
public:
    typedef RecordFileOptEnum FileOpt;

    static constexpr FileOpt kNoOpt = RecordFileOptEnum::kNoOpt;
    static constexpr FileOpt kCrc   = RecordFileOptEnum::kUseCrc;

    FileOpts(FileOpt file_opt = kNoOpt)
        : opts_(static_cast<RCDFFlagType>(kNoOpt))
    {
    }

    bool operator==(const FileOpts& rhs) const
    {
        return opts_ == rhs.opts_;
    }

    bool operator!=(const FileOpts& rhs) const
    {
        return !((*this) == rhs);
    }

    FileOpt operator&(FileOpt opt) const
    {
        return ((FileOpt)opts_) & opt;
    }

    FileOpt operator|(FileOpt opt) const
    {
        return ((FileOpt)opts_) | opt;
    }

    FileOpt operator^(FileOpt opt) const
    {
        return ((FileOpt)opts_) ^ opt;
    }

    FileOpt operator~() const
    {
        return ~((FileOpt)opts_);
    }

    FileOpts& operator&=(FileOpt opt)
    {
        this->opts_ &= opt;
        return (*this);
    }

    FileOpts& operator|=(FileOpt opt)
    {
        this->opts_ |= opt;
        return (*this);
    }

    FileOpts& operator^=(FileOpt opt)
    {
        this->opts_ ^= opt;
        return (*this);
    }

private:
    RCDFFlagType opts_;

    friend std::ostream& operator<<(std::ostream&, const FileOpts&);
};

inline std::ostream& operator<<(std::ostream& os, const FileOpts& file_opts)
{
    if (0 == file_opts.opts_)
    {
        os << "empty";
    }
    else
    {
        os << "crc - "
           << (!!(file_opts & FileOpts::kCrc) ? "o" : "x");
    }

    return os;
}

/**
 * layout: | header len(1 byte) | version | file opt |
 */
class RecordFileHdr
{
public:
    RecordFileHdr() = default;
    RecordFileHdr(const FileOpts& opts)
        : opts_(opts)
    {
    }

    size_t HeaderLength() const
    {
        return header_len_;
    }

    ///< 文件的版本号是否和程序精确匹配
    bool VersionMatch() const
    {
        return version_ == kVersion;
    }

    ///< 程序版本是否能正确解析文件
    bool VersionCompatible() const
    {
        if (!VersionMatch())
        {  //目前只有版本精确匹配才可以
            LOG_FATAL("version not compatible: "
                      "reader version(={1}) != file version(={2})",
                      kVersion, version_);
        }

        return true;
    }

    const FileOpts& GetFileOpts() const
    {
        return opts_;
    }

    FileOpts& GetFileOpts()
    {
        return opts_;
    }

    bool operator==(const RecordFileHdr& rhs) const
    {
        return ((header_len_ == rhs.header_len_)
                && (version_ == rhs.version_)
                && (opts_ == rhs.opts_));
    }

    bool Write(std::streambuf& o) const
    {
        const FileSizeType len_to_write = sizeof(RecordFileHdr);
        const auto len_written          = o.sputn((char*)this, len_to_write);
        if (len_written < len_to_write)
        {
            return false;
        }

        return (o.pubsync() >= 0);
    }

    bool Read(std::streambuf& i)
    {
        if (i.sgetn((char*)this, sizeof(header_len_))
            < (FileSizeType)sizeof(header_len_))
        {
            return false;
        }

        if (header_len_ > sizeof(RecordFileHdr))
        {
            LOG_FATAL("too big header length(={1}) > {2}",
                      (FileSizeType)header_len_,
                      (FileSizeType)sizeof(RecordFileHdr));
            return false;
        }

        const FileSizeType len_to_read = header_len_ - sizeof(header_len_);
        const auto len_read =
            i.sgetn(((char*)this) + sizeof(header_len_), len_to_read);
        if (len_read < len_to_read)
        {
            return false;
        }

        if (!VersionCompatible())
        {
            return false;
        }

        return true;
    }

    bool Read(const std::string& file_path)
    {
        std::filebuf file_buf;
        if (nullptr == file_buf.open(file_path.c_str(), std::ios_base::binary | std::ios_base::in))
        {
            LOG_ERROR("read header of file '{1}'failed.", file_path);
            return false;
        }

        bool res = Read(file_buf);
        if (!res)
        {
            LOG_ERROR("read header of file '{1}' failed", file_path);
        }
        else
        {
            LOG_INFO("read header of file '{1}' ok: {2}", file_path, *this);
        }

        return res;
    }

private:
    static constexpr uint8_t kVersion = 1u;

    /**
     * 为了向前兼容，增加成员只在已有成员后面增加，不要在已有成员中间
     * 增加
     */
    uint8_t header_len_ = (uint8_t)sizeof(RecordFileHdr);
    uint8_t version_    = kVersion;
    FileOpts opts_;

    friend std::ostream& operator<<(std::ostream&, const RecordFileHdr&);
    LOG_DECLARE
};

inline std::ostream& operator<<(std::ostream& os, const RecordFileHdr& header)
{
    os << "header_len=" << (int)header.header_len_ << " "
       << "version=" << (int)header.version_ << " "
       << "opts=(" << header.opts_ << ")";

    return os;
}

/**
 * @param pos 在逻辑文件中的位置
 *
 * @return 所在的物理文件的序号和位置
 */
inline PhysicalFilePosType GetMsgDataPhyFilePos(const FilePosType& pos,
                                                const RecordFileHdr& file_header)
{
    //目前先不实现文件rotate策略
    return PhysicalFilePosType(0, pos + (PosOffType)file_header.HeaderLength());

    /* 如果有文件滚动策略(rotate)，则为以下实现
       return PhysicalFilePosType
       (rpos = pos % kDataFileSizeLimit, pos / kDataFileSizeLimit);
    */
}

#pragma pack(pop)

}  //namespace ami


namespace fmt
{
template <> 
struct formatter<ami::RecordFileHdr> : fmt::formatter<string_view>
{
    template<typename FormatContext>
    auto format(const ami::RecordFileHdr& x, FormatContext& ctx) -> decltype(this->formatter<string_view>::format(string_view{}, ctx))
    {
        std::ostringstream os;
        os << x;
        return formatter<string_view>::format(string_view(os.str()), ctx);
    }
};
}

#endif /* AMI_RECORD_FILE_OPT_H_ */
