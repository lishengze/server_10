/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */
#ifndef AMI_MESSAGE_TRACK_H_
#define AMI_MESSAGE_TRACK_H_

///< linux
#include <fcntl.h>  //open, fallocate
#include <linux/falloc.h>
#include <sys/stat.h>  //fstat
#include <sys/types.h>
#include <sys/uio.h>  //writev
#include <unistd.h>  //close, ftruncate, fsync

///< cpp std
#include <cstring>
#include <functional>
#include <streambuf>
#include <string>
#include <unordered_map>
#include <vector>

///< boost
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/regex.hpp>

///< ami public
#include <ami/message.h>
#include <ami/property.h>

///< ami impl
#include "../ami_constant.h"
#include "../ami_message.h"
#include "../log.h"
#include "../util.h"
#include "../test_driver.h"

///< impl
#include "control_message_key.h"
#include "record_file_header.h"
#include "recorded_message_index.h"
#include "serial_worker.h"

namespace ami
{

struct BufferWrite
{
    bool Write(const void* data, uint32_t length)
    {
        assert (fd_ != -1);

        // not enough space left
        if (pos_ + length > 4096
            && pos_ > 0)
        {
            size_t ret = ::write(fd_, buffer_, pos_);
            if (ret != pos_)
            {
                return false;
            }
            // write success;
            pos_ = 0;
        }

        if (length > 4096)
        {
            assert(pos_ == 0);
            // write the large buffer directly
            size_t ret = ::write(fd_, data, length);
            if (ret == length)
            {
                return true;
            }
            return false;
        }

        // copy the data to the left space
        memcpy(&buffer_[pos_], data, length);
        pos_ += length;
        assert(pos_ <= 4096);
        return true;
    }

    bool Open(const char* path)
    {
        fd_ = ::open(path, O_CREAT|O_WRONLY, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
        if (fd_ < 0)
        {
            return false;
        }
        return true;
    }

    bool Close()
    {
        // has data left
        if (pos_ > 0)
        {
            size_t ret = ::write(fd_, buffer_, pos_);
            if (ret != pos_)
            {
                return false;
            }
            // write success
            pos_ = 0;    
        }
        
        ::close(fd_);
        fd_ = -1;
        return true;
    }

    bool Good()
    {
        return (fd_ != -1);
    }

    int  fd_ = -1;
    uint32_t pos_ = 0;
    char buffer_[4096];
};


/**
 * 持久化一个消息流
 *
 * 消息按照在消息流上的序号按照从小到大的顺序存储在一个逻辑文件中，物理上按照固定大小
 *（@see Recorder::kDataFileSizeLimit）切割，文件名按照逻辑上的顺序编号（0，1，2...）
 */
class MessageTrack
{
protected:
    static constexpr const char* kDataQueuePrefix = "data";
    static constexpr int kDefaultWeight           = 128;
    static constexpr int kRxWeightMultiply        = 10;

    /**
     * 数据文件预分配的默认大小。ext4文件系统有预分配的特性，默认
     * 预分配的大小为一个extent的预分配的最大值，即2^15-1个block，
     * 即(2^15-1) * 4K
     */
    static constexpr FileSizeType kDataFilePreAllocDefault =
        ((1u << 15) - 1u) * 4096u;

public:
    /**
     * 写文件的buffer
     *
     * 使用共享内存作为写文件的buffer，在recorder被误杀后该buffer不会
     * 丢失。以恢复模式启动以后，recorder可以找回这些buffer并把里面的
     * 内容持久化到磁盘。
     *
     */
    struct FileWriteBuffer
    {
        typedef std::function<void()> AfterOverFlowOpType;

        ///< 数据文件的写缓存大小设置为ami_message的最大长度
        static constexpr size_t kDataFileBufferSize =
            AMI_MAX_MESSAGE_SIZE_INTERNAL;

        /**
         * @par portable_issue
         *
         * 目前只适合linux平台
         */
        class FileBuffer : public std::streambuf
        {
        public:
            explicit FileBuffer(FileWriteBuffer& encloser,
                                MessageTrack& track,
                                const AfterOverFlowOpType& op,
                                size_t quantum)
                : encloser_(encloser),
                  enclosing_track_(track),
                  op_(op),
                  quantum_(quantum),  // the data file quantum is 1, the index file quantum is OrdinalIndex::ValueSize()
                  buf_len_(encloser_.avail_len_ / quantum_ * quantum_)  // align to quantum
            {
                buf_vec_.reserve(1024u);
            }

            virtual ~FileBuffer()
            {
                if (is_open())
                {
                    sync();
                    close();
                }
            }

            FileBuffer(const FileBuffer&) = delete;
            FileBuffer& operator=(const FileBuffer&) = delete;

            FileBuffer* open(std::ios_base::openmode open_mode)
            {
                FileBuffer* ret = nullptr;

                int open_flag = O_CREAT | O_WRONLY;
                if (open_mode & std::ios_base::app)
                {
                    open_flag |= O_APPEND;
                }

                if (open_mode & std::ios_base::trunc)
                {
                    open_flag |= O_TRUNC;
                }

                mode_t create_mode = S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP
                    | S_IWOTH | S_IROTH;

                fd_ = ::open(encloser_.GetFilePathStr().c_str(),
                             open_flag, create_mode);
                if (fd_ >= 0)
                {
                    LOG_INFO("{1}'s real buffer size={2}",
                             encloser_.GetFilePathStr(), buf_len_);

                    is_open_ = true;
                    this->setp(encloser_.GetBufferHead(),
                               encloser_.GetBufferHead() + buf_len_);
                    ret = this;
                }
                else
                {
                    LOG_ERROR("open {1} failed, "
                              "error code({2}), detail({3})",
                              encloser_.GetFilePathStr(),
                              errno, ::strerror(errno));
                }

                return ret;
            }

            bool is_open() const
            {
                return is_open_;
            }

            FileBuffer* close()
            {
                if (::close(fd_) < 0)
                {
                    return nullptr;
                }
                else
                {
                    fd_      = -1;
                    is_open_ = false;
                    return this;
                }
            }

            FileBuffer* tie(FileBuffer* tiee)
            {
                FileBuffer* old_tiee = tiee_;
                tiee_                = tiee;
                return old_tiee;
            }

            FileBuffer* preallocate(pos_type offset, std::streamsize length)
            {
                FileBuffer* ret = nullptr;
                if (is_open())
                {
                    int mode = FALLOC_FL_KEEP_SIZE;

                    if (0 == ::fallocate(fd_, mode, offset, length))
                    {
                        LOG_DEBUG("preallocate '{1}' from offset={2} as len={3}",
                                  encloser_.GetFilePathStr(), offset, length);

                        ret = this;
                    }
                    else
                    {
                        static bool is_write_err_log = false;
                        if (!is_write_err_log)
                        {
                            is_write_err_log = true;
                            LOG_ERROR("fallocate {1} failed, error code({2}), detail({3})",
                                      encloser_.GetFilePathStr(), errno, ::strerror(errno));
                        }
                        else
                        {
                            LOG_WARN_RATELIMITED_VERY_LOW(
                                "fallocate {1} failed, error code({2}), detail({3})",
                                encloser_.GetFilePathStr(), errno, ::strerror(errno));
                        }
                    }
                }

                return ret;
            }

