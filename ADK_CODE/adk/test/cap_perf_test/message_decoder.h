#ifndef CAP_MESSAGE_DECODER_H_ 
#define CAP_MESSAGE_DECODER_H_

#include <stdint.h>

namespace cap
{

class MessageDecoder
{
public:
    virtual int32_t OnMessage(char* data, uint32_t len) = 0;
};

}

#endif
