/**
 * @author 陈志(chenzhi@af.local)
 */
#ifndef AMI_RECORD_ITER_H_
#define AMI_RECORD_ITER_H_

///< cpp std
#include <iterator>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

///< boost
#include <boost/filesystem.hpp>

///ami public
#include <ami/ami_recorder_base.h>

///< ami impl
#include "../ami_message.h"
#include "../log.h"

///< impl
#include "record_reader.h"
#include "recorder_fwd.h"

namespace ami
{

template <typename TrackType>
RecordIterator<TrackType> operator+(const RecordIterator<TrackType>&,
                                    typename RecordIterator<TrackType>::difference_type);

template <typename TrackType>
class RecordIterator
    : public std::iterator<std::random_access_iterator_tag,
                           AmiMessage,
                           typename std::make_signed<Message::SqnType>::type,
                           AmiMessage*,
                           AmiMessage&>
{
public:
    typedef Message::SqnType pos_type;

public:
    /**
     * 初始化一个表示eof的迭代器，访问该迭代器将导致未定义行为
     */
    RecordIterator() {}
    RecordIterator(const RecordIterator& rhs)
    {
        if (!rhs.IsEof())
        {
            reader_.reset(new RecordReader());
            reader_->Reset(rhs.TrackDataPath());

            if (kSuccess != reader_->ReadHistMessage<TrackType>(TrackDataPath().string(), rhs.CurrentPos() - 1, rhs.CurrentPos(), [](AmiMessage*) -> ErrorCode { return kSuccess; }))
            {
                reader_.reset();
            }
        }
    }

    RecordIterator& operator=(const RecordIterator& rhs)
    {
        if (IsEof() && rhs.IsEof())
        {
            return *this;
        }

        if (rhs.IsEof())
        {
            reader_.reset();
            return *this;
        }

        if (reader_ == rhs.reader_
            || !reader_)
        {
            reader_.reset(new RecordReader());
            reader_->Reset(rhs.TrackDataPath());
        }

        if (TrackDataPath() != rhs.TrackDataPath())
        {
            reader_->Reset(rhs.TrackDataPath());
        }

        if (kSuccess != reader_->ReadHistMessage<TrackType>(TrackDataPath().string(), rhs.CurrentPos() - 1, rhs.CurrentPos(), [](AmiMessage*) -> ErrorCode { return kSuccess; }))
        {
            reader_.reset();
        }

        return *this;
    }

    RecordIterator(RecordIterator&& rhs) = default;
    RecordIterator& operator=(RecordIterator&& rhs) = default;

    const value_type& operator*() const
    {
        return *(reader_->ami_msg_);
    }

    const value_type* operator->() const
    {
        return reader_->ami_msg_;
    }

    bool operator==(const RecordIterator& rhs) const
    {
        if (IsEof() && rhs.IsEof())
        {
            return true;
        }

        if (!IsEof() && !rhs.IsEof()
            && TrackDataPath() == rhs.TrackDataPath()
            && CurrentPos() == rhs.CurrentPos())
        {
            return true;
        }

        return false;
    }

    bool operator!=(const RecordIterator& rhs) const
    {
        return !(this->operator==(rhs));
    }

    bool operator<(const RecordIterator& rhs) const
    {
        if (!IsEof() && rhs.IsEof())
        {
            return true;
        }

        if (!IsEof() && !rhs.IsEof()
            && TrackDataPath() == rhs.TrackDataPath()
            && CurrentPos() < rhs.CurrentPos())
        {
            return true;
        }

        return false;
    }

    bool operator<=(const RecordIterator& rhs) const
    {
        if (*this == rhs || *this < rhs)
        {
            return true;
        }

        return false;
    }

    bool operator>(const RecordIterator& rhs) const
    {
        if (IsEof() && !rhs.IsEof())
        {
            return true;
        }

        if (!IsEof() && !rhs.IsEof()
            && TrackDataPath() == rhs.TrackDataPath()
            && CurrentPos() > rhs.CurrentPos())
        {
            return true;
        }

        return false;
    }

    bool operator>=(const RecordIterator& rhs) const
    {
        if (*this == rhs || *this > rhs)
        {
            return true;
        }

        return false;
    }

    RecordIterator& operator++()
    {
        if (!reader_->ReadNextMessage<TrackType>())
        {
            reader_.reset();
        }
        else
        {
            CurrentPos()++;
        }

        return *this;
    }

    RecordIterator operator++(int)
    {
        RecordIterator res = *this;
        this->operator++();
        return res;
    }

    RecordIterator& operator+=(difference_type n)
    {
        if (IsEof())
        {
            return *this;
        }

        const auto target = Advance(n);

        if (target.second)
        {
            reader_.reset();
        }
        else if (kSuccess != reader_->ReadHistMessage<TrackType>(TrackDataPath().string(), target.first, target.first + 1, [](AmiMessage*) -> ErrorCode { return kSuccess; }))
        {
            reader_.reset();
        }

        return *this;
    }

    RecordIterator& operator-=(difference_type n)
    {
        return *this += (-n);
    }

    difference_type operator-(const RecordIterator& rhs) const
    {
        if (!IsEof() && !rhs.IsEof()
            && TrackDataPath() == rhs.TrackDataPath())
        {
            return CurrentPos() - rhs.CurrentPos();
        }

        return std::numeric_limits<difference_type>::max();
    }

    RecordIterator operator-(difference_type n) const
    {
        return *this + (-n);
    }

    RecordIterator& operator--()
    {
        return *this -= 1;
    }

    RecordIterator operator--(int)
    {
        RecordIterator res = *this;
        this->operator--();
        return res;
    }

protected:
    bool IsEof() const
    {
        if (!reader_ || reader_->is_eof_)
        {
            return true;
        }

        return false;
    }

    pos_type& CurrentPos()
    {
        return reader_->cur_msg_sqn_;
    }

    const pos_type& CurrentPos() const
    {
        return reader_->cur_msg_sqn_;
    }

    const boost::filesystem::path& TrackDataPath() const
    {
        return reader_->track_data_path_;
    }

    std::pair<pos_type, bool> Advance(difference_type n) const
    {
        bool overflow       = false;
        pos_type target_sqn = (CurrentPos() - 1) + n;

        if (n < 0 && target_sqn > (CurrentPos() - 1))
        {
            overflow = true;
        }

        if (n > 0 && target_sqn < (CurrentPos() - 1))
        {
            overflow = true;
        }

        if (AmiRecorderBase::kMostRecent == target_sqn)
        {
            overflow = true;
        }

        return std::make_pair(target_sqn, overflow);
    }

private:
    RecordIterator(const boost::filesystem::path& track_path,
                   pos_type sqn)
        : reader_(new RecordReader())
    {
        reader_->Reset(track_path);
        auto begin = sqn;
        auto end   = sqn + 1;
        if (AmiRecorderBase::kMostRecent == sqn)
        {
            end = begin;
        }

        if (kSuccess != reader_->ReadHistMessage<TrackType>(TrackDataPath().string(), begin, end, [](AmiMessage*) -> ErrorCode { return kSuccess; }))
        {
            reader_.reset();
        }
    }

protected:
    std::unique_ptr<RecordReader> reader_;

    friend class RxRecordChannel;
    friend class TxRecordChannel;

    friend RecordIterator operator+<TrackType>(const RecordIterator&, difference_type);
};

template <typename TrackType>
RecordIterator<TrackType> operator+(const RecordIterator<TrackType>& it,
                                    typename RecordIterator<TrackType>::difference_type n)
{
    if (it.IsEof())
    {
        return RecordIterator<TrackType>();
    }

    const auto target = it.Advance(n);
    if (target.second)
    {
        return RecordIterator<TrackType>();
    }
    else
    {
        return RecordIterator<TrackType>(it.TrackDataPath(), target.first);
    }
}

template <typename TrackType>
RecordIterator<TrackType> operator+(typename RecordIterator<TrackType>::difference_type n,
                                    const RecordIterator<TrackType>& it)
{
    return it + n;
}

}  // namespace ami

#endif