            FileBuffer* depreallocate()
            {
                FileBuffer* ret = nullptr;
                if (is_open())
                {
                    struct stat sb;
                    if ((sync() >= 0)
                        && (0 == ::fsync(fd_))
                        && (0 == ::fstat(fd_, &sb))
                        && (0 == ::ftruncate(fd_, sb.st_size)))
                    {
                        LOG_DEBUG("depreallocate '{1}', shrink to len={2}",
                                  encloser_.GetFilePathStr(), sb.st_size);
                        ret = this;
                    }
                    else
                    {
                        LOG_ERROR("depreallocate '{1}' failed, shrink to len={2}",
                                  encloser_.GetFilePathStr(), sb.st_size);
                    }
                }

                return ret;
            }

            FileBuffer* truncate(std::streamsize length)
            {
                FileBuffer* ret = nullptr;
                if (is_open())
                {
                    if (0 == ::ftruncate(fd_, length))
                    {
                        ret = this;
                    }
                    else
                    {
                        LOG_ERROR("ftruncate {1} failed, "
                                  "error code({2}), detail({3})",
                                  encloser_.GetFilePathStr(),
                                  errno, ::strerror(errno));
                    }
                }

                return ret;
            }

            bool Stat(std::size_t& size)
            {
                struct stat sb;
                if (::fstat(fd_, &sb) != 0)
                {
                    LOG_ERROR("fstat on file {1} failed, errno {2}, desc {3}",
                              encloser_.GetFilePathStr(), errno, ::strerror(errno));
                    return false;
                }            
                size = sb.st_size;
                return true;
            }

            FileWriteBuffer* get_encloser() const
            {
                return &encloser_;
            }

            virtual FileBuffer* setbuf(char_type*, std::streamsize)
            {
                return this;
            }

            virtual pos_type seekoff(off_type off, std::ios_base::seekdir way, std::ios_base::openmode)
            {
                pos_type ret = pos_type(off_type(-1));
                if (is_open())
                {
                    int whence = SEEK_CUR;
                    switch (way)
                    {
                    case std::ios_base::beg:
                        whence = SEEK_SET;
                        break;
                    case std::ios_base::cur:
                        whence = SEEK_CUR;
                        break;
                    case std::ios_base::end:
                        whence = SEEK_END;
                        break;
                    default:
                        break;
                    }

                    ret = (pos_type)lseek(fd_, off, whence);
                }

                return ret;
            }

            // triggered by pubseekpos
            // pos_type is uint64_t
            virtual pos_type seekpos(pos_type pos, std::ios_base::openmode)
            {
                pos_type ret = pos_type(off_type(-1));

                if (is_open())
                {
                    ret = (pos_type)lseek(fd_, pos, SEEK_SET);
                }

                return ret;
            }

            // triggered by pubsync
            virtual int sync()
            {
                int ret = 0;
                if (this->pbase() < this->pptr())
                {
                    const int_type of_ret = this->overflow();
                    if (traits_type::eq_int_type(of_ret, traits_type::eof()))
                    {
                        ret = -1;
                    }
                }
                else if (tiee_)
                {
                    ret = tiee_->sync();
                }

                return ret;
            }

            int_type WriteUntilExit(int_type c, ssize_t write_size);

            void TrimBufVector(ssize_t writev_ret)
            {
                auto writev_ret_local = writev_ret;
                auto it_begin         = buf_vec_.begin();
                auto it_end           = it_begin;
                for (; it_end != buf_vec_.end(); ++it_end)
                {
                    if ((ssize_t)(it_end->iov_len) <= writev_ret_local)
                    {
                        writev_ret_local -= it_end->iov_len;
                        continue;
                    }

                    it_end->iov_len -= writev_ret_local;
                    it_end->iov_base = (char*)(it_end->iov_base) + writev_ret_local;
                    break;
                }

                if (it_begin != it_end)
                    buf_vec_.erase(it_begin, it_end);
            }

            virtual int_type overflow(int_type c = traits_type::eof())
            {
                LOG_TRACE("file '{1}'s buffer info: filled={2}, avail={3}",
                          encloser_.GetFilePathStr(),
                          (off_type)(pptr() - pbase()),
                          (off_type)(epptr() - pptr()));

                /*优先把tiee的缓存刷到磁盘*/
                if (tiee_ && (traits_type::eq_int_type(traits_type::eof(), tiee_->overflow(traits_type::eof()))))
                {
                    return traits_type::eof();
                }

                int_type ret       = traits_type::eof();
                const bool testeof = traits_type::eq_int_type(c, ret);
                char_type ch;
                if (!testeof)
                {
                    ch = traits_type::to_char_type(c);
                    iovec buf;
                    buf.iov_base = &ch;
                    buf.iov_len  = 1u;
                    buf_vec_.push_back(buf);
                    this->pbump(1u);            // add 1 byte on put pointer
                }

                if (this->pbase() < this->pptr())
                {
                    assert(!buf_vec_.empty());

#ifdef __RECORDER_TEST_WRITE_ERROR__
                    auto buf_it = buf_vec_.rbegin();
                    if (buf_it->iov_len > 1u)
                    {
                        buf_it->iov_len -= 1u;
                        LOG_ERROR("change buf_it->iov_len to {1}", buf_it->iov_len);
                    }
                    LOG_ERROR("current buf_vec_.size() = {1}", buf_vec_.size());
#endif

                    GAUGE_BEGIN(enclosing_track_.writev_gauge_);
                    auto writev_ret = ::writev(fd_, &(*buf_vec_.begin()), buf_vec_.size());
                    GAUGE_GAUGE(enclosing_track_.writev_gauge_);

                    if (writev_ret == (this->pptr() - this->pbase()))
                    {
                        this->setp(this->pbase(), this->pbase() + buf_len_);
                        buf_vec_.clear();
                        ret = traits_type::not_eof(c);
                    }
                    else
                    {
                        LOG_ERROR("write of '{1}'(fd={3}) failed: {2}, {4}",
                                  encloser_.GetFilePathStr(), errno, fd_, writev_ret);

#ifdef __RECORDER_TEST_WRITE_ERROR__
                        auto buf_it = buf_vec_.rbegin();
                        if (buf_it->iov_len > 1u)
                        {
                            buf_it->iov_len += 1u;
                            LOG_ERROR("change buf_it->iov_len to {1}", buf_it->iov_len);
                        }
#endif

                        if (writev_ret > 0)
                        {
                            TrimBufVector(writev_ret);

#ifdef __RECORDER_TEST_WRITE_ERROR__
                            LOG_ERROR("new buf_vec_.size() = {1}", buf_vec_.size());
#endif
                        }
                        else
                        {
                            writev_ret = 0;
                        }

                        return WriteUntilExit(c, writev_ret);
                    }
                }
                else
                {
                    assert(buf_vec_.empty());
                    ret = traits_type::not_eof(c);
                }

                if (!traits_type::eq_int_type(ret, traits_type::eof())
                    && (!!op_))
                {
                    op_();  // to free all share memory messages
                }

                return ret;
            }

