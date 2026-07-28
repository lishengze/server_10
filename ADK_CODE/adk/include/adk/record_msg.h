/**
* @file         record_msg.h
* @brief       
* @author     luojian@archforce.com.cn
* @date       2018/01/26
*/

#ifndef ADK_IMPL_RECORD_MSG_H_
#define ADK_IMPL_RECORD_MSG_H_

#include "log.h"
#include "error_code.h"
#include "rate_limit.h"
#include "entry_wrapper.h"
#include "lock_free_queue_variant.h"

#include <ctime>
#include <atomic>
#include <string>
#include <chrono>
#include <thread>
#include <exception>
#include <functional>

#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/format.hpp>
#include <boost/filesystem.hpp>
#include <boost/exception/all.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/asio/steady_timer.hpp>


#ifndef ADK_RECORD_MSG_LOG
#define ADK_RECORD_MSG_LOG 10000
#endif

#ifndef ADK_RECORD_MSG_TO_ADK_LOG
#define ADK_RECORD_MSG_TO_ADK_LOG 10001
#endif

#ifndef ADK_RECORD_MSG_TO_LOCALFILE
#define ADK_RECORD_MSG_TO_LOCALFILE  10002
#endif


namespace adk_impl
{

    /**
    * @brief      记录接口
    *
    */
    class IRecorder
    {
    public:
        virtual bool Record(const std::string& msg) = 0;
        virtual bool Init() = 0;
        virtual ~IRecorder() {}
    };

    /**
    * @brief      ADK记录类
    *
    */
    class RecordToAdkLog : public IRecorder
    {
    public:
        ADK_LOG_DECLARE_AC(ADK_RECORD_MSG_TO_ADK_LOG);

        RecordToAdkLog() {}

        bool Record(const std::string& msg) override
        {
            ADK_LOG_INFO_AC("Message", msg);
            return true;
        }

        bool Init() override
        {
            return true;
        }
    };
   

    /**
    * @brief      文件记录类
    *
    */
    class RecordToLocalFile :public IRecorder
    {
    public:
        ADK_LOG_DECLARE_AC(ADK_RECORD_MSG_TO_LOCALFILE);
        /*
        * @brief  
        * @param file_name 记录文件的名字  is_addtail_newline 末尾是否增加换行符
        */
        RecordToLocalFile(const std::string &file_name,bool is_addtail_newline = false) :m_file_name_(file_name) 
        {
            m_is_addtail_newline_.store(is_addtail_newline);
        }

