#ifndef ADK_IMPL_COMPRESS_H_
#define ADK_IMPL_COMPRESS_H_

#include <map>
#include <string>
#include <vector>
#include <zlib.h>

#ifndef z_const
#define z_const 
#endif

namespace adk_impl
{
    
class Compression
{
public:

    enum CompressMethod
    {
        kMethodDeflate = 0,
        kMethodGzip
    };

    enum HandleType
    {
        kNotOp = 0,
        kCompress = 1,
        kDecompress = 2,
        kBoth = 3
    };

    enum CompressLevel
    {
        kDefaultCompression = -1,
        kNoCompression = 0,
        kBestSpeed = 1,
        kBestCompression = 9
    };

    /**
    *@brief             构造函数
    *
    *@method            压缩与解压使用的方法
    *
    *@type              操作类型, 压缩, 解压或者两者
    *
    *@compress_level    压缩级别, 范围为:-1到9, 越高越耗时   
    *
    *@exception         runtime_error            
    */
    Compression(const CompressMethod method, const HandleType type = kCompress, const int compress_level = kDefaultCompression);

    Compression(const Compression &other) = delete;

    Compression& operator=(const Compression &other) = delete;

    ~Compression();

    /**
    *@brief       压缩数据
    *
    *@in_data     输入数据
    *
    *@in_len      输入数据长度
    *
    *@out_data    输出数据   
    *
    *@return      成功返回0
    */
    inline int Compress(const unsigned char *in_data, const size_t in_len, std::string &out_data)
    {
        return ZlibCompress(in_data, in_len, out_data);
    }

    /**
    *@brief       解压数据
    *
    *@in_data     输入数据
    *
    *@in_len      输入数据长度
    *
    *@out_data    输出数据   
    *
    *@return      成功返回0
    */
    inline int Decompress(const unsigned char *in_data, const size_t in_len, std::string &out_data)
    {
        return ZlibDecompress(in_data, in_len, out_data);
    }
    
    /**
    *@brief       压缩数据
    *
    *@in_data     输入数据
    *
    *@in_len      输入数据长度
    *
    *@out_data    输出数据   
    *
    *@return      成功返回0
    */
    inline int Compress(const unsigned char *in_data, const size_t in_len, std::vector<unsigned char> &out_data)
    {
        return ZlibCompress(in_data, in_len, out_data);
    }

    /**
    *@brief       解压数据
    *
    *@in_data     输入数据
    *
    *@in_len      输入数据长度
    *
    *@out_data    输出数据   
    *
    *@return      成功返回0
    */
    inline int Decompress(const unsigned char *in_data, const size_t in_len, std::vector<unsigned char> &out_data)
    {
        return ZlibDecompress(in_data, in_len, out_data);
    }
  
    /**
    *@brief       根据输入值返回对于的状态码描述
    *
    *@ret_code    返回值 
    *
    *@return      状态码描述
    */    
    inline static std::string GetRetMsg(const int ret_code)
    {
        auto const it = kZlibRetMessages.find(ret_code);
        return it != kZlibRetMessages.cend() ? it ->second : std::string(); 
    }

private:
    template<class T>
    int ZlibCompress(const unsigned char *in_data, const size_t in_len, T &out_data);
    
    template<class T>
    int ZlibDecompress(const unsigned char *in_data, const size_t in_len, T &out_data);

    static const std::map<int, std::string> kZlibRetMessages;
    static const int kZlibWindowBits;
    static const int kGzipWindowBits;
    static const int kMemoryLevel;
    z_stream *compress_stream_ptr_;
    z_stream *decompress_stream_ptr_;
    size_t compress_out_buf_size_ = 1024*32;
    size_t decompree_out_buf_szie_ = 1024*32;
};

template<class T>
int Compression::ZlibCompress(const unsigned char *in_data, const size_t in_len, T &out_data)
{
    int ret = -1;
    out_data.resize(compress_out_buf_size_);
    compress_stream_ptr_->next_in = const_cast<z_const unsigned char *>(in_data);
    compress_stream_ptr_->avail_in = in_len;
    size_t out_len = 0;
    
    while (true)
    {
        compress_stream_ptr_->next_out = reinterpret_cast<unsigned char *>(&out_data[out_len]);
        compress_stream_ptr_->avail_out = compress_out_buf_size_ - out_len;

        ret = deflate(compress_stream_ptr_, Z_FINISH);

        if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR)
        {
            return ret;  
        }

        out_len = compress_out_buf_size_ - compress_stream_ptr_->avail_out;

        if (ret == Z_STREAM_END)
        {
            deflateReset(compress_stream_ptr_);
            break;
        }
        if (compress_stream_ptr_->avail_out == 0)
        {
            compress_out_buf_size_ *= 2;
            out_data.resize(compress_out_buf_size_);
        }
    }

    compress_out_buf_size_ = out_len + 150; // +150byte数据浮动
    out_data.resize(out_len);

    return Z_OK;
}

template<class T>
int Compression::ZlibDecompress(const unsigned char *in_data, const size_t in_len, T &out_data)
{
    int ret = -1;
    out_data.resize(decompree_out_buf_szie_);
    decompress_stream_ptr_->next_in = const_cast<z_const unsigned char *>(in_data);
    decompress_stream_ptr_->avail_in = in_len;
    size_t out_len = 0;
    
    while (true)
    {
        decompress_stream_ptr_->next_out = reinterpret_cast<unsigned char *>(&out_data[out_len]);
        decompress_stream_ptr_->avail_out = decompree_out_buf_szie_ - out_len;
        ret = inflate(decompress_stream_ptr_, Z_FINISH);

        if  (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR)
        {
            return ret;
        }

        out_len = decompree_out_buf_szie_ - decompress_stream_ptr_->avail_out;
        
        if (ret == Z_STREAM_END)
        {
            inflateReset(decompress_stream_ptr_);
            break;
        }
        if (decompress_stream_ptr_->avail_out == 0)
        {
            decompree_out_buf_szie_ *= 2;
            out_data.resize(decompree_out_buf_szie_);
        }
    }
    decompree_out_buf_szie_ = out_len + 150; //+150byte数据浮动
    out_data.resize(out_len);
    return Z_OK;   
}

} // namespace adk_impl

#endif