            // triggered by sputn
            virtual std::streamsize xsputn(const char_type* s, std::streamsize n)
            {
                if (0 == n)
                    return n;

                std::streamsize ret       = 0;
                std::streamsize buf_avail = this->epptr() - this->pptr();
                if (n >= buf_len_)
                {
                    int_type of_ret = this->overflow();
                    if (!traits_type::eq_int_type(of_ret, traits_type::eof()))
                    {
                        iovec buf;
                        buf.iov_base = const_cast<void*>((const void*)s);
                        buf.iov_len  = n;
                        GAUGE_BEGIN(enclosing_track_.writev_gauge_);
                        const auto writev_ret = ::writev(fd_, &buf, 1u);
                        GAUGE_GAUGE(enclosing_track_.writev_gauge_);
                        if (writev_ret == n)
                        {
                            ret = n;
                        }
                    }
                }
                else if (n > buf_avail)
                {
                    int_type of_ret = this->overflow();
                    if (!traits_type::eq_int_type(of_ret, traits_type::eof()))
                    {
                        iovec buf;
                        buf.iov_base = const_cast<void*>((const void*)s);
                        buf.iov_len  = n;
                        buf_vec_.push_back(buf);
                        this->pbump(n); // add n byte on put pointer
                        ret = n;
                    }
                }
                /**
                 * 目前对索引文件buffer大小的限制（2K且一定是单个索引
                 * 的整数倍），保证了所有的索引buffer只会走以下两个分
                 * 支，从而避免了以上两个分支先free再尝试写盘的可能性
                 */
                else if (n == buf_avail)
                {
                    iovec buf;
                    buf.iov_base = const_cast<void*>((const void*)s);
                    buf.iov_len  = n;
                    buf_vec_.push_back(buf);
                    this->pbump(n); // add n byte on put pointer
                    ret = n;

                    this->overflow();
                }
                else
                {
                    iovec buf;
                    buf.iov_base = const_cast<void*>((const void*)s);
                    buf.iov_len  = n;
                    buf_vec_.push_back(buf);
                    this->pbump(n); // add n byte on put pointer
                    ret = n;
                }
                return ret;
            }

            virtual int_type pbackfail(int_type c)
            {
                return traits_type::eof();
            }

            FileWriteBuffer& encloser_;
            MessageTrack& enclosing_track_;
            const AfterOverFlowOpType op_;
            FileBuffer* tiee_ = nullptr;
            const size_t quantum_;
            const std::streamsize buf_len_;
            int fd_                = -1;
            bool is_open_          = false;
            uint64_t next_msg_sqn_ = 0;
            std::vector<iovec> buf_vec_;
        };

        typedef FileBuffer FileBufType;

        uint64_t next_msg_sqn() { return (++(file_buf_->next_msg_sqn_)); }
        void recover_next_msg_sqn(uint64_t next_sqn) { file_buf_->next_msg_sqn_ = next_sqn; }

        /**
         * @param op 在overflow以后会调用
         * @param quantum buffer_size会设置为该参数的整数倍
         */
        explicit FileWriteBuffer(const boost::filesystem::path& file_path,
                                 MessageTrack& track,
                                 const AfterOverFlowOpType& op = AfterOverFlowOpType(),
                                 size_t quantum                = 1);

        ~FileWriteBuffer()
        {
            delete file_buf_;
        }

        FileWriteBuffer* Open(const std::ios_base::openmode& open_mode)
        {
            if (nullptr == file_buf_->open(open_mode))
            {
                return nullptr;
            }
            else
            {
                return this;
            }
        }

        bool IsOpen() const
        {
            return file_buf_->is_open();
        }

        FileWriteBuffer* Close()
        {
            if (nullptr == file_buf_->close())
            {
                return nullptr;
            }
            else
            {
                return this;
            }
        }

        FileWriteBuffer* Tie(FileWriteBuffer* tiee)
        {
            FileBufType* old_tiee = nullptr;

            if (tiee)
            {
                LOG_DEBUG("set '{1}' as {2}'s tiee",
                          tiee->GetFilePathStr(), GetFilePathStr());
                old_tiee = file_buf_->tie(&tiee->GetFilebuf());
            }
            else
            {
                LOG_DEBUG("unset {1}'s tiee", GetFilePathStr());
                old_tiee = file_buf_->tie(nullptr);
            }

            if (nullptr == old_tiee)
            {
                return nullptr;
            }
            else
            {
                return old_tiee->get_encloser();
            }
        }

        FileWriteBuffer* Preallocate(FilePosType offset, FileSizeType length)
        {
            if (nullptr == file_buf_->preallocate(offset, length))
            {
                return nullptr;
            }
            else
            {
                return this;
            }
        }

        FileWriteBuffer* DePreallocate()
        {
            if (nullptr == file_buf_->depreallocate())
            {
                return nullptr;
            }
            else
            {
                return this;
            }
        }

        FileWriteBuffer* Truncate(FileSizeType length)
        {
            if (nullptr == file_buf_->truncate(length))
            {
                return nullptr;
            }
            else
            {
                return this;
            }
        }

        ErrorCode_def Stat(std::size_t& size)
        {
            if (file_buf_->Stat(size) == true)
            {
                return ErrorCode::kSuccess;   
            }
            return ErrorCode::kFailure;
        }

        void* operator new(size_t sz, void* ptr) noexcept
        {
            return ::operator new(sz, ptr);
        }

        void* operator new(size_t, const MsgData&);
        static void Delete(void* wb, const MsgData&) noexcept;

        void operator delete(void* wb, const MsgData& md)
        {
            Delete(wb, md);
        }

        void* operator new(size_t, const Index&)
        {
            /*索引文件的buffer统一设为2K*/
            adk::MemoryBuffer* mb =
                (adk::MemoryBuffer*)::operator new(kIndexBufferSize);
            mb->set_mem_buf_size(kIndexBufferSize);
            mb->reset();
            return mb->data;
        }

        static void Delete(void* wb, const Index&) noexcept
        {
            if (!wb)
                return;
            adk::MemoryBuffer* mb =
                ADK_CONTAINER_OF(wb, adk::MemoryBuffer, data);
            ::operator delete(mb);
        }

        void operator delete(void* wb, const Index& idx)
        {
            Delete(wb, idx);
        }

        char* GetBufferHead() const
        {
            return ((char*)this
                    + offsetof(FileWriteBuffer, data_begin_)
                    + file_name_len_
                    + path_len_ + data_path_len_
                    + app_mp_mgr_name_len_);
        }

        FileBufType& GetFilebuf() const
        {
            return *file_buf_;
        }

        std::string GetFileName() const
        {
            return std::string(data_begin_, file_name_len_);
        }

        std::string GetTrackPathStr() const
        {
            return std::string(data_begin_ + file_name_len_,
                               path_len_);
        }

        std::string GetDataPathStr() const
        {
            return std::string(data_begin_ + file_name_len_ + path_len_,
                               data_path_len_);
        }

        std::string GetAppMPMgrName() const
        {
            return std::string(data_begin_ + file_name_len_ + path_len_ + data_path_len_,
                               app_mp_mgr_name_len_);
        }

        std::string GetFilePathStr() const
        {
            return (boost::filesystem::path(GetDataPathStr())
                    / GetFileName())
                .string();
        }