        ~RecordToLocalFile() override
        {
            if (m_ofs_.is_open())
            {
                m_ofs_.flush();
                m_ofs_.close();
            }
        }
        static inline std::string GetFormatLocalTime(const std::string& format)
        {
            time_t time_point = time(0);
            struct tm* timeinfo;
            timeinfo = localtime(&time_point);

            char buffer[24];// _YYYY-mm-dd_HH-MM-SS_ 设置24为超出format的长度
            strftime(buffer, 24, format.c_str(), timeinfo);
            return std::string(buffer);
        }
        /**
        * @brief     初始化函数
        */
        bool Init() override
        {
            if (m_file_name_.empty())
            {
                return false;
            }

            try{
                boost::filesystem::path file_path(m_file_name_) ;
               
                auto full_path =file_path.remove_filename() ;
                if(!boost::filesystem::exists(full_path))
                {
                    if(!boost::filesystem::create_directories(full_path))
                    {
                        ADK_LOG_WARN_AC_TF("Create record file failed",
                                           "file_path: {1}", m_file_name_);
                        return false ;
                    }
                }
            }
            catch(...)
            {
                std::string str = std::move(boost::current_exception_diagnostic_information());
                ADK_LOG_WARN_AC_TF("Catch exception", str) ;
                return false ;
            }

            boost::filesystem::path log_file_base_name = m_file_name_;
            // 插入时间信息，初始时仅向精确到日期的文件写入
            std::string log_file = log_file_base_name.string();
            if (log_file_base_name.has_extension())
            {
                insert_pos_ = log_file_base_name.string().find_last_of('.');
                log_file_extension_ = log_file_base_name.extension().string();
            }
            else
            {
                insert_pos_ = log_file.length();
                log_file_extension_ = "";
            }
            m_file_name_ = log_file.insert(insert_pos_, GetFormatLocalTime("_%Y-%m-%d"));// insert主要解决存在后缀的情况

            // 通过环境变量获取日志分割大小
            char* env_var = std::getenv("IO_LOG_ROTATE_SIZE");
            if (nullptr != env_var)
            {
                try{
                    log_rotate_ = atoi(env_var);
                }
                catch(...)
                {
                    log_rotate_ = 0;
                }
                log_file_number_ = 0;
                // 遍历当前目录，获取编号情况下的最新编号
                boost::filesystem::directory_iterator end_it; 
                // 日志文件的格式 name_YYYY-mm-dd_HH-MM-SS_N.xxx，这里需要获取N的值
                boost::regex expression(log_file_base_name.stem().string() + GetFormatLocalTime("_%Y-%m-%d") + "_([0-9]+)" + log_file_extension_);
                for (boost::filesystem::directory_iterator i(log_file_base_name.parent_path().string()); i != end_it; ++i)
                {
                    if(!boost::filesystem::is_regular_file(i->status()))
                    {
                        continue;
                    }

                    boost::cmatch what;
                    std::string filename = i->path().filename().string();
                    if (boost::regex_match(filename.c_str(), what, expression))
                    {
                        uint64_t number = atoll(what.str(1).c_str());
                        if (log_file_number_ <= number)
                            log_file_number_ = number + 1;
                    }
                }
                // 获取文件初始化时的具体时间，用于日志切割后，携带编号的文件名中，否则创建文件时再获取时间
                // 会使文件名的时分秒对应成所保存最后一条日志的时分秒
                // log_time_ = GetFormatLocalTime("_%H-%M-%S_");
            }
            else
            {
                log_rotate_ = 0;
            }

            m_ofs_.open(m_file_name_, std::ofstream::out | std::ofstream::app);
            if (!m_ofs_.is_open())
            {
                boost::format fmt("cannot open the Record local file [%1%]");
                fmt % m_file_name_;
                ADK_LOG_WARN_AC_TF("Cannot open the record Local file",
                                   "file_path: {1}", m_file_name_);
                return false;
            }
            return true;
        }
        bool ChangeFile()
        {
            m_ofs_.flush();
            m_ofs_.close();
            std::string localtime = GetFormatLocalTime("_%Y-%m-%d");
            std::string cache_file_name;
            const auto lt_len = localtime.length();
            if (m_file_name_.substr(insert_pos_, lt_len) != localtime)
            {
                cache_file_name = m_file_name_.substr(0, insert_pos_ + lt_len) + "_" + std::to_string(log_file_number_) + log_file_extension_;
                log_file_number_ = 0;
            }
            else
            {
                cache_file_name = m_file_name_.substr(0, insert_pos_) + localtime + "_" + std::to_string(log_file_number_) + log_file_extension_;
                ++log_file_number_;
            }
            // 文件拷贝，将最新日志写入带具体时间和编号的文件中
            boost::filesystem::rename(m_file_name_,cache_file_name);
            m_file_name_.replace(insert_pos_, lt_len, localtime);
            m_ofs_.open(m_file_name_, std::ofstream::out | std::ofstream::app);
            if (!m_ofs_.is_open())
            {
                ADK_LOG_WARN_AC_TF("Cannot open the record Local file",
                                "file_path: <{1}>", m_file_name_);
                return false;
            }
            return true;
        }
        /*
         *@brief 获取当前时间
         *@retrun 时间(微妙)
        */
        static std::string GetLocalTime();

        /**
        * @brief     文件记录函数
        */
        bool Record(const std::string &msg) override
        {
            if (m_ofs_.is_open())
            {
                if (log_rotate_ > 0 && m_ofs_.tellp() > log_rotate_)
                {
                    // 修改文件输出文件，重新打开ofs
                    if (!ChangeFile())
                    {
                        ADK_LOG_WARN_AC_TF("Record local file is not opened", "");
                        return false;
                    }
                }
                m_ofs_ << RecordToLocalFile::GetLocalTime()<<" msg : " << msg ;
                if (m_is_addtail_newline_)
                {
                    m_ofs_ << std::endl;
                }
                return true;
            }
            else
            {
                ADK_LOG_WARN_AC_TF("Record local file is not opened", "");
                return false;
            }
        }

