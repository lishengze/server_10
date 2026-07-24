/**
 * @author 陈志(chenzhi@af.local)
 */
#ifndef AMI_RECORD_KEYINDEX_ITER_H_
#define AMI_RECORD_KEYINDEX_ITER_H_

///< cpp std

///< boost

///ami public

///< ami impl

///< impl
#include "record_iterator.h"

namespace ami
{

template <typename TrackType, typename KeyindexType>
KeyindexRecordIterator<TrackType, KeyindexType> operator+(const KeyindexRecordIterator<TrackType, KeyindexType>&,
                                                          typename KeyindexRecordIterator<TrackType, KeyindexType>::difference_type);

template <typename TrackType, typename KeyindexType>
class KeyindexRecordIterator
    : public RecordIterator<TrackType>
{
public:
    typedef typename RecordIterator<TrackType>::iterator_category
        iterator_category;

    typedef typename RecordIterator<TrackType>::value_type value_type;

    typedef typename RecordIterator<TrackType>::difference_type
        difference_type;

    typedef typename RecordIterator<TrackType>::pointer pointer;
    typedef typename RecordIterator<TrackType>::reference reference;
    typedef typename RecordIterator<TrackType>::pos_type pos_type;

public:
    KeyindexRecordIterator() {}

    KeyindexRecordIterator(const KeyindexRecordIterator& rhs)
    {
        if (!rhs.IsEof())
        {
            this->reader_.reset(new RecordReader());
            this->reader_->Reset(rhs.TrackDataPath());
            this->key_value_ = rhs.key_value_;

            RecordReader* reader = this->reader_.get();
            if (kSuccess != reader->ReadHistMessage<TrackType, KeyindexType>(this->TrackDataPath().string(), this->key_value_, rhs.CurrentPos() - 1, rhs.CurrentPos(), [](AmiMessage*) -> ErrorCode { return kSuccess; }))
            {
                this->reader_.reset();
            }
        }
    }

    KeyindexRecordIterator& operator=(const KeyindexRecordIterator& rhs)
    {
        if (this->IsEof() && rhs.IsEof())
        {
            return *this;
        }

        if (rhs.IsEof())
        {
            this->reader_.reset();
            return *this;
        }

        if (this->reader_ == rhs.reader_
            || !this->reader_)
        {
            this->reader_.reset(new RecordReader());
            RecordReader* reader = this->reader_.get();
            reader->Reset(rhs.TrackDataPath());
        }

        if (this->TrackDataPath() != rhs.TrackDataPath())
        {
            RecordReader* reader = this->reader_.get();
            reader->Reset(rhs.TrackDataPath());
        }
        this->key_value_ = rhs.key_value_;

        RecordReader* reader = this->reader_.get();
        if (kSuccess != reader->ReadHistMessage<TrackType, KeyindexType>(this->TrackDataPath().string(), this->key_value_, rhs.CurrentPos() - 1, rhs.CurrentPos(), [](AmiMessage*) -> ErrorCode { return kSuccess; }))
        {
            this->reader_.reset();
        }

        return *this;
    }

    KeyindexRecordIterator(KeyindexRecordIterator&& rhs) = default;
    KeyindexRecordIterator& operator=(KeyindexRecordIterator&& rhs) = default;

    bool operator==(const KeyindexRecordIterator& rhs) const
    {
        if (this->IsEof() && rhs.IsEof())
        {
            return true;
        }

        if (!this->IsEof() && !rhs.IsEof()
            && this->TrackDataPath() == rhs.TrackDataPath()
            && this->key_value_ == rhs.key_value_
            && this->CurrentPos() == rhs.CurrentPos())
        {
            return true;
        }

        return false;
    }

    bool operator!=(const KeyindexRecordIterator& rhs) const
    {
        return !(this->operator==(rhs));
    }

    bool operator<(const KeyindexRecordIterator& rhs) const
    {
        if (!this->IsEof() && rhs.IsEof())
        {
            return true;
        }

        if (!this->IsEof() && !rhs.IsEof()
            && this->TrackDataPath() == rhs.TrackDataPath()
            && this->key_value_ == rhs.key_value_
            && this->CurrentPos() < rhs.CurrentPos())
        {
            return true;
        }

        return false;
    }

    bool operator<=(const KeyindexRecordIterator& rhs) const
    {
        if (*this == rhs || *this < rhs)
        {
            return true;
        }

        return false;
    }

    bool operator>(const KeyindexRecordIterator& rhs) const
    {
        if (this->IsEof() && !rhs.IsEof())
        {
            return true;
        }

        if (!this->IsEof() && !rhs.IsEof()
            && this->TrackDataPath() == rhs.TrackDataPath()
            && this->key_value_ == rhs.key_value_
            && this->CurrentPos() > rhs.CurrentPos())
        {
            return true;
        }

        return false;
    }

    bool operator>=(const KeyindexRecordIterator& rhs) const
    {
        if (*this == rhs || *this > rhs)
        {
            return true;
        }

        return false;
    }

    KeyindexRecordIterator& operator++()
    {
        RecordReader* reader = this->reader_.get();
        if (kSuccess != reader->ReadHistMessage<TrackType, KeyindexType>(this->TrackDataPath().string(), this->key_value_, this->CurrentPos(), this->CurrentPos() + 1, [](AmiMessage*) -> ErrorCode { return kSuccess; }))
        {
            this->reader_.reset();
        }

        return *this;
    }

    KeyindexRecordIterator operator++(int)
    {
        KeyindexRecordIterator res = *this;
        this->operator++();
        return res;
    }

    KeyindexRecordIterator& operator+=(difference_type n)
    {
        if (this->IsEof())
        {
            return *this;
        }

        const auto target    = this->Advance(n);
        RecordReader* reader = this->reader_.get();

        if (target.second)
        {
            this->reader_.reset();
        }
        else if (kSuccess != reader->ReadHistMessage<TrackType, KeyindexType>(this->TrackDataPath().string(), this->key_value_, target.first, target.first + 1, [](AmiMessage*) -> ErrorCode { return kSuccess; }))
        {
            this->reader_.reset();
        }

        return *this;
    }

    KeyindexRecordIterator& operator-=(difference_type n)
    {
        return *this += (-n);
    }

    difference_type operator-(const KeyindexRecordIterator& rhs) const
    {
        if (!this->IsEof() && !rhs.IsEof()
            && this->TrackDataPath() == rhs.TrackDataPath()
            && this->key_value_ == rhs.key_value_)
        {
            return this->CurrentPos() - rhs.CurrentPos();
        }

        return std::numeric_limits<difference_type>::max();
    }

    KeyindexRecordIterator operator-(difference_type n) const
    {
        return *this + (-n);
    }

    KeyindexRecordIterator& operator--()
    {
        return *this -= 1;
    }

    KeyindexRecordIterator operator--(int)
    {
        KeyindexRecordIterator res = *this;
        this->operator--();
        return res;
    }

private:
    KeyindexRecordIterator(const boost::filesystem::path& track_path,
                           KeyindexType key_value, pos_type sqn)
        : key_value_(key_value)
    {
        this->reader_.reset(new RecordReader());
        this->reader_->Reset(track_path);
        auto begin = sqn;
        auto end   = sqn + 1;
        if (AmiRecorderBase::kMostRecent == sqn)
        {
            end = begin;
        }

        RecordReader* reader = this->reader_.get();
        if (kSuccess != reader->ReadHistMessage<TrackType, KeyindexType>(this->TrackDataPath().string(), this->key_value_, begin, end, [](AmiMessage*) -> ErrorCode { return kSuccess; }))
        {
            this->reader_.reset();
        }
    }

private:
    KeyindexType key_value_;

    friend class RxRecordChannel;

    friend KeyindexRecordIterator operator+<TrackType, KeyindexType>(const KeyindexRecordIterator&, difference_type);
};

template <typename TrackType, typename KeyindexType>
KeyindexRecordIterator<TrackType, KeyindexType> operator+(const KeyindexRecordIterator<TrackType, KeyindexType>& it,
                                                          typename KeyindexRecordIterator<TrackType, KeyindexType>::difference_type n)
{
    if (it.IsEof())
    {
        return KeyindexRecordIterator<TrackType, KeyindexType>();
    }

    const auto target = it.Advance(n);
    if (target.second)
    {
        return KeyindexRecordIterator<TrackType, KeyindexType>();
    }
    else
    {
        return KeyindexRecordIterator<TrackType, KeyindexType>(it.TrackDataPath(), it.key_value_, target.first);
    }
}

template <typename TrackType, typename KeyindexType>
KeyindexRecordIterator<TrackType, KeyindexType> operator+(typename KeyindexRecordIterator<TrackType, KeyindexType>::difference_type n,
                                                          const KeyindexRecordIterator<TrackType, KeyindexType>& it)
{
    return it + n;
}

}  // namespace ami

#endif