        adk::MemoryBuffer* GetLowLayerMemoryBuffer() const
        {
            return ADK_CONTAINER_OF(this, adk::MemoryBuffer, data);
        }

        static FileWriteBuffer* ConvertFromShmPointer(const adk::ShmPointer& shm_point,
                                                      const adk::MPManager& mp_manager)
        {
            adk::MPManager& mp_manager_not_const = const_cast<adk::MPManager&>(mp_manager);
            return reinterpret_cast<FileWriteBuffer*>(
                mp_manager_not_const.ShmPtrToMemBuf(
                                        &const_cast<adk::ShmPointer&>(shm_point))
                    ->data);
        }

        operator FileBufType&() const
        {
            return GetFilebuf();
        }

        operator FileBufType*() const
        {
            return &GetFilebuf();
        }

        bool Sync()
        {
            int ret = file_buf_->pubsync();
            return (ret >= 0) ? true : false;
        }

        FileBufType* file_buf_;
        char track_type;  ///< 't' for tx, 'r' for rx, 's' for ack
        uint32_t file_name_len_;  ///< 文件名长度
        uint32_t path_len_;  ///< MessageTrack::path_的长度
        uint32_t data_path_len_;  ///< MessageTrack::data_path_的长度
        uint32_t app_mp_mgr_name_len_;  ///< ami消息所属mp manger的名字长度
        uint32_t avail_len_;  ///< 净可用长度
        /**
         * layout: |file_name...|channel(track) path...|data path...
         * |app_mp_mgr_name...|buffer...|
         */
        char data_begin_[];
    };

    typedef std::unordered_map<HashMapKeyType, Message::SqnType> KeyvalueSqnMapType;
    typedef std::unordered_map<HashMapKeyType, KeyvalueSqnMapType> KeyindexTypeSqnMap;

    typedef std::unordered_map<HashMapKeyType, FileWriteBuffer*> KeyvalueFilebufMapType;
    typedef std::unordered_map<HashMapKeyType, KeyvalueFilebufMapType> KeyindexTypeFilebufMap;

    struct RecordedMsgStuff
    {
        MessageTrack* msg_track   = nullptr;
        MsgRecord* msg_record     = nullptr;
        MQMsgEntry* entry         = nullptr;
        adk::MPSCQueue* msg_queue = nullptr;
        bool to_release           = false;
        bool to_delete            = false;
        bool to_record            = true;
        MsgCRCType crc_;
        size_t index_cnt = 0;
        OrdinalIndex indexes[kIndexTypeCnt];

        RecordedMsgStuff(MessageTrack* msg_track_,
                         MsgRecord* msg_record_,
                         bool to_record_ = true)
            : msg_track(msg_track_),
              msg_record(msg_record_),
              to_delete(true),
              to_record(to_record_)
        {
        }

        RecordedMsgStuff(MessageTrack* msg_track_,
                         MQMsgEntry* entry_,
                         adk::MPSCQueue* msg_queue_,
                         bool to_record_ = true)
            : msg_track(msg_track_),
              entry(entry_),
              msg_queue(msg_queue_),
              to_release(true),
              to_record(to_record_)
        {
            msg_record = &entry->msg_record;
        }

        RecordedMsgStuff(const RecordedMsgStuff&) = delete;
        RecordedMsgStuff& operator=(const RecordedMsgStuff&) = delete;

        RecordedMsgStuff(RecordedMsgStuff&& orig)
            : msg_track(orig.msg_track),
              msg_record(orig.msg_record),
              entry(orig.entry),
              msg_queue(orig.msg_queue),
              to_release(orig.to_release),
              to_delete(orig.to_delete),
              to_record(orig.to_record),
              crc_(orig.crc_),
              index_cnt(orig.index_cnt)
        {
            std::copy(&orig.indexes[0], &orig.indexes[0] + kIndexTypeCnt,
                      &indexes[0]);

            orig.to_release = false;
            orig.to_delete  = false;
            orig.to_record  = false;
        }

        RecordedMsgStuff& operator=(RecordedMsgStuff&&) = delete;

        ~RecordedMsgStuff()
        {
            if (to_release)
            {
                msg_track->ReleaseMessage(
                    *entry->GetOrigAmiMsg(msg_track->app_msg_mp_manager_));

                msg_queue->FreeEntry(entry->GetEncloingAdkEntry());
            }
            else if (to_delete)
            {
                delete msg_record;
            }

            if (to_record)
            {
                msg_track->recorded_msg_cnt_++;
            }
        }

        const std::streambuf::char_type*
        AddIndex(const OrdinalIndex& index)
        {
            assert(index_cnt < kIndexTypeCnt);

            indexes[index_cnt] = index;
            void* ret          = indexes[index_cnt].ValueBegin();
            index_cnt++;

            return (const std::streambuf::char_type*)ret;
        }

        const std::streambuf::char_type*
        AddCRC(const MsgCRCType& crc)
        {
            crc_ = crc;
            return (const std::streambuf::char_type*)&crc_;
        }
    };

    static constexpr size_t kMsgToFreeQMaxLength =
        kIndexBufferSize / OrdinalIndex::ValueSize();

public:
    MessageTrack()
    {
        // msgs_to_free_.reserve(kMsgToFreeQMaxLength);
        // make sure msgs_to_free_ is large enough
        msgs_to_free_.reserve((kMsgToFreeQMaxLength * 2));
    }

    virtual ~MessageTrack()
    {
        app_msg_mp_manager_.DetachAll();
    }

    void Start();
    void Stop();

    // clear all msg at queue
    virtual void ClearQueueMsgAtRecovery() = 0;

    ///< 创建一个新的通道
    ErrorCode_def Init(const string& track_path,
                       const Property& request,
                       Property& reply,
                       size_t recorder_worker_idx)
    {
        return DoInit(track_path, request, reply, recorder_worker_idx);
    }

    ///< 故障恢复（丢消息场景）以后agent重建通道
    ErrorCode_def Init(const string& track_path,
                       const Property& request,
                       Property& reply)
    {
        return DoInit(track_path, request, reply);
    }

    ///< 故障恢复（不丢消息场景）以后agent重建通道
    ErrorCode_def Init(const adk::ShmPointer& shm_point,
                       size_t recorder_worker_idx)
    {
        return DoInit(shm_point, recorder_worker_idx);
    }

    std::string GetTrackPath() const
    {
        return path_;
    }

    std::string GetTrackDataPath() const
    {
        return data_path_;
    }

    bool IsRecoveryOk() const
    {
        if (msgdata_filebuf_to_recover_ && recovery_ok_)
        {
            return true;
        }

        return false;
    }

    bool HasError() const
    {
        return error_happened_;
    }

    void ReadyToQuit()
    {
        if(is_already_stop_track_)
        {
            return;
        }
        can_quit_elegantly_ = false;
    }

    bool CanQuitElegantly() const
    {
        return can_quit_elegantly_;
    }

    bool HasRebuilt() const
    {
        return rebuilt_;
    }

    bool IsUseCRC() const
    {
        return !!(msg_data_file_header_.GetFileOpts() & FileOpts::kCrc);
    }

    void ReleaseMessage(AmiMessage& ami_msg) const
    {
        DoReleaseMessage(ami_msg);
    }

