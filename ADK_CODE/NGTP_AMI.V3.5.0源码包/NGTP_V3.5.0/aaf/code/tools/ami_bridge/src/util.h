#ifndef AMI_UTIL_H_
#define AMI_UTIL_H_

///< posix
#include <pwd.h>
#include <unistd.h>
#include <sys/types.h>

///< cpp std
#include <string>
#include <vector>
#include <chrono>

///< boost
#include <boost/lexical_cast.hpp>

#define IF_ERR_RET(x, ...) \
    if (ami::ErrorCode_def ec = (x)) { __VA_ARGS__; return ec; }

namespace ami{

using std::string;

/**
 * ByteSize，处理单位B、K、M、G
 */
class ByteSize
{
public:
    typedef unsigned long long SizeType;
    
    static constexpr SizeType K_SIZE = 1024u;
    static constexpr SizeType M_SIZE = 1024u * K_SIZE;
    static constexpr SizeType G_SIZE = 1024u * M_SIZE;
    
    ByteSize(SizeType raw_size = 0) : size_(raw_size)
    {}

    /**
     * @param sz_str 如果1B/1b, 1K/1k, 1M/1m, 1G/1g
     *
     * @return 如果输入格式错误，则效果等同于default constructor
     */ 
    ByteSize(const std::string& sz_str);
    
    /**
     * 返回Size,单位为Byte
     */ 
    SizeType RawSize() const
    { return size_; }

    bool operator<(const ByteSize& rh) const
    { return size_ < rh.size_; }

    bool operator<=(const ByteSize& rh) const
    { return size_ <= rh.size_; }
    
    bool operator>(const ByteSize& rh) const
    { return size_ > rh.size_; }
    
    bool operator>=(const ByteSize& rh) const
    { return size_ >= rh.size_; }
    
    bool operator==(const ByteSize& rh) const
    { return size_ == rh.size_; }

    ByteSize operator-(const ByteSize& rh)
    { return ByteSize(size_ - rh.size_); }

    ByteSize operator+(const ByteSize& rh)
    { return ByteSize(size_ + rh.size_); }

private:
    SizeType size_;

    friend inline std::ostream& operator<<(std::ostream&, const ByteSize&);
    friend inline std::istream& operator>>(std::istream&, ByteSize&);
};

inline std::ostream& operator<<(std::ostream& os, const ByteSize& byte_size)
{
    if(byte_size.RawSize() / ByteSize::G_SIZE > 0)
    { os << byte_size.RawSize() / ByteSize::G_SIZE << "G"; }
    else if(byte_size.RawSize() / ByteSize::M_SIZE > 0)
    { os << byte_size.RawSize() / ByteSize::M_SIZE << "M"; }
    else if(byte_size.RawSize() / ByteSize::K_SIZE > 0)
    { os << byte_size.RawSize() / ByteSize::K_SIZE << "K"; }
    else
    { os << byte_size.RawSize() << "B"; }

    return os;
}

inline std::istream& operator>>(std::istream& is, ByteSize& byte_size)
{
    std::string str;
    is >> str;
    byte_size = ByteSize();
    
    if(is && !str.empty())
    {
        //先尝试作为纯整数处理
        try{
            byte_size = ByteSize(boost::lexical_cast<ByteSize::SizeType>(str));
            return is;
        }
        catch(const std::exception&)
        { /*如果错误，则继续下面的尝试*/ }
            
        char unit(str[str.size()-1]);
        std::string size_str(str, 0, str.size()-1);
        ByteSize::SizeType sz = 0;
        try{ sz = std::stoull(str); }
        catch(const std::exception&)
        { return is; }
        
        if('b' == unit || 'B' == unit)
        { byte_size = ByteSize(sz); }
        else if('k' == unit || 'K' == unit)
        { byte_size = ByteSize(sz*ByteSize::K_SIZE); }
        else if('m' == unit || 'M' == unit)
        { byte_size = ByteSize(sz*ByteSize::M_SIZE); }
        else if('g' == unit || 'G' == unit)
        { byte_size = ByteSize(sz*ByteSize::G_SIZE); }
        else
        {}
    }
    
    return is;
}

inline ByteSize::ByteSize(const std::string& sz_str)
        : size_(0)
{
    std::stringstream strstr(sz_str);
    strstr >> *this;
}

} // ami


#endif // AMI_UTIL_H_
