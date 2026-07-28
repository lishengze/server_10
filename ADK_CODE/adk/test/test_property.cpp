#include <adk/property.h>
#include <adk_pack/property.h>

#include <iostream>

void SetValue(adk::Property* props)
{
    props->SetValue("MessageHandler1", reinterpret_cast<void*>(1));
    props->SetValue("MessageHandler2", reinterpret_cast<void*>(2));
    props->SetValue("MessageHandler3", reinterpret_cast<void*>(3));
    props->SetValue("MessageHandler4", reinterpret_cast<void*>(4));
    props->SetValue("MessageHandler5", reinterpret_cast<void*>(5));
    props->SetValue("MessageHandler6", reinterpret_cast<void*>(6));
    props->SetValue("MessageHandler7", reinterpret_cast<void*>(7));
    props->SetValue("MessageHandler8", reinterpret_cast<void*>(8));
    props->SetValue("MessageHandler9", reinterpret_cast<void*>(9));
    props->SetValue("MessageHandler10", reinterpret_cast<void*>(10));
    props->SetValue("MessageHandler11", reinterpret_cast<void*>(11));
}

int main()
{
    adk_impl::Property props;
    SetValue((adk::Property*)(&props));

    std::cout << props.GetValue("MessageHandler1", adk_impl::Pointer()).get_value() << std::endl; 
    std::cout << props.GetValue("MessageHandler2", adk_impl::Pointer()).get_value() << std::endl;
    std::cout << props.GetValue("MessageHandler3", adk_impl::Pointer()).get_value() << std::endl;
    std::cout << props.GetValue("MessageHandler4", adk_impl::Pointer()).get_value() << std::endl;
    std::cout << props.GetValue("MessageHandler5", adk_impl::Pointer()).get_value() << std::endl;
    std::cout << props.GetValue("MessageHandler6", adk_impl::Pointer()).get_value() << std::endl;
    std::cout << props.GetValue("MessageHandler7", adk_impl::Pointer()).get_value() << std::endl;
    std::cout << props.GetValue("MessageHandler8", adk_impl::Pointer()).get_value() << std::endl;
    std::cout << props.GetValue("MessageHandler9", adk_impl::Pointer()).get_value() << std::endl;
    std::cout << props.GetValue("MessageHandler10", adk_impl::Pointer()).get_value() << std::endl;
    std::cout << props.GetValue("MessageHandler11", adk_impl::Pointer()).get_value() << std::endl;
    return 0;
}