    bool SyncToFile()
    {
        return DoSyncToFile();
    }

    bool operator==(const MessageTrack& rhs) const
    {
        return IsEqual(rhs);
    }

    void Dump(std::ostream& os) const
    {
        DoDump(os);
    }

    void DumpToPtree(boost::property_tree::ptree& status_tree) const
    {
        DoDumpToPtree(status_tree);
    }

    static MessageTrack* NewTrack(const adk::ShmPointer& shm_point);

protected:
    virtual ErrorCode_def DoInit(const string& track_path,
                                 const Property& request,
                                 Property& reply,
                                 size_t recorder_worker_idx);

    ///< 故障恢复以后agent重建通道
    virtual ErrorCode_def DoInit(const string& track_path,
                                 const Property& request,
                                 Property& reply);

    ///< 恢复故障后的通道
    virtual ErrorCode_def DoInit(const adk::ShmPointer& shm_point,
                                 size_t recorder_worker_idx);

    virtual void DoDump(std::ostream& os) const;
    virtual void DoDumpToPtree(boost::property_tree::ptree& status_tree) const;

    virtual bool IsEqual(const MessageTrack& rhs) const
    {
        return ((path_ == rhs.path_)
                && (data_path_ == rhs.data_path_)
                && (app_msg_mp_manager_.GetMPTableName() == rhs.app_msg_mp_manager_.GetMPTableName())
                && (msg_ptr_queue_ && rhs.msg_ptr_queue_)
                && (msg_ptr_queue_->name() == rhs.msg_ptr_queue_->name())
                && (msg_data_file_header_ == rhs.msg_data_file_header_));
    }

    void DeleteFileBuffer(FileWriteBuffer* filebuf)
    {
        if (!filebuf)
        {
            return;
        }

        LOG_DEBUG("delete file '{1}'s buffer.", filebuf->GetFilePathStr());

        if (msg_data_file_buf_ == filebuf)
        {
            msg_data_file_buf_->Delete(msg_data_file_buf_, MsgData());
            return;
        }

        DeleteFileBuffer(filebuf->Tie(nullptr));
        filebuf->Delete(filebuf, Index());
    }

    bool PreallocateDataFileUntilExit();

    bool PreallocateDataFile()
    {
        if (!msg_data_file_buf_)
        {
            return true;
        }

        if (cur_msgdata_filepos_ >= msgdata_len_inc_prealloc_)
        {
            const auto phyFilePos = GetMsgDataPhyFilePos(cur_msgdata_filepos_,
                                                         msg_data_file_header_)
                                        .second;
            if (nullptr == msg_data_file_buf_->Preallocate(phyFilePos, kDataFilePreAllocDefault))
            {
                return PreallocateDataFileUntilExit();
            }

            msgdata_len_inc_prealloc_ = cur_msgdata_filepos_
                + (FilePosType)kDataFilePreAllocDefault;
        }

        return true;
    }

    bool DePreallocateDataFile()
    {
        if (!msg_data_file_buf_)
        {
            return true;
        }

        if (nullptr == msg_data_file_buf_->DePreallocate())
        {
            return false;
        }

        msgdata_len_inc_prealloc_ = cur_msgdata_filepos_;
        return true;
    }

    void FreeAllMessages()
    {
        AMI_TD_FLAG_JOB(ami_test_sync_control_, {
                sleep(10000);
        });
        msgs_to_free_.clear();
    }

    template <typename... Args>
    RecordedMsgStuff& PushIntoRecordingMsgQ(Args&&... args)
    {
        AMI_TD_JOB_BY_ENV_ARG_THRESHOLD("AMI_RETRANS_TEST_PAUSE_RELEASE_MSG_AT", 
                                        path_,
                                        {
                                            ami_test_sync_control_ = true;
                                        });
        /**
         * 在出现大量需要滤重的消息时，会造成msgs_to_free_频繁到达上限，
         * 从而进入下面的分支
         */
        if (msgs_to_free_.size() >= kMsgToFreeQMaxLength
            && !SyncToFile())
        {
            throw std::system_error(kFailure);
        }

        msgs_to_free_.emplace_back(std::forward<Args>(args)...);
        return *msgs_to_free_.rbegin();
    }

    RecordedMsgStuff& GetRecordingMsg()
    {
        return *msgs_to_free_.rbegin();
    }

    ErrorCode_def OpenMsgDataFilebuf(std::ios_base::openmode open_mode);
    virtual ErrorCode_def OpenIndexDataFiles();

    bool WriteMsgOrdinalIndex(std::streambuf& o,
                              const OrdinalIndex& index_item,
                              RecordedMsgStuff& fms)
    {
        const FileSizeType len_to_write = index_item.ValueSize();
        GAUGE_BEGIN(sputn_gauge_);
        const auto len_written = o.sputn(fms.AddIndex(index_item), len_to_write);
        GAUGE_GAUGE(sputn_gauge_);

        if (len_written < len_to_write)
        {
            return false;
        }

        return true;
    }

    bool WriteMsgEndpointID(std::streambuf& o, const MsgRecord& msg_record)
    {
        const FileSizeType len_to_write = sizeof(AmiMetaData::IDType);
        GAUGE_BEGIN(sputn_gauge_);
        const auto len_written =
            o.sputn((std::streambuf::char_type*)&msg_record.endpoint_id,
                    len_to_write);
        GAUGE_GAUGE(sputn_gauge_);
        if (len_written < len_to_write)
        {
            return false;
        }

        if (IsUseCRC())
        {
            crc_ = MsgCRCCalFunc(&msg_record.endpoint_id, len_to_write, crc_);
        }

        return true;
    }

    bool WriteMsgTransportID(std::streambuf& o, const MsgRecord& msg_record)
    {
        const FileSizeType len_to_write = sizeof(AmiMetaData::IDType);
        GAUGE_BEGIN(sputn_gauge_);
        const auto len_written = o.sputn((std::streambuf::char_type*)(&msg_record.transport_id),
                                         len_to_write);
        GAUGE_GAUGE(sputn_gauge_);
        if (len_written < len_to_write)
        {
            return false;
        }

        if (IsUseCRC())
        {
            crc_ = MsgCRCCalFunc(&msg_record.transport_id, len_to_write, crc_);
        }

        return true;
    }

