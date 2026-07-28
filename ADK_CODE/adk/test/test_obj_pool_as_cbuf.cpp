#include <adk/object_pool.h>

template<int buffer_size = 2048>
class BufferObject : public adk::IObject
{
public:
    char* buffer() { return &cbuf_[0]; }

private:
    char cbuf_[buffer_size];
};

int main(int argc, char const *argv[])
{
    adk::ObjectPool<BufferObject<>>* buffer_pool = adk::ObjectPool<BufferObject<>>::Create("buffer_pool",
                                                                                       8192);

    BufferObject<>* obj = buffer_pool->NewObject();
    char* underlying_buffer = obj->buffer();

    // using buffer;

    // delete buffer
    obj->Delete();
    return 0;
}