    private:
        std::string m_file_name_; ///< 文件名
        std::ofstream m_ofs_; ///< 文件流
        std::atomic<bool> m_is_addtail_newline_{ false }; ///< 是否末尾增加换行符
        std::ofstream::pos_type log_rotate_; ///< 输出文件大小限制
        uint64_t log_file_number_ = 0; ///< 输出文件的当前序号
        std::size_t insert_pos_; ///< 插入日期序号位置
        std::string log_file_extension_; ///< 日志文件拓展名
        std::string log_time_; ///< 存储日志切割情况下，新文件的创建时间点 格式为_HH-MM-SS_
    };
   

    class RecordMsgBase
    {
    public:
        ADK_LOG_DECLARE_AC(ADK_RECORD_MSG_LOG) ;
    };
    
    /**
    * @brief     记录工具类
    *
    */
    template<typename  T>
    class RecordMsg : public RecordMsgBase
    {
    public:
        //ADK_LOG_DECLARE_AC(ADK_RECORD_MSG_LOG);
        using ExeFunc = std::function<void(const T& t)>;
        using FreeFunc = std::function<void (T)>;
        using SerializeMsgFunc = std::function<std::string (T)>;
        using RecordFunc = std::function<bool(const std::string &)>;
        /**
        * @brief     默认构造函数
        *  
        */
        RecordMsg()
        {}

        /**
        * @brief     构造函数
        * @param   boost::asio::io_service 使用io_service方式， 
        *                  periodic_number 定时器周期(微妙,默认1000微妙)
         *                  deal_number每次处理数量(默认10)
        */
        RecordMsg(boost::asio::io_service* ios, 
            uint32_t periodic_timer=1000, 
            uint32_t deal_number = 10
            )
            : m_ios_(ios),
            m_deal_number_(deal_number),
            m_periodic_time_(periodic_timer)
        {
            m_timer_to_post_ = new boost::asio::steady_timer(*m_ios_);
        }

        ~RecordMsg() {}

        /**
        * @brief     初始化函数
        * @param   buffer_size：队列大小，is_throw_exception：是否抛出异常，false将会间隔10打印异常信息的日志
        * @attention   创建RecordMsg需要先初始化
        */
        bool Init(uint32_t buffer_size= 8192,
            bool is_throw_exception = true )
        {
            if (m_is_init_)
            {
                return true;
            }

            m_is_throw_exception_.store(is_throw_exception);

            m_is_init_.store(true);

            m_msg_que_ = variant::MPSCQueue<T>::Create("DealRecordQueue", buffer_size);
            if (!m_msg_que_)
            {
                return false;
            }
           
            return true;
        }

        /**
        * @brief     设置数据释放函数
        * @attention   std::function<void(T)>;
        */
        inline void SetFreeFunc(FreeFunc free_func)
        {
            m_free_func_ = free_func;
        }

        /**
        * @brief     设置解析函数
        * @attention   std::function<std::string(T)>;
        */
        inline void SetSerializeMsgFunc(SerializeMsgFunc serialize_msg_func)
        {
            m_serialize_msg_func_ = serialize_msg_func;
        }

        /**
        * @brief     设置Record函数
        * @attention  std::function<bool(const std::string &)>;
        */
        inline void SetRecorder(RecordFunc record_func)
        {
            m_record_func_ = record_func;
        }

        /**
        * @brief      Start函数，
        *
        * @param is_create_dealthrd  true：创建线程处理./ false ：使用其他方式
        *
        * @attention   is_create_dealthrd为true将会创建处理线程，在此之前需要设置回调函数及调用Init函数
        *
        */
        bool Start(bool is_create_dealthrd = false)
        {
            if (!m_is_init_)
            {
               return false ;
            }

            if (m_is_running_)
            {
                return true ;
            }

            if (m_serialize_msg_func_ && m_record_func_)
            {
                if (!m_free_func_)
                {
                    m_exe_functor_ = [=](const T& t) {
                        std::string str= m_serialize_msg_func_(t);
                        m_record_func_(std::move(str));
                    };
                }
                else
                {
                    m_exe_functor_ = [=](const T& t) {
                        std::string str = m_serialize_msg_func_(t);
                        m_record_func_(std::move(str));
                        m_free_func_(t);
                    };
                }
            }
            else
            {
                return false ;
            } // end if serialize_msg_func && record_func_

            m_is_running_.store(true);

            if (!m_is_create_thrd_ && is_create_dealthrd) // 创建处理线程
            {
                m_is_create_thrd_ = true;
                m_deal_thread_ = std_thread("adk-recordmsg", "deal thread", boost::bind(&RecordMsg::DealThrd, this));
            }
            return true;
        }