    bool WriteAppMsg(std::streambuf& o, const MsgRecord& msg_record, RecordedMsgStuff& fms)
    {
        if (msg_record.app_data_len > AMI_MAX_MESSAGE_SIZE_INTERNAL)
        {
            LOG_ERROR("unexpected message data len of message record(={1})",
                      msg_record);
            return false;
        }

        ///Message::stream_sqn ~ topic_sqn
        const FileSizeType len_sqn_to_write = sizeof(MsgRecord::stream_sqn) + sizeof(MsgRecord::topic_sqn);
        GAUGE_BEGIN(sputn_gauge_);
        const auto len_sqn_written =
            o.sputn((std::streambuf::char_type*)(&msg_record.stream_sqn),
                    len_sqn_to_write);
        GAUGE_GAUGE(sputn_gauge_);
        if (len_sqn_written < len_sqn_to_write)
        {
            return false;
        }

        ///Message::app_data_len ~ msg_header
        const FileSizeType len_rest_md_to_write =
            sizeof(MsgRecord::app_data_len)
            + sizeof(MsgRecord::msg_header)
            + sizeof(MsgRecord::ex_msg_header);
        GAUGE_BEGIN(sputn_gauge_);
        const auto len_rest_md_written =
            o.sputn((std::streambuf::char_type*)(&msg_record.app_data_len),
                    len_rest_md_to_write);
        GAUGE_GAUGE(sputn_gauge_);
        if (len_rest_md_written < len_rest_md_to_write)
        {
            return false;
        }

        ///Message::app_data
        const FileSizeType len_data_to_write = msg_record.app_data_len;
        GAUGE_BEGIN(sputn_gauge_);
        const auto len_data_written =
            o.sputn((std::streambuf::char_type*)(msg_record.app_data),
                    len_data_to_write);
        GAUGE_GAUGE(sputn_gauge_);
        if (len_data_written < len_data_to_write)
        {
            return false;
        }

        if (IsUseCRC())
        {  //crc的计算略过Message::ex_msg_header的部分
            crc_ = MsgCRCCalFunc(&msg_record.stream_sqn,
                                 sizeof(msg_record.stream_sqn)
                                     + sizeof(msg_record.topic_sqn),
                                 crc_);
            crc_ = MsgCRCCalFunc(&msg_record.app_data_len,
                                 sizeof(msg_record.app_data_len), crc_);
            crc_ = MsgCRCCalFunc(&msg_record.msg_header,
                                 sizeof(msg_record.msg_header), crc_);
            crc_ = MsgCRCCalFunc(msg_record.app_data,
                                 msg_record.app_data_len, crc_);
        }

        return true;
    }

    bool WriteCRC(std::streambuf& o)
    {

        const FileSizeType len_to_write = sizeof(MsgCRCType);
        GAUGE_BEGIN(sputn_gauge_);
        const auto len_written = o.sputn((const char*)GetRecordingMsg().AddCRC(crc_),
                                         len_to_write);
        GAUGE_GAUGE(sputn_gauge_);
        if (len_written < len_to_write)
        {
            return false;
        }

        return true;
    }

    virtual ErrorCode_def FetchThenRecordOneMessage();

    ErrorCode_def TruncateIndexFile(FileWriteBuffer* index_filebuf,
                                    Message::SqnType new_last_msg_sqn);

    ErrorCode_def RecoverIndexFile(FileWriteBuffer* index_filebuf,
                                   Message::SqnType& last_msg_sqn);

    ErrorCode_def RecoverIndexFile(FileWriteBuffer* index_filebuf,
                                   const Message::SqnType& last_msg_sqn,
                                   const LostMsg&);

    virtual ErrorCode_def RecoverIndexDataFiles();
    virtual ErrorCode_def RecoverIndexDataFiles(const LostMsg&) = 0;

    template <typename KeyType>
    FileWriteBuffer* CreateKeyindexFilebuf(const KeyType& key_value)
    {
        Index type_identifier;// This is a tradeoff for clang & icc, since "new (Index())" can be interpreted as a new statement of function-pointer type.
        FileWriteBuffer* key_value_buffer = new (type_identifier)
            FileWriteBuffer(GetKeyindexFilePath(data_path_, key_value), *this,
                            FileWriteBuffer::AfterOverFlowOpType(),
                            OrdinalIndex::ValueSize());
        if (nullptr == key_value_buffer->Open(std::ios_base::binary | std::ios_base::out))
        {
            LOG_ERROR("create key index file buffer {1} failed.",
                      key_value_buffer->GetFilePathStr());
            return nullptr;
        }
        else
        {
            LOG_INFO("create key index file buffer {1} ok.",
                     key_value_buffer->GetFilePathStr());
            return key_value_buffer;
        }
    }

    template <typename KeyType, typename IndexType>
    FileWriteBuffer* GetFileWriteBuffer(const KeyType& key_value,
                                        const IndexType& index_item,
                                        RecordedMsgStuff& fms)
    {
        FileWriteBuffer* key_value_buffer = nullptr;

        if (!keyindex_type_filebuf_map_.count(KeyType::TypeHashCode()))
        {  //该关键字索引第一次出现
            keyindex_type_filebuf_map_.emplace(std::make_pair(KeyType::TypeHashCode(),
                                                              KeyvalueFilebufMapType()));
        }

        if (!keyindex_type_filebuf_map_.at(KeyType::TypeHashCode())
                 .count(key_value.HashCode()))
        {  //该键值第一次出现
            key_value_buffer = CreateKeyindexFilebuf(key_value);
            if (!key_value_buffer)
            {
                return nullptr;
            }

            keyindex_type_filebuf_map_.at(KeyType::TypeHashCode())
                .emplace(std::make_pair(key_value.HashCode(),
                                        key_value_buffer));

            /**
             * 此处将形成以ordinal_index_file_buf_为头的一个tie链
             * 只要ordinal_index_file_buf_写磁盘，则会触发该链上的
             * 所有buffer都写磁盘，最后消息数据写磁盘
             */
            FileWriteBuffer* old_tiee = ordinal_index_file_buf_->Tie(key_value_buffer);
            key_value_buffer->Tie(old_tiee);
        }

        if (!key_value_buffer)
        {
            key_value_buffer = keyindex_type_filebuf_map_.at(KeyType::TypeHashCode())
                                   .at(key_value.HashCode());
        }

        assert(key_value_buffer);
        return key_value_buffer;
    }

    template <typename KeyType, typename IndexType>
    bool WriteMsgKeyIndex(const KeyType& key_value,
                          const IndexType& index_item,
                          RecordedMsgStuff& fms)
    {
        FileWriteBuffer* key_value_buffer = GetFileWriteBuffer(key_value, index_item, fms);
        if (key_value_buffer == nullptr)
            return false;

        return WriteMsgOrdinalIndex(*key_value_buffer, index_item, fms);
    }

    template <typename KeyType>
    int32_t UpdateLastMsgSqn(const KeyType& key_value, const OrdinalIndex& oridnal_index)
    {
        if (!keyindex_type_sqn_map_.count(KeyType::TypeHashCode()))
        {  //该关键字索引第一次出现
            keyindex_type_sqn_map_.emplace(std::make_pair(KeyType::TypeHashCode(),
                                                          KeyvalueSqnMapType()));
        }

        if (!keyindex_type_sqn_map_.at(KeyType::TypeHashCode())
                 .count(key_value.HashCode()))
        {  //该键值第一次出现

            keyindex_type_sqn_map_.at(KeyType::TypeHashCode())
                .emplace(std::make_pair(key_value.HashCode(),
                                        AmiRecorderBase::kBegin));
        }
        else
        {
            keyindex_type_sqn_map_.at(KeyType::TypeHashCode())[key_value.HashCode()]++;
        }

        auto it = rewrite_partial_index_file_map_.find(key_value.HashCode());
        if (it == rewrite_partial_index_file_map_.end())
        {
            auto& buffer_write = rewrite_partial_index_file_map_[key_value.HashCode()];
            std::string index_file_path_str = 
                (boost::locale::format("{1}/{2}-{3}_{4}") % data_path_ % KeyType::KeyTypeName() 
                                                          % std::to_string(key_value.HashCode())
                                                          % kIndexFileName).str();
            if (!buffer_write.Open(index_file_path_str.c_str()))
            {
                LOG_ERROR("open file <{1}> failed, errno <{2}>, desc <{3}>",
                          index_file_path_str, errno, ::strerror(errno));
                return ErrorCode::kFailure;
            }

            // FIXME: handle error
            it = rewrite_partial_index_file_map_.find(key_value.HashCode());
        }

        if (RewriteIndexFile(it->second, oridnal_index) != ErrorCode::kSuccess)
        {
            LOG_ERROR("write on file id <{1}> failed, errno <{2}>, desc <{3}>",
                      it->first, errno, ::strerror(errno));
            return ErrorCode::kFailure;
        }

        return ErrorCode::kSuccess;
    }

