/**
 * @author 陈志(chenzhi@af.local)
 */

///< cpp std
#include <exception>
#include <sstream>

///< linux
#include <dlfcn.h>  //dlopen, dlsym

///< adk, ami public
#include <adk/log.h>
#include <ami/error_code.h>

///< impl
#include "compressor.h"

namespace
{

extern "C" void Log(int level,
                    int code,
                    const char* module_name,
                    const char* function_name,
                    int src_line,
                    const char* message)
{
    ADK_LOG_RAW((adk::log::LogLevel)level, (adk::log::LogCode)code, module_name,
                function_name, src_line, "Ami log", message);
}
}

namespace ami
{
namespace bridge
{

/******************************************************************************
 * CompressorBase
 */
CompressorBase::CompressorBase(const boost::filesystem::path& compress_lib_path,
                               const std::string& compress_config_str)
    : compress_lib_path_(compress_lib_path),
      compress_config_str_(compress_config_str)
{
    LoadApi();
}

CompressorBase::CompressorBase(const CompressorBase& other)
    : compress_lib_path_(other.compress_lib_path_),
      compress_config_str_(other.compress_config_str_),
      dl_handler_(other.dl_handler_)
{
}

void CompressorBase::LoadApi()
{
    dl_handler_ = ::dlopen(compress_lib_path_.c_str(), RTLD_NOW);
    if (nullptr == dl_handler_)
    {
        std::ostringstream error_str;
        error_str << "CAN NOT load compression lib("
                  << compress_lib_path_ << ")";
        throw std::system_error(kConfigError, error_str.str());
    }
}
/******************************************************************************/

/******************************************************************************
 * Compressor
 */
constexpr size_t Compressor::kOutUnitSize;
constexpr const char* Compressor::kAmiCCompressInitFN;
constexpr const char* Compressor::kAmiCCompressFN;
constexpr const char* Compressor::kAmiCCompressEndFN;

Compressor::Compressor(const boost::filesystem::path& compress_lib_path,
                       const std::string& compress_config_str)
    : CompressorBase(compress_lib_path, compress_config_str),
      output_pool_(kOutUnitSize)
{
    LoadApi();

    strm_.reset(new AmiCStream);
    strm_->log_func = Log;
    int ret         = compress_init_func_(
        strm_.get(), compress_config_str_.c_str());
    if (AMIC_OK != ret)
    {
        throw std::system_error(kFailure, "init compress stream failed.");
    }
}
/*
Compressor::Compressor(const Compressor& other)
        : CompressorBase(other),
          compress_init_func_(other.compress_init_func_),
          compress_func_(other.compress_func_),
          compress_end_func_(other.compress_end_func_),
          output_pool_(kOutUnitSize)
{
    strm_.reset(new AmiCStream);
    strm_->log_func = Log;
    int ret = compress_init_func_(
        strm_.get(), compress_config_str_.c_str());
    if(AMIC_OK != ret)
    { throw std::system_error(kFailure, "init compress stream failed."); }
}
*/
Compressor::~Compressor()
{
    compress_end_func_(strm_.get());
}

void Compressor::LoadApi()
{
    CompressorBase::LoadApi();

    compress_init_func_ = (AmiCCompressInitFunc)::dlsym(
        dl_handler_, kAmiCCompressInitFN);
    if (nullptr == compress_init_func_)
    {
        std::ostringstream error_str;
        error_str << "CAN NOT load function(" << kAmiCCompressInitFN
                  << ") from lib(" << compress_lib_path_ << ")";
        throw std::system_error(kFailure, error_str.str());
    }

    compress_func_ = (AmiCCompressFunc)::dlsym(dl_handler_, kAmiCCompressFN);
    if (nullptr == compress_func_)
    {
        std::ostringstream error_str;
        error_str << "CAN NOT load function(" << kAmiCCompressFN
                  << ") from lib(" << compress_lib_path_ << ")";
        throw std::system_error(kFailure, error_str.str());
    }

    compress_end_func_ = (AmiCCompressEndFunc)::dlsym(
        dl_handler_, kAmiCCompressEndFN);
    if (nullptr == compress_end_func_)
    {
        std::ostringstream error_str;
        error_str << "CAN NOT load function(" << kAmiCCompressEndFN
                  << ") from lib(" << compress_lib_path_ << ")";
        throw std::system_error(kFailure, error_str.str());
    }
}

const char* Compressor::Compress(const Message* msg, size_t& len)
{
    out_len_        = 0;
    strm_->avail_in = msg->size();
    strm_->next_in  = msg->const_data();

    do
    {
        strm_->next_out  = &output_pool_[out_len_];
        strm_->avail_out = output_pool_.size() - out_len_;

        int ret = compress_func_(strm_.get());
        if (AMIC_ERRNO == ret)
        {
            std::ostringstream error_str;
            if (strm_->error_info)
            {
                error_str << "compress error: " << strm_->error_info << ret;
            }
            else
            {
                error_str << "compress error: " << ret;
            }

            throw std::system_error(kFailure, error_str.str());
        }

        out_len_ = output_pool_.size() - strm_->avail_out;
        if (AMIC_STREAM_END == ret)
        {
            break;
        }
        if (0 == strm_->avail_out)
        {
            const auto origin_size = output_pool_.size();
            output_pool_.resize(origin_size * 2u);
        }
    } while (true);

    len          = out_len_;
    char* packet = new char[len];
    for (int i = 0; i < len; ++i)
    {
        packet[i] = output_pool_[i];
    }
    return packet;
    // assert(0 == strm_->avail_in);
    // return ConstBuffer(&output_pool_[0], out_len_);
}

// const char* Compressor::Compress(const Message* msg, size_t& len)
// {
//     char* packet_buffer = new char[msg->size()];
//     strm_->avail_in  = msg->size();
//     strm_->next_in   = msg->const_data();
//     strm_->next_out  = packet_buffer;
//     strm_->avail_out = msg->size();

//     int ret = compress_func_(strm_.get());
//     if(AMIC_OK != ret)
//     {
//         std::ostringstream error_str;
//         if(strm_->error_info)
//         { error_str << "compress error: " << strm_->error_info; }
//         else
//         { error_str << "compress error"; }
//         throw std::system_error(kFailure, error_str.str());
//     }
//     len = msg->size() - strm_->avail_out;
//     assert(0 == strm_->avail_in);
//     return packet_buffer;
// }

/******************************************************************************/

/******************************************************************************
 * Decompressor
 */
constexpr const char* Decompressor::kAmiCDecompressInitFN;
constexpr const char* Decompressor::kAmiCDecompressFN;
constexpr const char* Decompressor::kAmiCDecompressEndFN;

Decompressor::Decompressor(const boost::filesystem::path& compress_lib_path,
                           const std::string& compress_config_str)
    : CompressorBase(compress_lib_path, compress_config_str)
{
    LoadApi();

    strm_.reset(new AmiCStream);
    strm_->log_func = Log;
    int ret         = decompress_init_func_(
        strm_.get(), compress_config_str_.c_str());
    if (AMIC_OK != ret)
    {
        throw std::system_error(kFailure, "init decompress stream failed.");
    }
}

Decompressor::Decompressor(const Decompressor& other)
    : CompressorBase(other),
      decompress_init_func_(other.decompress_init_func_),
      decompress_func_(other.decompress_func_),
      decompress_end_func_(other.decompress_end_func_)
{
    strm_.reset(new AmiCStream);
    strm_->log_func = Log;
    int ret         = decompress_init_func_(
        strm_.get(), compress_config_str_.c_str());
    if (AMIC_OK != ret)
    {
        throw std::system_error(kFailure, "init decompress stream failed.");
    }
}

Decompressor::~Decompressor()
{
    decompress_end_func_(strm_.get());
}

void Decompressor::LoadApi()
{
    CompressorBase::LoadApi();

    decompress_init_func_ = (AmiCDecompressInitFunc)::dlsym(
        dl_handler_, kAmiCDecompressInitFN);
    if (nullptr == decompress_init_func_)
    {
        std::ostringstream error_str;
        error_str << "CAN NOT load function(" << kAmiCDecompressInitFN
                  << ") from lib(" << compress_lib_path_ << ")";
        throw std::system_error(kFailure, error_str.str());
    }

    decompress_func_ = (AmiCDecompressFunc)::dlsym(
        dl_handler_, kAmiCDecompressFN);
    if (nullptr == decompress_func_)
    {
        std::ostringstream error_str;
        error_str << "CAN NOT load function(" << kAmiCDecompressFN
                  << ") from lib(" << compress_lib_path_ << ")";
        throw std::system_error(kFailure, error_str.str());
    }

    decompress_end_func_ = (AmiCDecompressEndFunc)::dlsym(
        dl_handler_, kAmiCDecompressEndFN);
    if (nullptr == decompress_end_func_)
    {
        std::ostringstream error_str;
        error_str << "CAN NOT load function(" << kAmiCDecompressEndFN
                  << ") from lib(" << compress_lib_path_ << ")";
        throw std::system_error(kFailure, error_str.str());
    }
}

void Decompressor::Decompress(const char* compressed_data, size_t len, size_t orig_len, Message* msg)
{
    strm_->avail_in  = len;
    strm_->next_in   = compressed_data;
    strm_->next_out  = msg->data();
    strm_->avail_out = orig_len;

    int ret = decompress_func_(strm_.get());
    if (AMIC_OK != ret)
    {
        std::ostringstream error_str;
        if (strm_->error_info)
        {
            error_str << "decompress error: " << strm_->error_info << ret;
        }
        else
        {
            error_str << "decompress error" << ret;
        }

        throw std::system_error(kFailure, error_str.str());
    }

    assert(strm_->avail_in == 0 && strm_->avail_out == 0);
    msg->set_size(orig_len);
}
/******************************************************************************/

}
}  // namespace ami::bridge