        /**
        * @brief      Stop函数
        *
        * @attention 如果使用io_service模式，需要先调用此stop，再停止io_service
        *
        */
        void Stop()
        {
            if (!m_is_running_)
            {
                return;
            }
            m_is_running_.store(false);

            m_is_dispatch_run_.store(false);

            if (m_is_create_thrd_)
            {
                if (m_deal_thread_.joinable())
                {
                    m_deal_thread_.join();
                    m_is_create_thrd_.store(false);
                }
            }
        }


        /**
        * @brief     PutMsg函数
        *
        * @parameter T msg
        *
        * @attention 非阻塞接口
        */
        void PutMsg(const T& msg)
        {
            if (!m_is_running_)
            {
                return;
            }

            while (ErrorCode::kSuccess != m_msg_que_->Push(msg))
            {
                if (!m_is_running_)
                {
                    return;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
        }

        /**
        * @brief     Run函数
        *
        * @attention 此函数将会阻塞当前的调用线程,
        */
        void Run()
        {
            if (!Check())
            {
                return;
            }
            m_is_dispatch_run_.store(true);

            DealThrd();
        }

        /**
        * @brief     RunWithIos函数
        *
        * @parameter
        *
        * @attention 需要使用构造函数：RecordMsg(boost::asio::io_service *ios) 
        */
        void RunWithIos()
        {
            if (!m_ios_)
            {
                return ;
            }

            if (!Check())
            {
                return;
            }
            m_is_dispatch_run_.store(true);

            boost::system::error_code ec;
            m_timer_to_post_->expires_from_now(std::chrono::microseconds(m_periodic_time_), ec);
            if (ec)
            {
                return ;
            }
            m_timer_to_post_->async_wait(boost::bind(&RecordMsg::AsyncDealMsg, this, boost::asio::placeholders::error));
        }

        /**
        * @brief     RunOnce函数
        *
        * @parameter number 处理数量 默认为10
        *
        * @attention  非阻塞接口
        */
        void RunOnce(uint32_t number=10)
        {

            if (!Check())
            {
                return;
            }

            if (number == 0)
            {
                number = m_deal_number_;
            }
             DealMsgByNumber(number);
            
        }

    private:

        /**
        * @brief      检测是否允许运行，能否被调用，是否已调用过
        * @note       被Run，Run(uint32_t,uint32_t)，RunOnce(uint32_t)函数调用
        * @return    true/false
        */
        bool Check()
        {
            if (!m_is_running_)
            {
                return false;
            }
            if (m_is_create_thrd_)
            {
                return false;
            }
            if (m_is_dispatch_run_)
            {
                return false;
            }
            return true;
        }

        /**
        * @brief      处理函数
        * @attention 如果抛出异常将会间隔打印，间隔时间为1秒
        * @return     kSuccess/kFailure ...
        */
        ErrorCode  DealMessageWithLog()
        {
            T element;
            ErrorCode ret_code = (ErrorCode)m_msg_que_->Pop(element);
            if (ErrorCode::kSuccess == ret_code)
            {
                try
                {
                    m_exe_functor_(element);
                }
                catch (...)
                {
                    std::string str = std::move(boost::current_exception_diagnostic_information());
                    ADK_EXE_RATE_LIMIT(ADK_LOG_ERROR_AC_TF("Catch exception", str), (5 * 1000UL * 1000UL), 1);
                    ret_code = ErrorCode::kFailure;
                } // end try catch exception
            }// end if ErrorCode::kSuccess
            return ret_code;
        }

        /**
        * @brief      处理函数
        * @attention 如果存在异常将不捕获，直接抛出
        * @return     kSuccess/else error with the msg_que_ ...
        */
        ErrorCode  DealMessageThrowException()
        {
            T element;
            ErrorCode ret_code = (ErrorCode)m_msg_que_->Pop(element);
            if (ErrorCode::kSuccess == ret_code)
            {
                m_exe_functor_(element);
            }// end if ErrorCode::kSuccess
            return ret_code;
        }

        /**
        * @brief      消息处理线程
        */
        void DealThrd()
        {
            ErrorCode ret = ErrorCode::kSuccess;
            while (m_is_running_)
            {
                if (m_is_throw_exception_)
                {
                    ret = DealMessageThrowException();
                }
                else
                {
                    ret = DealMessageWithLog();
                }
                if (ErrorCode::kQueueEmpty == ret)
                {
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
                else if (ErrorCode::kSuccess == ret)
                {
                    continue;
                }
                else if (ErrorCode::kFailure != ret &&
                    ErrorCode::kSuccess != ret &&
                    ErrorCode::kQueueEmpty != ret)
                {
                    ADK_EXE_RATE_LIMIT(ADK_LOG_ERROR_AC_TF("Deal the RecordQueue error",
                                                           "error: {1}", GetErrorDesc(ret)),
                                       (5UL * 1000UL * 1000UL), 1);
                }
            }// end while is_running_
        }

        /**
        * @brief      消息处理函数
        *
        * @param     number，处理消息的数量
        */
        ErrorCode DealMsgByNumber(int32_t number)
        {
            ErrorCode ret = ErrorCode::kFailure;
            if (!m_is_running_)
            {
                return ret ;
            }
                
            for (int32_t i = 0; i < number; i++)
            {
                if (m_is_throw_exception_)
                {
                    ret = DealMessageThrowException();
                }
                else
                {
                    ret = DealMessageWithLog();
                }
                if (ErrorCode::kQueueEmpty == ret)
                {
                    return ret ;
                }
                else if (ErrorCode::kFailure != ret && 
                    ErrorCode::kSuccess != ret && 
                    ErrorCode::kQueueEmpty != ret)
                {
                    ADK_EXE_RATE_LIMIT(ADK_LOG_ERROR_AC_TF("Deal the RecordQueue error",
                                                           "error: {1}", GetErrorDesc(ret)),
                                       (5UL * 1000UL * 1000UL), 1);
                }// end if DealMessage
            }// end for
            return ret ;
        }

        /**
        * @brief      timer_to_post_的回调函数
        *
        * @param     error
        */
        void AsyncDealMsg(const boost::system::error_code &error)
        {
            if (m_is_running_)
            {
                if (!error)
                {
                    AsyncDealMsgWrap() ;
                }// end if error
            }// end if is_running_
        }

        void AsyncDealMsgWithoutWait()
        {
            AsyncDealMsgWrap() ;
        }

        void AsyncDealMsgWrap()
        {
            ErrorCode ec =  DealMsgByNumber(m_deal_number_); //每次仅处理10条消息,可以调整
            if(!m_is_running_)
            {
                return ;
            }

            if (ec != ErrorCode::kSuccess)
            {
                boost::system::error_code ec;
                m_timer_to_post_->expires_from_now(std::chrono::milliseconds(m_periodic_time_), ec);
                if (ec)
                {
                    ADK_LOG_WARN_AC_TF("Record Msg Periodic timer is failed", "error: {1}", ec.message());
                    return;
                }
                m_timer_to_post_->async_wait(boost::bind(&RecordMsg::AsyncDealMsg, this, boost::asio::placeholders::error));
            }
            else
            {
                m_ios_->post(boost::bind(&RecordMsg::AsyncDealMsgWrap, this));
            }
        }
        void test();
    private:
        FreeFunc m_free_func_;       ///< free the element of  T
        SerializeMsgFunc m_serialize_msg_func_; ///< parse the msg to string
        RecordFunc m_record_func_ ;   ///< record the msg of string
        ExeFunc m_exe_functor_;///< 执行函数体

        std::atomic<bool> m_is_running_{ false }; ///< running flag
        variant::MPSCQueue<T> *m_msg_que_=nullptr; ///< mpsc que
        std::atomic<bool> m_is_dispatch_run_{ false }; ///< 是否已经调用Run函数
        std::atomic<bool> m_is_init_{ false }; ///< init or not
        std::atomic<bool> m_is_throw_exception_{ false };

         // create thread 
        std::thread m_deal_thread_;                ///< thread then create
        std::atomic<bool> m_is_create_thrd_{ false }; ///< create thread or not

        // use io_service
        boost::asio::io_service* m_ios_  { nullptr }; ///< tmp storage the io_service
        boost::asio::steady_timer  *m_timer_to_post_{nullptr}; ///<定时器
        uint32_t m_deal_number_{10};///< 每个周期处理的消息数量
        uint32_t m_periodic_time_{1000};///< 定时器时长(微妙)
    };
    
} // end namespace adk_impl


#endif