    template <typename KeyIndexType>
    ErrorCode_def RecoverKeyIndexFile(const boost::filesystem::path& file_path,
                                      KeyIndexType& key_value,
                                      Message::SqnType& last_msg_sqn,
                                      FileWriteBuffer** out_file_buffer = nullptr)
    {
        std::string file_name = file_path.filename().string();
        boost::regex key_index_re((boost::locale::format("{1}-([0-9]+)_{2}")
                                   % KeyIndexType::KeyTypeName()
                                   % kIndexFileName)
                                      .str());
        boost::cmatch what;
        if (boost::regex_match(file_name.c_str(), what, key_index_re))
        {
            LOG_DEBUG("locate '{1}' key index file '{2}'",
                      KeyIndexType::KeyTypeName(), file_path);

            HashMapKeyType key_value_hash_code = 0;
            std::string key_value_hash_code_str(what[1].first, what[1].second);
            try
            {
                key_value_hash_code = boost::lexical_cast<HashMapKeyType>(key_value_hash_code_str);
            }
            catch (const boost::bad_lexical_cast&)
            {
                assert(false); /*should not reach here*/
            }

            key_value = KeyIndexType(key_value_hash_code);

            if (!keyindex_type_filebuf_map_.count(KeyIndexType::TypeHashCode()))
            {
                keyindex_type_filebuf_map_.emplace(std::make_pair(KeyIndexType::TypeHashCode(),
                                                                  KeyvalueFilebufMapType()));
            }

            FileWriteBuffer* key_value_buffer = CreateKeyindexFilebuf(key_value);
            if (nullptr == key_value_buffer)
            {
                return kFailure;
            }
            if (out_file_buffer != nullptr)
            {
                *out_file_buffer = key_value_buffer;
            }

            IF_ERR_RET(RecoverIndexFile(key_value_buffer, last_msg_sqn),
                       key_value_buffer->Delete(key_value_buffer, Index()));
            keyindex_type_filebuf_map_.at(KeyIndexType::TypeHashCode())
                .emplace(std::make_pair(key_value.HashCode(),
                                        key_value_buffer));

            /**
             * 此处将形成以ordinal_index_file_buf_为头的一个tie链
             * 只要ordinal_index_file_buf_写磁盘，则会触发该链上的
             * 所有buffer都写磁盘，最后消息数据写磁盘
             */
            // ordinal_index_file_buf_->key_value_buffer
            // old_tiee = ordinal_index_file_buf_->tiee_
            FileWriteBuffer* old_tiee = ordinal_index_file_buf_->Tie(key_value_buffer);
            // ordinal_index_file_buf_->key_value_buffer->old_tiee
            // always place ordinal_index_file_buf_ at the list head
            key_value_buffer->Tie(old_tiee);
        }
        else
        {
            return kTryAgain; /*也许是索引类型不匹配，试试其他索引类型*/
        }

        return kSuccess;
    }

    template <typename KeyIndexType>
    ErrorCode_def RecoverKeyIndexFile(const boost::filesystem::path& file_path,
                                      const KeyIndexType&,
                                      const LostMsg& lost_msg_tag)
    {
        std::string file_name = file_path.filename().string();
        boost::regex key_index_re((boost::locale::format("{1}-([0-9]+)_{2}")
                                   % KeyIndexType::KeyTypeName()
                                   % kIndexFileName)
                                      .str());
        boost::cmatch what;
        if (boost::regex_match(file_name.c_str(), what, key_index_re))
        {
            LOG_DEBUG("locate '{1}' key index file '{2}'",
                      KeyIndexType::KeyTypeName(), file_path);

            HashMapKeyType key_value_hash_code = 0;
            std::string key_value_hash_code_str(what[1].first, what[1].second);
            try
            {
                key_value_hash_code = boost::lexical_cast<HashMapKeyType>(key_value_hash_code_str);
            }
            catch (const boost::bad_lexical_cast&)
            {
                assert(false); /*should not reach here*/
            }

            const auto& key_value = KeyIndexType(key_value_hash_code);

            if (!keyindex_type_sqn_map_.count(KeyIndexType::TypeHashCode())
                || !keyindex_type_sqn_map_.at(KeyIndexType::TypeHashCode()).count(key_value_hash_code))
            {
                boost::system::error_code bs_ec;
                LOG_DEBUG("remove locate '{1}' key index file '{2}'",
                          KeyIndexType::KeyTypeName(), file_path);
                boost::filesystem::remove(file_path, bs_ec);
            }
            else
            {
                FileWriteBuffer* key_value_buffer = CreateKeyindexFilebuf(key_value);
                if (nullptr == key_value_buffer)
                {
                    return kFailure;
                }

                const auto last_msg_sqn =
                    keyindex_type_sqn_map_.at(KeyIndexType::TypeHashCode()).at(key_value_hash_code);

                IF_ERR_RET(RecoverIndexFile(key_value_buffer, last_msg_sqn, lost_msg_tag),
                           key_value_buffer->Delete(key_value_buffer, Index()));

                if (!keyindex_type_filebuf_map_.count(KeyIndexType::TypeHashCode()))
                {
                    keyindex_type_filebuf_map_.emplace(std::make_pair(KeyIndexType::TypeHashCode(),
                                                                      KeyvalueFilebufMapType()));
                }

                keyindex_type_filebuf_map_.at(
                                              KeyIndexType::TypeHashCode())
                    .emplace(
                        std::make_pair(key_value.HashCode(), key_value_buffer));

                /**
                 * 此处将形成以ordinal_index_file_buf_为头的一个tie链
                 * 只要ordinal_index_file_buf_写磁盘，则会触发该链上的
                 * 所有buffer都写磁盘，最后消息数据写磁盘
                 */
                FileWriteBuffer* old_tiee = ordinal_index_file_buf_->Tie(key_value_buffer);
                key_value_buffer->Tie(old_tiee);
            }
        }
        else
        {
            return kTryAgain; /*也许是索引类型不匹配，试试其他索引类型*/
        }

        return kSuccess;
    }

private:
    MessageTrack(const MessageTrack&) = delete;
    MessageTrack& operator=(const MessageTrack&) = delete;

    bool WriteFileHeader()
    {
        if (!msg_data_file_header_.Write(*msg_data_file_buf_))
        {
            LOG_ERROR("write header of file '{1}' failed",
                      msg_data_file_buf_->GetFilePathStr());
            return false;
        }
        else
        {
            LOG_INFO("write header of file '{1}' ok: {2}",
                     msg_data_file_buf_->GetFilePathStr(),
                     msg_data_file_header_);
        }

        return true;
    }

