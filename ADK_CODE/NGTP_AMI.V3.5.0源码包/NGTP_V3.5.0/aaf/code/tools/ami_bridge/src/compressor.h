/**
 * @author 陈志(chenzhi@af.local)
 */

#ifndef AMI_BRIDGE_COMPRESSOR_H_
#define AMI_BRIDGE_COMPRESSOR_H_

///< cpp std
#include <string>
#include <memory>  //unique_ptr
#include <vector>
#include <system_error>

///< boost
#include <boost/filesystem/path.hpp>

///< adk, ami public
#include <user_defined_compression_c_api.h>
#include <ami/message.h>

///< impl

namespace ami
{
namespace bridge
{

extern "C" typedef int (*AmiCCompressInitFunc)(AmiCStream*, const char*);
extern "C" typedef int (*AmiCCompressFunc)(AmiCStream*);
extern "C" typedef int (*AmiCCompressEndFunc)(AmiCStream*);
extern "C" typedef int (*AmiCDecompressInitFunc)(AmiCStream*, const char*);
extern "C" typedef int (*AmiCDecompressFunc)(AmiCStream*);
extern "C" typedef int (*AmiCDecompressEndFunc)(AmiCStream*);

class CompressorBase
{
public:
    CompressorBase(const boost::filesystem::path& compress_lib_path,
                   const std::string& compress_config_str);
    CompressorBase(const CompressorBase&);
    CompressorBase& operator=(const CompressorBase&) = delete;
    ~CompressorBase() {}

protected:
    void LoadApi();

protected:
    boost::filesystem::path compress_lib_path_;
    std::string compress_config_str_;
    void* dl_handler_ = nullptr;

    std::unique_ptr<AmiCStream> strm_;
};

class Compressor : public CompressorBase
{
private:
    static constexpr const char* kAmiCCompressInitFN = "AmiCCompressInit";
    static constexpr const char* kAmiCCompressFN     = "AmiCCompress";
    static constexpr const char* kAmiCCompressEndFN  = "AmiCCompressEnd";
    static constexpr size_t kOutUnitSize             = 512u;

public:
    Compressor(const boost::filesystem::path& compress_lib_path,
               const std::string& compress_config_str);
    Compressor(const Compressor&);
    Compressor& operator=(const Compressor&) = delete;
    ~Compressor();

    /**
     * 错误，弹出异常
     */
    const char* Compress(const Message* msg, size_t& len);

private:
    void LoadApi();

private:
    AmiCCompressInitFunc compress_init_func_ = nullptr;
    AmiCCompressFunc compress_func_          = nullptr;
    AmiCCompressEndFunc compress_end_func_   = nullptr;
    std::vector<char> output_pool_;
    size_t out_len_ = 0;
};

class Decompressor : public CompressorBase
{
private:
    static constexpr const char* kAmiCDecompressInitFN = "AmiCDecompressInit";
    static constexpr const char* kAmiCDecompressFN     = "AmiCDecompress";
    static constexpr const char* kAmiCDecompressEndFN  = "AmiCDecompressEnd";

public:
    Decompressor(const boost::filesystem::path& compress_lib_path,
                 const std::string& compress_config_str);
    Decompressor(const Decompressor&);
    Decompressor& operator=(const Decompressor&) = delete;
    ~Decompressor();

    /**
     * 错误，弹出异常
     */
    void Decompress(const char* compressed_data, size_t len, size_t orig_len, Message* msg);

private:
    void LoadApi();

private:
    AmiCDecompressInitFunc decompress_init_func_ = nullptr;
    AmiCDecompressFunc decompress_func_          = nullptr;
    AmiCDecompressEndFunc decompress_end_func_   = nullptr;
};

}
}  // namespace ami::bridge

#endif /* AMI_BRIDGE_UNICAST_CONN_H_ */
