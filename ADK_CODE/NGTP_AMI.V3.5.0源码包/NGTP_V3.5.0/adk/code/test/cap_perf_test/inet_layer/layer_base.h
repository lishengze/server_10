#ifndef INET_LAYER_BASE_H_
#define INET_LAYER_BASE_H_

#include <stdint.h>
#include <assert.h>

#ifdef __GNUC__
#include <arpa/inet.h>
#else
#include <windows.h>
#include <winsock2.h>
#endif

#include <string>
#include <utility>
#include <exception>

namespace inet
{

using std::pair;
using std::string;

using Payload = pair<const uint8_t*, uint32_t>;
using LayerData = pair<const uint8_t*, uint32_t>;

class InetParseException : public std::exception
{
public:
    InetParseException(const char* content)
    {
        exception_ = content;
    }

    InetParseException(string&& content)
    {
        exception_ = content;
    }

    const char* what() const noexcept
    {
        return exception_.data();
    }

private:
    string exception_;
};

class LayerBase
{
public:
    LayerBase(const LayerData& layer_data) : layer_data_(layer_data)
    {
    }

    LayerBase(const uint8_t* data, uint32_t len) : LayerBase(std::make_pair(data, len))
    {
    }

    ~LayerBase() = default;

    inline Payload GetPayload()
    {
        return std::make_pair(layer_data_.first + header_len_, payload_len_);
    }

    uint16_t protocol() const
    {
        return protocol_;
    }

protected:
    uint16_t  protocol_;
    uint16_t  header_len_;
    uint16_t  payload_len_;
    LayerData layer_data_;
};

}

#endif