    std::string GetMsgPtrQueueName() const
    {
        return (boost::locale::format("{1}_{2}")
                % kDataQueuePrefix
                % path_)
            .str();
    }

    template <class T>
    void CascadeThenCheckConfig(const Property& request, const std::string& key, T& config_value,
                                const T& default_value,
                                boost::optional<T> low_limit = boost::none,
                                boost::optional<T> up_limit  = boost::none);

    virtual char GetTypeChar() const                   = 0;
    virtual Message::SizeType GetMessageFixLen() const = 0;

    virtual bool WriteMessage(std::streambuf& o,
                              const MsgRecord& msg_record,
                              RecordedMsgStuff& fms) = 0;

    virtual bool RecordOneMessage(MsgRecord& msg_record,
                                  MQMsgEntry* entry         = nullptr,
                                  adk::MPSCQueue* msg_queue = nullptr) = 0;

    virtual void DoReleaseMessage(AmiMessage& ami_msg) const = 0;

    virtual bool DoSyncToFile()
    {
        bool ret = true;
        if (ordinal_index_file_buf_)
        {
            ret = ordinal_index_file_buf_->Sync();           
            /**
             * 因为可能存在被滤重的消息，因此当上面的Sync中没有消息需
             * 要落地时（全部是被滤重而不需要落地的消息），sync不会引发
             * FreeAllMessages，因此需要显式调用FreeAllMessages
             */
            FreeAllMessages();
        }

        return ret;
    }

    virtual ErrorCode_def FilterMessage(const MsgRecord* msg_record,
                                        MQMsgEntry* entry) = 0;

    virtual ErrorCode_def GetLastMessageLen(const Message::SqnType& last_msg_sqn,
                                            Message::SizeType& last_msg_len) const = 0;

    SerialWorker::JobStatus RecordMessage();

protected:
    AMI_TD_DEF_FLAG(ami_test_sync_control_);
    std::string path_;
    std::string data_path_;  //已经包含了path_
    uint32_t weight_       = 0;
    size_t msg_queue_size_ = 0;
    bool use_msg_crc_      = false;

    adk::MPManager app_msg_mp_manager_;
    adk::MPSCQueue* msg_ptr_queue_ = nullptr;
    SerialWorker* record_worker_   = nullptr;

    std::vector<RecordedMsgStuff> msgs_to_free_;
    FileWriteBuffer* msgdata_filebuf_to_recover_ = nullptr;  ///< 需要故障恢复的buffer
    bool recovery_ok_                            = false;  ///恢复成功的标志
    bool error_happened_                         = false;  ///< 因为错误而停止了
    bool can_quit_elegantly_                     = false;
    bool stopped_                                = true;
    bool rebuilt_                                = true;
    bool ignore_lost_msg_                        = false;  //可能丢消息
    bool is_already_stop_track_                  = false;
    bool is_doing_record_                        = false;

    /***************************************************************************
     * 在故障恢复时需要正确复位的状态量
     */
    FileWriteBuffer* ordinal_index_file_buf_ = nullptr;
    FileWriteBuffer* msg_data_file_buf_      = nullptr;

    ///<当前正在写的物理消息数据文件的序号
    size_t cur_msgdata_filesqn_ = 0;

    ///< 当前拟落地的消息在消息数据文件中的相对位置（不算文件头，起点位于文件头之后）
    FilePosType cur_msgdata_filepos_ = 0;

    ///< 包括预分配部分的文件长度（不算文件头，起点位于文件头之后）
    FilePosType msgdata_len_inc_prealloc_ = 0;

    Message::SqnType recorded_msg_cnt_ = 0;
    KeyindexTypeFilebufMap keyindex_type_filebuf_map_;
    /**************************************************************************/

    KeyindexTypeSqnMap keyindex_type_sqn_map_;

    Message::SqnType msg_cnt_before_process_mq_ = 0;

    /***************************************************************************
     * 一些指标
     */
    Message::SqnType filtered_msg_cnt_    = 0;
    Message::SqnType filtered_msg_cnt_repair_ = 0;
    unsigned long long no_work_sleep_cnt_ = 0;
    unsigned long long yield_cnt_         = 0;
    /**************************************************************************/

    MsgCRCType crc_;
    RecordFileHdr msg_data_file_header_;
    IntervalLogger no_msg_inv_logger_;

    // map key is stream_id/transport_id/endpoint_id
    std::map<uint32_t, BufferWrite> rewrite_partial_index_file_map_;

    // ordinal_index buffer_write
    BufferWrite ordinal_index_write_;

    int32_t RewriteIndexFile(BufferWrite& buffer_write, const OrdinalIndex& index)
    {
        if (!buffer_write.Write(index.ValueBegin(), index.ValueSize()))
        {
            return ErrorCode::kFailure;
        }

        return ErrorCode::kSuccess;
    }

    int32_t RewriteIndexFileDone()
    {
        for (auto& node : rewrite_partial_index_file_map_)
        {
            if (!node.second.Close())
            {
                LOG_ERROR("write/close on file id <{1}> failed, errno <{2}>, desc <{3}>",
                          node.first, errno, ::strerror(errno));
                return ErrorCode::kFailure;
            }
        }

        if (ordinal_index_write_.Good())
        {
            if (!ordinal_index_write_.Close())
            {
                LOG_ERROR("write/close on file <{1}> failed, errno <{2}>, desc <{3}>",
                          data_path_ + "/index", errno, ::strerror(errno));
                return ErrorCode::kFailure;
            }
        }
        return ErrorCode::kSuccess;
    }

    int32_t OpenOrdinalIndexBufferWrite()
    {
        std::string index_file_path_str = data_path_ + "/index";
        if (!ordinal_index_write_.Open(index_file_path_str.c_str()))
        {
            LOG_ERROR("open file <{1}> failed, errno <{2}>, desc <{3}>",
                      index_file_path_str, errno, ::strerror(errno));
            return ErrorCode::kFailure;
        }
        return ErrorCode::kSuccess;
    }

private:
#ifdef AMI_PERFORMANCE_PROFILE
    DelayGauge null_gauge_ {"null"};
    DelayGauge record_one_msg_gauge_ {"record_one_msg"};
    DelayGauge sputn_gauge_ {"sputn"};
    DelayGauge writev_gauge_ {"writev"};
    friend class Recorder;
#endif

    friend class FileWriteBuffer;
    friend class FileWriteBuffer::FileBuffer;
    friend std::ostream& operator<<(std::ostream&, const MessageTrack&);
    LOG_DECLARE
};

#ifdef AMI_PERFORMANCE_PROFILE
inline std::ostream& operator<<(std::ostream& os, const MessageTrack& track)
{
    track.Dump(os);

    os << "\n";
    os << track.null_gauge_ << "\n"
       << track.record_one_msg_gauge_ << "\n"
       << track.sputn_gauge_ << "\n"
       << track.writev_gauge_;

    return os;
}
#else
inline std::ostream& operator<<(std::ostream& os, const MessageTrack& track)
{
    track.Dump(os);
    return os;
}
#endif

}  // namespace ami

#endif /* AMI_MESSAGE_TRACK_H_ */
