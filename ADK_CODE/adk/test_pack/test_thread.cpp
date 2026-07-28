#include <adk/thread.h>

#include <iostream>

ADK_THREAD_MESSAGE(MyThreadMessage)
{
public:
    MyThreadMessage()
        :   a_(0)
    {}

private:
    int32_t a_;
};

ADK_THREAD_MESSAGE(MyThreadMessage2)
{
public:
    MyThreadMessage2()
        :   a_(0)
    {}

private:
    int32_t a_;
};

ADK_OOB_THREAD_MESSAGE(MyOOB)
{
public:
    MyOOB()
    {}
private:
    int32_t a_;
};

int main(int argc, char const *argv[])
{
    auto obj = MyThreadMessage::New();
    std::cout << "MyThreadMessage::TypeId() = " << MyThreadMessage::TypeId() << std::endl;
    std::cout << "MyThreadMessage::TypeName() = " << MyThreadMessage::TypeName() << std::endl;

    std::cout << "obj->message_type() = " << obj->message_type() << std::endl;
    std::cout << "obj->message_type_name() = " << obj->message_type_name() << std::endl;
    std::cout << "&obj = " << &obj << std::endl;
    std::cout << "&*obj = " << &(*obj) << std::endl;

    auto obj1 = MyThreadMessage::New();
    std::cout << "obj1->message_type() = " << obj1->message_type() << std::endl;
    std::cout << "obj1->message_type_name() = " << obj1->message_type_name() << std::endl;
    std::cout << "&obj1 = " << &obj1 << std::endl;
    std::cout << "&*obj1 = " << &(*obj1) << std::endl;

    auto obj2 = MyThreadMessage2::New();
    std::cout << "MyThreadMessage2::TypeId() = " << MyThreadMessage2::TypeId() << std::endl;
    std::cout << "MyThreadMessage2::TypeName() = " << MyThreadMessage2::TypeName() << std::endl;

    std::cout << "obj2->message_type() = " << obj2->message_type() << std::endl;
    std::cout << "obj2->message_type_name() = " << obj2->message_type_name() << std::endl;
    std::cout << "&obj2 = " << &obj2 << std::endl;
    std::cout << "&*obj2 = " << &(*obj2) << std::endl;

    auto obj3 = MyThreadMessage2::NewUnsafe();
    std::cout << "obj3->message_type() = " << obj3->message_type() << std::endl;
    std::cout << "obj3->message_type_name() = " << obj3->message_type_name() << std::endl;
    std::cout << "&obj3 = " << &obj3 << std::endl;
    std::cout << "&*obj3 = " << &(*obj3) << std::endl;

    auto obj4 = MyOOB::New();
    std::cout << "MyOOB::TypeId() = " << MyOOB::TypeId() << std::endl;
    std::cout << "MyOOB::TypeName() = " << MyOOB::TypeName() << std::endl;

    std::cout << "obj4->message_type() = " << obj4->message_type() << std::endl;
    std::cout << "obj4->message_type_name() = " << obj4->message_type_name() << std::endl;
    std::cout << "&obj4 = " << &obj4 << std::endl;
    std::cout << "&*obj4 = " << &(*obj4) << std::endl;

    return 0;
}



