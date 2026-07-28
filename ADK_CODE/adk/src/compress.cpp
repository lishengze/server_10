#include <exception>

#include <boost/assign/list_of.hpp>

#include <adk/compress.h>

namespace adk_impl
{

const std::map<int, std::string> Compression::kZlibRetMessages = boost::assign::map_list_of
    (Z_OK, "Z_OK")
    (Z_STREAM_END, "Z_STREAM_END")
    (Z_NEED_DICT, "Z_NEED_DICT")
    (Z_ERRNO, "Z_ERRNO")
    (Z_STREAM_ERROR, "Z_STREAM_ERROR")
    (Z_DATA_ERROR, "Z_DATA_ERROR")
    (Z_MEM_ERROR, "Z_MEM_ERROR")
    (Z_BUF_ERROR, "Z_BUF_ERROR")
    (Z_VERSION_ERROR, "Z_VERSION_ERROR");

const int Compression::kZlibWindowBits = 15;
const int Compression::kGzipWindowBits = 31;
const int Compression::kMemoryLevel = 9;

Compression::Compression(const CompressMethod method, const HandleType type, const int compress_level): compress_stream_ptr_(nullptr), decompress_stream_ptr_(nullptr)
{
    if (compress_level < -1 || compress_level > 9)
    {
        throw std::runtime_error("invalid compress level:"+std::to_string(compress_level));
    }
    if (type & kCompress)
    {
        compress_stream_ptr_ = new z_stream;
        compress_stream_ptr_->zalloc = Z_NULL;
        compress_stream_ptr_->zfree = Z_NULL;
        compress_stream_ptr_->opaque = Z_NULL;
        compress_stream_ptr_->data_type = Z_UNKNOWN;
    }
    if (type & kDecompress)
    {
        decompress_stream_ptr_ = new z_stream;
        decompress_stream_ptr_->zalloc = Z_NULL;
        decompress_stream_ptr_->zfree = Z_NULL;
        decompress_stream_ptr_->opaque = Z_NULL;
        decompress_stream_ptr_->data_type = Z_UNKNOWN;
    }

    int ret = -1;
    if (method == kMethodDeflate)
    {
        if (type & kCompress)
        {
            ret = deflateInit2(compress_stream_ptr_, compress_level, Z_DEFLATED, kZlibWindowBits, kMemoryLevel, Z_DEFAULT_STRATEGY);
            if (ret != Z_OK)
            {
                throw std::runtime_error("deflateInit2 fail:"+GetRetMsg(ret));
            }
        }
        if (type & kDecompress)
        {
            ret = inflateInit2(decompress_stream_ptr_, kZlibWindowBits);
            if (ret != Z_OK)
            {
                throw std::runtime_error("inflateInit2 fail:"+GetRetMsg(ret));
            }
        }
    }
    else if (method == kMethodGzip)
    {
        if (type & kCompress)
        {
            ret = deflateInit2(compress_stream_ptr_, compress_level, Z_DEFLATED, kGzipWindowBits, kMemoryLevel, Z_DEFAULT_STRATEGY);
            if (ret != Z_OK)
            {
                throw std::runtime_error("deflateInit2 fail:"+GetRetMsg(ret));
            }
        }
        if (type & kDecompress)
        {
            ret = inflateInit2(decompress_stream_ptr_, kGzipWindowBits);
            if (ret != Z_OK)
            {
                throw std::runtime_error("inflateInit2 fail:"+GetRetMsg(ret));
            }
        }
    }
}

Compression::~Compression()
{
    if (compress_stream_ptr_)
    {
        deflateEnd(compress_stream_ptr_);
        delete compress_stream_ptr_;
        compress_stream_ptr_ = nullptr;
    }

    if (decompress_stream_ptr_)
    {
        inflateEnd(decompress_stream_ptr_);
        delete decompress_stream_ptr_;
        decompress_stream_ptr_ = nullptr;
    }
}


} // namespace adk_impl
