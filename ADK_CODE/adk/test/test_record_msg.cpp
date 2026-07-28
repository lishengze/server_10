#include "adk/record_msg.h"
#include <exception>
#include <stdexcept>
#include <boost/bind.hpp>
#include <boost/thread.hpp>
class Element 
{
public:
    Element(){}

    Element(int32_t int_test, double double_test) :int_test_(int_test), double_test_(double_test) {}

    virtual void Free()
    {
        if (p_int_test_)
            free(p_int_test_);
        if (p_double_test_)
            free(p_double_test_);
    }

    virtual std::string ToString()
    {
        std::ostringstream oss;
        oss <<"\t" <<"int_test:" << int_test_ ;
        oss << "\t"<<"double_test:" << double_test_;
        if (p_int_test_)
            oss <<"\t" <<"p_int_test:" << *p_int_test_;
        if (p_double_test_)
            oss <<"\t"<< "p_double_test:" << *p_double_test_;
        return std::move(oss.str());
    }

     static void FreeElement(Element* t)
    {
        t->Free();
    }

     static std::string ParseMsg(Element* t)
    {
        return t->ToString();
    }

private:
    int int_test_ = 1;
    double double_test_ = 0.001;
    int *p_int_test_ = nullptr;
    double *p_double_test_ = nullptr;
};

class ElementSec : public Element
{
public:
    ElementSec(int int_test, double double_test, float float_test) :Element(int_test, double_test), float_test_(float_test) {}
    void Free() override
    {
        Element::Free();
    }

    std::string ToString() override
    {
        std::ostringstream oss;
        std::string str = Element::ToString();
        oss << str;
        oss << "\t"<<"float:" << float_test_;
        return std::move(oss.str());
    }

private:
    float float_test_{ 0.02f };
};

class TestRecordMsg
{
public:
    TestRecordMsg(std::string file_name) :file_name_(file_name) {}

    ~TestRecordMsg()
    {
        if (nullptr != record_file_)
        {
            delete record_file_;
            record_file_ = nullptr;
        }
        if (nullptr != record_msg_)
        {
            delete record_msg_;
            record_msg_ = nullptr;
        }
       
    }

    bool Init(boost::asio::io_service& ios, bool is_adk_log = true)
    {
        
        if(is_adk_log)
            record_file_ = new adk::RecordToAdkLog();
        else
            record_file_ = new adk::RecordToLocalFile(file_name_, true);

        if (!record_file_->Init())
        {
            std::cout << "record_file init failed." << std::endl;
            return false;
        }

        record_msg_ = new adk::RecordMsg<Element*>(&ios); // 使用ios模式

        //record_msg_ = new adk::RecordMsg<Element*>(); // 使用非ios模式
       // record_msg_->SetFreeFunc(std::bind(&Element::FreeElement,std::placeholders::_1));
       // record_msg_->SetParseMsgFunc(std::bind(&Element::ParseMsg,std::placeholders::_1)) ;

        record_msg_->SetFreeFunc([](Element* element) { return element->Free(); });
        record_msg_->SetSerializeMsgFunc([](Element *element) { 
            return element->ToString(); });
        record_msg_->SetRecorder(std::bind(&adk::IRecorder::Record, record_file_,std::placeholders::_1));

        std::cout << "init is ok or not :" << record_msg_->Init(8192) << std::endl;
        return true;
    }

    void Start(int32_t flag)
    {
        if (flag == 3)
        {
            record_msg_->Start(true); 
        }
        else
        {
            std::cout << "record_msg start :" << record_msg_->Start() << std::endl;
        }
        put_msg_thrd_ = std::thread(std::bind(&TestRecordMsg::PutMsg,this));
    }

    void Stop(int32_t flag)
    {
        if (put_msg_thrd_.joinable())
        {
            put_msg_thrd_.join();
        }

       record_msg_->Stop(); // 停止自身创建的线程并使用join等
    }
    
    void PutMsg()
    {
        int32_t i = 0;
        while (i< 100)
        {
            Element *element = new ElementSec(i,i+0.01,0.002f);
            record_msg_->PutMsg(element);
            i++;
            //std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::cout << "put msg thread is exit." << std::endl;
        
    }

    void RunIos(int32_t periodic_time,int32_t number)
    {
        record_msg_->RunWithIos();
    }

    void Run()
    {
        record_msg_->Run();
    }

    void RunOnce(int32_t number)
    {
        record_msg_->RunOnce(number);
    }

private:
    adk::RecordMsg<Element*> *record_msg_{nullptr};
    adk::IRecorder *record_file_{nullptr};
    std::string file_name_;
    std::thread put_msg_thrd_;

};

void Run(boost::asio::io_service *ios)
{
    ios->run();
}

int main(int argc,char **argv)
{

    if (argc < 4)
    {
        std::cout << "need two parameter" << std::endl;
        return 0;
    }

    int32_t flag = std::atoi(argv[1]); // 0.RunOnce; 1. RunIos; 2. Run; 3. createThread

    bool is_adk_log = false;
    int32_t is_adk_flag = std::atoi(argv[2]);  // 0. local file; !0: adk log
    if (is_adk_flag != 0)
    {
        is_adk_log = true;
    }

    std::string file_name(argv[3]) ;
    TestRecordMsg test(file_name);
   

    boost::asio::io_service ios;
    boost::asio::io_service::work ios_work(ios);
    if(!test.Init(ios, is_adk_log))
    {
        std::cout<<"Init failed"<<std::endl;
        return -1 ;
    }

    test.Start(flag); 
    std::thread thread_ios;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    switch (flag)
    {
    case 0:
        {
            int i = 0;
            while (i < 1000)
            {
                test.RunOnce(10);          
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                i++;
            }
            break;
        }
    case 1:
        {
            // 使用ios
            test.RunIos(1000,50);
            thread_ios = std::move(std::thread(std::bind(&Run, &ios)));
            break;
        }
    case 2 :
        {
            // 阻塞接口
            //std::thread t = std::move(std::thread(std::bind(&TestRecordMsg::RunIos, &test, 1000, 10)));
            test.Run();
            break;
        }
    case 3:
        {
            // 调用start 函数创建处理线程
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            break;
        }
    default:
        std::cout << "unknow flag" << std::endl;
    }
    std::this_thread::sleep_for(std::chrono::seconds(15));
    test.Stop(flag);
    ios.stop();
    if (thread_ios.joinable())
    {
        thread_ios.join();
    }
   
   
    return 0;
}
