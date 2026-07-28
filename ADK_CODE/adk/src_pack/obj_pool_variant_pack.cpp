#include <adk/obj_pool_variant.h>
#include <adk_pack/obj_pool_variant.h>



namespace adk
{

namespace variant
{

using ObjPoolVarImpl = adk_impl::variant::ObjPoolBase;

// adk_impl::variant::ObjPoolBase must equal to adk::variant::ObjPoolBase

static_assert(sizeof(ObjPoolVarImpl) == sizeof(adk::variant::ObjPoolBase),
              "ObjPoolBase must keep algin between adk and adk_impl");

int32_t DoGetTimeId()
{
    return adk_impl::variant::DoGetTimeId();
}

int32_t ObjPoolBase::Init(ObjConstructorType f, std::size_t size, int32_t id)
{
    return reinterpret_cast<ObjPoolVarImpl*>(this)->Init(f, size, id);
}


void* ObjPoolBase::New(int32_t id)
{
    return reinterpret_cast<ObjPoolVarImpl*>(this)->New(id);
}

void ObjPoolBase::Delete(void* obj, int32_t id)
{
    return ObjPoolVarImpl::Delete(obj, id);
}

int32_t Delete(void* obj)
{
    return adk_impl::variant::Delete(obj);
}

} // variant

} // adk
