//
// Created by lzn on 9/16/19.
//

#ifndef ADK_IMPL_PIPELINE_VARIANT_H_
#define ADK_IMPL_PIPELINE_VARIANT_H_

#include "util.h"
#include "event.h"
#include "entry_wrapper.h"
#include "lock_free_msg_queue.h"

#include <stdlib.h>

#include <queue>
#include <mutex>
#include <tuple>
#include <vector>
#include <cstring>
#include <functional>

#include <boost/thread/thread.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/exception/diagnostic_information.hpp>
//#include <boost/property_tree.h>

#ifndef PIPELINE_VARIANT_PAYLOAD
//#define PIPELINE_VARIANT_PAYLOAD uint64_t
#endif

namespace adk_impl
{




    /**
     * @brief always return T&
     * @tparam T
     * @param obj
     * @return
     */
    template<class T>
    auto ResolvPointer(T obj, typename std::enable_if<std::is_pointer<T>::value, int>::type = 0) -> decltype(*obj)
    {
        return *obj;
    }

    template<class T>
    auto ResolvPointer(T& obj, typename std::enable_if<!std::is_pointer<T>::value, int>::type = 0) -> T&
    {
        return obj;
    }

/**
 * @brief A Helper function use in LoadConfig, if base class doesn't exist LoadConfig, it would do nothing, otherwise it would call LoadConfig implemented in base class.
 * @tparam Derived Base Class
 * @tparam T Argument Type of function LoadConfig(T&)
 * @param this_ptr object pointer of base class
 * @param obj Argument of function LoadConfig(T&)
 */
    template<class Derived, class T>
    void CallNextConfigLoader(Derived *this_ptr, T &obj,
                              typename std::enable_if<
                                      std::is_same<decltype(std::declval<Derived>().LoadConfig(
                                              std::declval<T &>())), void>::value, int>::type = 0)
    {
        this_ptr->LoadConfig(obj);
    }

/**
 * @brief Default Implemention. Do nothing.
 * @tparam Derived
 * @tparam T
 * @param ...
 */
    template<class Derived, class T>
    void CallNextConfigLoader(...)
    {
        //OnMessage when it doesn't exist a lower-level Impl
    }

    struct IndexGenerator
    {
    public:
        /**
         * @brief returns a unique index.
         * @return
         */
        static int GetNewIndex();
    };

    struct DuplicateInfo
    {
        uint64_t index;
    };

    class NConnector;

    template< class T>
    struct OrderFlag
    {
    public:
        uint64_t order = 1;
    };

    /**
     * @brief Flag of a Stage, to avoid unwanted input.
     */
    struct StageFlag{};

/**
 * @brief Interface of a Stage, all of the Stage objects can cast to this type.
 */
    struct StageType
    {
    public:
        /**
         * @brief Init related data structure, and start This Stage.
         * @return Success?
         */
        virtual bool Start() = 0;

        /**
         * @brief Stop this Stage. It would stop the work thread, and can not start it again.
         */
        virtual void Stop() = 0;

        /**
         * @brief write Indicator about this Stage to 'dest'
         * @param dest target ptree
         */
        virtual void Indicator(boost::property_tree::ptree &dest) = 0;

        /**
         * @brief Executed only when input queue is empty.
         */
        virtual void Idle() = 0;

        /**
         * @brief returns If this Stage is processing message.
         * @return
         */
        virtual bool IsIdle() = 0;

        /**
         * @brief returns running state of this Stage.
         * @return
         */
        virtual bool IsRunning() = 0;

        /**
         * @brief Pause this Stage. A paused Stage can not receive any message.
         * Note: Pause() returns only when current stage has no pending message to process.
         */
        virtual void Pause() = 0;

        /**
         * @brief Executed when any exception occurred.
         * @param exception_info
         */
        virtual void Error(const std::string &exception_info) = 0;

        /**
         * @brief Resume a paused Stage.
         */
        virtual void Resume() = 0;

        /**
         * @brief Get Output channels count of this stage.
         * @return
         */
        virtual int GetOutputsCount() = 0;

        /**
         * @brief Get collection of NConnectors vectors in current Stage.
         * @return
         */
        virtual std::vector<NConnector *> *GetNConnectors() = 0;

        /**
         * @brief Get index of this Stage. Default value is 0;
         * @return
         */
        virtual uint64_t GetIndex() = 0;

        virtual ~StageType() = default;

        virtual const char* Name() = 0;

        virtual uint32_t GetPrevConnectorsCount() = 0;
    };

/**
 * @brief Data of current Message. Required by Stage Class.
 */
    struct MsgStatus
    {
        uint64_t order;
#ifdef PIPELINE_VARIANT_PAYLOAD
        PIPELINE_VARIANT_PAYLOAD payload;
        static_assert(std::is_trivial<PIPELINE_VARIANT_PAYLOAD>::value, "Payload: must be a pod struct.");
#endif
    };
    static_assert(std::is_trivial<MsgStatus>::value, "Dev: MsgStatus should be a pod struct.");



    class NConnector
    {
    private:
        struct OnMessageProxy
        {
            uint32_t OnMessage(void* stat, void *ptr)
            {return 0;}
        };

        InvokeInfo<uint32_t, void*, void*> invoker;

        decltype(&OnMessageProxy::OnMessage) nextOnMessage = nullptr;
        void *target = nullptr;
        StageType *target_ret = nullptr;
        StageType *prev_ret = nullptr;
    public:

        /**
         * @brief Connect to another function(not thread safe!!)
         * @param new_invoker
         */
        void Switch(InvokeInfo<uint32_t, void*, void*> new_invoker)
        {
            if(new_invoker.this_ptr == invoker.this_ptr)
            {
#ifndef BOOST_ARCH_X86_64
                static_assert(false, "Please Check Following code in current arch.");
#endif
                //Assignment of 64-bit value has no intermediate state in x86/64 architecture.
                invoker.callptr = new_invoker.callptr;
                return;
            }
            else
            {
                //Not Thread Safe, So do not use it in running state.
                assert(!prev_ret->IsRunning());
                //throw 1;
                invoker.this_ptr = new_invoker.this_ptr;
                ADK_BARRIER();
                invoker.callptr = new_invoker.callptr;
            }
        }

        StageType *GetTarget()
        { return target_ret; }

        StageType* GetSrc()
        { return prev_ret;}

        template<class T>
        uint32_t Forward(const MsgStatus &stat, const T &data)
        {
            //assert(nextOnMessage != nullptr && target != nullptr);
            //return (*reinterpret_cast<OnMessageProxy *>(target).*nextOnMessage)((void*) & stat, (void *) &data);
            return invoker((void*) & stat, (void *) &data);
        }



        template<class Prev, class Next>
        static NConnector *Connect(Prev &prevStep, Next &nextStep)
        {
            static_assert(std::is_base_of<StageFlag, Prev>::value, "NConnector: prevStep must be a StepBase");
            static_assert(std::is_base_of<StageFlag, Next>::value, "NConnector: nextStep must be a StepBase");
            static_assert(Prev::template getNConnectorIndex<typename Next::ArgType>() != -1,
                          "Connect Failed: Type dismatch");
            assert(!prevStep.IsRunning());//Can't connect two running stages.
            assert(!nextStep.IsRunning());//Can't connect two running stages.
            auto conns_prev = prevStep.template getNConnector<typename Next::ArgType>();
            if (conns_prev == nullptr)
                return nullptr;
            auto addr = memalign(ADK_CACHE_LINE_SIZE, sizeof(NConnector));
            auto this_ptr = new(addr)NConnector();
            conns_prev->push_back(this_ptr);

            this_ptr->invoker = TransformMemberFunction(&nextStep, &Next::Pre_Message);

            //this_ptr->nextOnMessage = (decltype(nextOnMessage)) (&Next::Pre_Message);
            //this_ptr->target = (void *) ((typename Next::StageType *) (&nextStep));
            this_ptr->target_ret = (StageType *) &nextStep;
            this_ptr->prev_ret = (StageType *) &prevStep;
            nextStep.IncConnCounter(this_ptr);
            return this_ptr;
        }
    };

    /**
     * @brief merge multiple stages. You can connect these stages to next stage in one statement.
     * @tparam Args stages
     * @param params stages
     * @return
     */
    template<class... Args>
    auto group(Args &&... params) -> std::tuple<Args...>
    {
        return std::tuple<Args...>(std::forward<Args>(params)...);
    }

    template<class... Args>
    void expansion(Args &&... params)
    {}

    struct PairHolder_Flag{};

    template <class Start, class End>
    struct PairHolder : public PairHolder_Flag
    {
        using StartType = typename std::decay<Start>::type;
        using EndType = typename std::decay<End>::type;
        StartType* start;
        EndType* end;
        PairHolder(StartType* start, EndType* end) : start(start), end(end){}
    };


    template<int index, class Prev, class... Args>
    void inline Conn(Prev* prev, std::tuple<Args...>&& next, typename std::enable_if<index < sizeof...(Args), int>::type = 0)
    {
        *prev | ResolvPointer(std::get<index>(next));
        Conn<index + 1>(prev, std::forward<std::tuple<Args...>>(next));
    }

    template<int index, class Prev, class... Args>
    void inline Conn(Prev* prev, std::tuple<Args...>&& next, typename std::enable_if<!(index < sizeof...(Args)), int>::type = 0)
    {
    }

    template<int index, class Next, class... Args>
    void inline Conn(std::tuple<Args...>&& prev, Next* next, typename std::enable_if<index < sizeof...(Args), int>::type = 0)
    {
        ResolvPointer(std::get<index>(prev)) | *next;
        Conn<index + 1>(std::forward<std::tuple<Args...>>(prev), next);
    }

    template<int index, class Next, class... Args>
    void inline Conn(std::tuple<Args...>&& prev, Next* next, typename std::enable_if<!(index < sizeof...(Args)), int>::type = 0)
    {
    }

    template<class Prev, class Next>
    auto inline operator|(Prev& prev, std::vector<Next *>& next) -> PairHolder<Prev, std::vector<Next*>>
    {
        for (int i = 0; i < next.size(); i++)
        {
            NConnector::Connect(prev, *(next[i]));
        }
        return PairHolder<Prev, std::vector<Next*>>{&prev, &next};
    }

    template<class Prev, class Next>
    auto inline operator|(Prev *prev, std::vector<Next *> &next) -> PairHolder<Prev, std::vector<Next*>>
    {
        for (int i = 0; i < next.size(); i++)
        {
            NConnector::Connect(*prev, *(next[i]));
        }
        return PairHolder<Prev, std::vector<Next*>>{prev, &next};
    }

    template<class Prev, class Next>
    auto inline operator|(std::vector<Prev *> &prev, Next *next) -> PairHolder<std::vector<Prev*>, Next>
    {
        for (int i = 0; i < prev.size(); i++)
        {
            NConnector::Connect(*(prev[i]), *next);
        }
        return PairHolder<std::vector<Prev*>, Next>{&prev, next};
    }

    template<class Prev, class Next>
    auto inline operator|(std::vector<Prev *> &prev, Next &next) -> PairHolder<std::vector<Prev*>, Next>
    {
        return prev | &next;
    }

    template <class Prev, class Next>
    struct ArgChecker
    {
        using PrevType = typename std::decay<Prev>::type;
        using NextType = typename std::decay<Next>::type;
        using IsTuple_Prev = std::__is_tuple_like<PrevType>;
        using IsTuple_Next = std::__is_tuple_like<NextType>;
        using IsPairHolder_Prev = std::is_base_of<PairHolder_Flag, PrevType>;
        using IsPairHolder_Next = std::is_base_of<PairHolder_Flag, NextType>;
        constexpr static bool value = !(IsTuple_Prev::value
                || IsTuple_Next::value
                || IsPairHolder_Prev::value
                || IsPairHolder_Next::value);
    };

    template<class Prev, class Next
            ,typename std::enable_if<ArgChecker<Prev, Next>::value ,int >::type = 0>
    auto inline operator|(Prev &prev, Next &next) -> PairHolder<Prev, Next>
    {
        //static_assert(std::is_base_of<StageType, Prev>::value, "Prev must be a StepBase");
        //static_assert(std::is_base_of<StageType, Next>::value, "Next must be a StepBase");
        NConnector::Connect(prev, next);
        return PairHolder<Prev, Next>{&prev, &next};
    }

    template<class Prev, class Next
            ,typename std::enable_if<ArgChecker<Prev, Next>::value ,int >::type = 0>
    auto inline operator|(Prev &prev, Next *next) -> PairHolder<Prev, Next>
    {
        return prev | *next;
    }

    template<class Prev, class Next
            ,typename std::enable_if<ArgChecker<Prev, Next>::value ,int >::type = 0>
    auto inline operator|(Prev *prev, Next &next) -> PairHolder<Prev, Next>
    {
        return *prev | next;
    }

    template<class Prev, class... Args>
    auto inline operator|(Prev* prev, std::tuple<Args...>&& next) -> PairHolder<Prev, std::tuple<Args...>>
    {
        Conn<0>(prev, std::forward<std::tuple<Args...>>(next));
        return PairHolder<Prev, std::tuple<Args...>>{prev, &next};
    }

    template<class Prev, class... Args>
    auto inline operator|(Prev& prev, std::tuple<Args...>&& next) -> PairHolder<Prev, std::tuple<Args...>>
    {
        Conn<0>(&prev, std::forward<std::tuple<Args...>>(next));
        return PairHolder<Prev, std::tuple<Args...>>{&prev, &next};
    }

    template<class Next, class... Args>
    auto operator|(std::tuple<Args...>&& prev, Next* next) -> PairHolder<std::tuple<Args...>, Next>
    {
        Conn<0>(std::forward<std::tuple<Args...>>(prev), next);
        return PairHolder<std::tuple<Args...>, Next>{&prev, next};
    }

    template<class Next, class... Args>
    auto operator|(std::tuple<Args...>&& prev, Next& next) -> PairHolder<std::tuple<Args...>, Next>
    {
        Conn<0>(std::forward<std::tuple<Args...>>(prev), &next);
        return PairHolder<std::tuple<Args...>, Next>{&prev, &next};
    }

    template<class Next, class... Args>
    auto operator|(std::tuple<Args...>& prev, Next* next) -> PairHolder<std::tuple<Args...>, Next>
    {
        return static_cast<std::tuple<Args...>&&>(prev) | next;
    }

    template<class Next, class... Args>
    auto operator|(std::tuple<Args...>& prev, Next& next) -> PairHolder<std::tuple<Args...>, Next>
    {
        return static_cast<std::tuple<Args...>&&>(prev) | next;
    }

    template<class Prev, class... Args>
    auto inline operator|(Prev* prev, std::tuple<Args...>& next) -> PairHolder<Prev, std::tuple<Args...>>
    {
        return prev | static_cast<std::tuple<Args...>&&>(prev);
    }

    template<class Prev, class... Args>
    auto inline operator|(Prev& prev, std::tuple<Args...>& next) -> PairHolder<Prev, std::tuple<Args...>>
    {
        return prev | static_cast<std::tuple<Args...>&&>(prev);
    }

    template<class Start, class End, class Next>
    auto inline operator|(PairHolder<Start, End> prev, Next* next) -> PairHolder<Start, Next>
    {
        *(prev.end) | *next;
        return PairHolder<Start, Next>{prev.start, next};
    }

    template<class Start, class End, class Next>
    auto inline operator|(PairHolder<Start, End> prev, Next& next) -> PairHolder<Start, Next>
    {
        *(prev.end) | next;
        return PairHolder<Start, Next>{prev.start, &next};
    }

    template<class Prev, class Start, class End>
    auto inline operator|(Prev* prev, PairHolder<Start, End> next) -> PairHolder<Prev, End>
    {
        *prev | *(next.start);
        return PairHolder<Prev, End>{prev, next.end};
    }

    template<class Prev, class Start, class End>
    auto inline operator|(Prev& prev, PairHolder<Start, End> next) -> PairHolder<Prev, End>
    {
        prev | *(next.start);
        return PairHolder<Prev, End>{&prev, next.end};
    }

    template<class ArgumentType, class... OutputTypes>
    class StageInterface : public StageType
    {
    protected:

        uint64_t placeholder_1 __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
        /*
        * @brief Status of current Message, any changes to this struct will pass to follow stages.
        */
        MsgStatus stats;

        uint64_t placeholder_2 __attribute__((aligned(ADK_CACHE_LINE_SIZE)));

        uint32_t conn_count = 0;

#ifdef PIPELINE_VARIANT_PAYLOAD
        /**
         * @brief returns payload of this message.
         * Note: if message A generated from message B, A and B have same payload.
         * @return
         */
        PIPELINE_VARIANT_PAYLOAD& getMsgPayload()
        {
            return stats.payload;
        }
#endif
    public:

        using ArgType = ArgumentType;
        using OutTypes = std::tuple<OutputTypes...>;

        uint32_t GetPrevConnectorsCount() override
        {
            return conn_count;
        }

        bool Start() override
        { return true;};

        void Stop() override
        {};

        //  virtual void Indicator(boost::property_tree&);
        void Idle() override
        {};

        void Pause() override
        {};

        void Error(const std::string &exception_info) override
        {};

        void Resume() override
        {};

        void Indicator(boost::property_tree::ptree &tree) override
        {
        };

        int GetOutputsCount() override
        { return sizeof...(OutputTypes); }

        std::vector<NConnector *> *GetNConnectors() override
        { return NConnectors; }

        virtual void Message(const ArgumentType &in)
        {};

        uint32_t Pre_Message(const MsgStatus& stat, const ArgumentType& payload)
        {
            //memcpy(&stats, &stat, sizeof(MsgStatus));
            stats = stat;
            Message(payload);
            return ErrorCode::kSuccess;
        };

        uint32_t Forward(std::nullptr_t)
        { throw new std::exception(); }

        template<class T>
        static constexpr int getNConnectorIndex()
        {
            return getConn<typename std::decay<T>::type, 0, OutputTypes...>();
        }

        using InterfaceType = StageInterface<ArgumentType, OutputTypes...>;

        template<class T>
        std::vector<NConnector *> *getNConnector()
        {
            return (getNConnectorIndex<T>() != -1) ? &NConnectors[getNConnectorIndex<T>()] : nullptr;
        }

        const char* Name() override
        {
            return "unknown";
        }

        void inline IncConnCounter(NConnector* ptr)
        {
            prev_connectors.push_back(ptr);
            conn_count++;
        }

    private:
        template<class T, int index, class current, class... SearchTypes>
        static constexpr int getConn()
        {
            return std::is_same<current, T>::value ? index : (getConn<T, index + 1, SearchTypes...>());
        }

        template<class T, int index>
        static constexpr int getConn()
        {
            return -1;
        }

    protected:
        std::vector<NConnector *> NConnectors[sizeof...(OutputTypes)];
        std::vector<NConnector*> prev_connectors;
    };


    template<class T, class Base>
    class ForwardInterface : public Base
    {
    public:
        static_assert(std::is_trivial<T>::value, "StageBase: Output Types should be trivial(pod type).");

        using Base::Forward;

        decltype(std::declval<Base>().template getNConnector<T>()->data()) data = nullptr;
        size_t dcnt;

        virtual uint32_t Forward(const T &element)
        {
            //static_assert(Base::template getNConnectorIndex<T>() != -1, "StageBase: Cannot Forward type undefined in Template parameters");
            //int index = Base::template getNConnectorIndex<T>();
            uint32_t last_error = ErrorCode::kSuccess;
            if(data == nullptr)
            {
                //....it's not a optimize....?(compiler can inline there func)

                //multi thread situation:
                //There is no need to add mutex.
                //vector will not change if Entrance.Start() is invoked.
                auto vec = this->template getNConnector<T>();
                dcnt = vec->size();
                data = vec->data();
            }

            //return data[0]->Forward(this->stats, element);

            for (size_t i = 0; i < dcnt; i++)
            {
                auto ret = data[i]->Forward(this->stats, element);
                last_error = (ret != ErrorCode::kSuccess) ? ret : last_error;
            }
            return last_error;
        }
    };

    template<template<class, class> class RepType, class BaseType, class... Rest>
    class FwdHelper
    {
    public:
        using CrrentType = BaseType;
        using NextType = BaseType;
    };

    static_assert(1, "you kaixin jiu hao");

    template<template<class, class> class RepType, class BaseType, class U, class... Rest>
    class FwdHelper<RepType, BaseType, U, Rest...>
    {
    public:
        using CurrentType = RepType<U, BaseType>;
        using NextType = typename FwdHelper<RepType, CurrentType, Rest...>::NextType;
    };

    struct StageConfig
    {
        bool is_same_context = false;
        bool is_ordered = false;
        std::string name{"DefaultStage"};
        uint64_t queue_size = 4096;
        uint64_t polling_nano = 200000ul;
        int32_t backoff_limit = 64;
        std::string cpuAffinity{""};
    };


    namespace pipeline_utils
    {
        extern void DoChangeToRealtime(int32_t priority, int32_t policy);
        extern void DoRestoreToOther();
    }

    /**
     * @brief Base Type of a stage, any custom stages should derived from this class.
     * @tparam ArgType input type of this stage.
     * @tparam OutputTypes output types of this stage.
     */
    template<class ArgType, class... OutputTypes>
    class StageBase
            : public FwdHelper<ForwardInterface, StageInterface<ArgType, OutputTypes...>, OutputTypes...>::NextType,
              public StageFlag
    {
        static_assert(std::is_trivial<ArgType>::value, "StageBase: Input Type should be trivial(pod type).");
    protected:
        bool is_accept_msg = true;
        bool is_same_context = false;
        bool is_ordered = false;
        bool is_running = false;
        bool is_idle = false;
        std::string name{"DefaultStage"};
        uint64_t queue_size = 4096;
        uint64_t polling_nano = 200000ul;
        int32_t backoff_limit = 64;
        uint64_t index = 0;
        std::string cpuAffinity{""};

        using BaseType = typename FwdHelper<ForwardInterface, StageInterface<ArgType, OutputTypes...>, OutputTypes...>::NextType;


        bool is_sp = false;

        SimpleEventManager *event_manager;

        boost::thread *thr = nullptr;

        struct QueuePayload
        {
            MsgStatus stats;
            ArgType data;
        };

        MPSCQueue *input_queue = nullptr;
    protected:
        void SetIndex(uint64_t i)
        {
            index = i;
        }
    public:
        struct
        {
            uint64_t rx_msgs = 0;
            uint64_t drop_msgs = 0;
        } stat_data;

        using StageType = StageBase<ArgType, OutputTypes...>;
    private:
        void Run()
        {
            char stageInfo[256] = "StageInfo_";
            strcpy(stageInfo + 10, name.c_str());
            if(cpuAffinity.size() > 0)
            {
                auto ret = SetCpuAffinity(cpuAffinity);
                if(ret != ErrorCode::kSuccess)
                {
                    this->Error("Failed to set cpu affinity.");
                }
            }
            pipeline_utils::DoChangeToRealtime(50, SCHED_FIFO);
            Entry *entry = nullptr;
            //QueuePayload payload;
            auto queue = (SPSCQueue<QueuePayload> *) input_queue;
            while (true)//(ACCESS_ONCE(this->is_running))
            {
                    //if (queue->Pop(payload) == adk_impl::ErrorCode::kSuccess)
                    if (queue->WaitEntry(&entry) == ErrorCode::kSuccess)
                    {
                        try //try - catch have cost ,but not the main reason
                        {
                            auto buf = (QueuePayload *)entry->buffer;
                            //printf("m %ld %ld\n", *((uint64_t*)&(buf->stats)), buf->data);
                            BaseType::Pre_Message(buf->stats, buf->data);
                            stat_data.rx_msgs++;
                        }
                        catch (...)
                        {
                            this->Error(boost::current_exception_diagnostic_information());
                            stat_data.drop_msgs++;
                        }
                        //if (entry != nullptr)
                        queue->FreeEntry(entry);
                    }
                    else
                    {
                        //Stop thread only when all pending messages are processed.
                        if(!ACCESS_ONCE(this->is_running))
                            break;
                        //entry = nullptr;
                        is_idle = true;
                        this->Idle();
                        is_idle = false;
                    }

            }
        }

        void Run_Order()
        {
            char stageInfo[256] = "StageInfo_";
            strcpy(stageInfo + 10, name.c_str());
            if(cpuAffinity.size() > 0)
            {
                auto ret = SetCpuAffinity(cpuAffinity);
                if(ret != ErrorCode::kSuccess)
                {
                    this->Error("Failed to set cpu affinity.");
                }
            }
            while (true)
            {
                Entry *entry = nullptr;
                try
                {
                    if (((SPSCQueue<QueuePayload> *) input_queue)->RecorderWaitEntry(&entry) == ErrorCode::kSuccess)
                    {
                        auto buf = (QueuePayload *) entry->buffer;
                        BaseType::Pre_Message(buf->stats, buf->data);
                        stat_data.rx_msgs++;
                    }
                    else
                    {

                        if(!ACCESS_ONCE(this->is_running))
                            break;
                        is_idle = true;
                        this->Idle();
                        is_idle = false;
                        entry = nullptr;
                    }
                }
                catch (...)
                {
                    this->Error(boost::current_exception_diagnostic_information());
                    stat_data.drop_msgs++;
                }
                if (entry != nullptr)
                    ((SPSCQueue<QueuePayload> *) input_queue)->RecorderFreeEntry(entry);
            }
        }


        uint32_t Pre_Message_UnOrdered(const MsgStatus & stat, const ArgType& in)
        {
            QueuePayload load;
            load.stats = stat;
            load.data = in;
            //printf("i %ld %ld\n", *((uint64_t*)&(load.stats)), load.data);
            //(adk_impl::SPSCQueue<QueuePayload> *)
            if(is_sp)
            {
                while (((SPSCQueue<QueuePayload> *)input_queue)->Push(load) != ErrorCode::kSuccess)
                {
                    ADK_PAUSE();
                }
            }
            else
            {
                while ((input_queue)->Push(load) != ErrorCode::kSuccess)
                {
                    ADK_PAUSE();
                }
            }
            return event_manager->TryNotify([]() { return true; });
        }

        uint32_t Pre_Message_Ordered(const MsgStatus& stat, const ArgType& in)
        {
            QueuePayload load;
            load.stats = stat;
            load.data = in;
            //(adk_impl::SPSCQueue<QueuePayload> *)
            while(((SPSCQueue<QueuePayload> *)input_queue)->ReorderPush(load, stat.order) != ErrorCode::kSuccess)
            {
                ADK_PAUSE();
            }
            return event_manager->TryNotify([]() { return true; });
        }

        uint32_t Pre_Message_Paused(const MsgStatus& stat, const ArgType& in)
        {
            stat_data.drop_msgs++;
            return ErrorCode::kQueueFull;
        }

        void UpdateConnectors()
        {
            InvokeInfo<uint32_t, void*, void*> new_func;
            if(!is_accept_msg)
                new_func = TransformMemberFunction(this, &StageType::Pre_Message_Paused);
            else if(is_same_context)
                new_func = TransformMemberFunction(this, &StageType::Pre_Message);
            else if(is_ordered)
                new_func = TransformMemberFunction(this, &StageType::Pre_Message_Ordered);
            else
                new_func = TransformMemberFunction(this, &StageType::Pre_Message_UnOrdered);
            for(auto& ele : StageType::prev_connectors)
            {
                ele->Switch(new_func);
            }
        }

    public:

        StageBase() = default;

        StageBase(const StageType &) = delete;

        StageBase(StageType &&) = delete;

        uint64_t GetIndex() override
        {
            return index;
        }

        const char* Name() override
        {
            return name.data();
        }

        void Indicator(boost::property_tree::ptree &tree) override
        {
            using boost::property_tree::ptree;
            ptree event, queue;
            if(!is_same_context)
                input_queue->Dump(queue);
            SimpleEveManStats stats;
            event_manager->GetStats(stats);
            event.add("direct_success", stats.direct_success);
            event.add("number_waits", stats.number_waits);
            event.add("poll_rounds", stats.poll_rounds);
            event.add("poll_success", stats.poll_success);

            tree.add("name", name);
            tree.add("is_accept_msg", is_accept_msg);
            tree.add("is_same_context", is_same_context);
            tree.add("is_ordered", is_ordered);
            tree.add("is_running", is_running);
            tree.add("is_idle", is_idle);
            tree.add("drop_messages", stat_data.drop_msgs);
            tree.add("receive_messages", stat_data.rx_msgs);

            tree.add_child("queue", queue);
            tree.add_child("event_manager", event);
        };

        void Idle() override
        {
            event_manager->Wait([this]() -> uint32_t {
                return (is_running && (input_queue->length() == 0)) ? ErrorCode::kQueueEmpty : ErrorCode::kSuccess;
            });
        }


        uint32_t Pre_Message(const MsgStatus & stat, const ArgType& in) // Hide Pre_Message in Base Class.
        {
            //Default Impl, SameContext
            stat_data.rx_msgs++;
            try
            {
                return BaseType::Pre_Message(stat, in);
            }
            catch(...)
            {
                this->Error(boost::current_exception_diagnostic_information());
                stat_data.drop_msgs++;
                return ErrorCode::kFailure;
            }
            return ErrorCode::kSuccess;
        }


        void LoadConfig(DuplicateInfo& info)
        {
            SetIndex(info.index);
        }

        void LoadConfig(boost::property_tree::ptree &config)
        {
            is_same_context = config.get("IsSameContext", false);
            is_ordered = config.get("IsOrdered", false);
            name = config.get("Name", "DefaultStage");
            queue_size = config.get("QueueSize", 4096);
            polling_nano = config.get("PollingNano", 200000ul);
            backoff_limit = config.get("BackoffLimit", 64);
            cpuAffinity = config.get("CpuAffinity", "");
            UpdateConnectors();
        }

        void LoadConfig(StageConfig &config)
        {
            is_same_context = config.is_same_context;
            is_ordered = config.is_ordered;
            name = config.name;
            queue_size = config.queue_size;
            polling_nano = config.polling_nano;
            backoff_limit = config.backoff_limit;
            cpuAffinity = config.cpuAffinity;
            UpdateConnectors();
        }

        void Stop() override
        {
            if(!is_running)
                return;
            is_running = false;
            if (!is_same_context && thr->joinable())
            {
                event_manager->TryNotify([]() { return true; });
                thr->join();
            }
        }

        bool IsRunning() override
        {
            return is_running;
        }

        bool IsIdle() override
        {
            return is_idle;
        }

        void Pause() override
        {
            if(!is_accept_msg)
                return;
            is_accept_msg = false;
            UpdateConnectors();
            if (is_same_context)
                return;
            for (;;)
            {
                if (ACCESS_ONCE(is_idle))
                    break;
                if (!ACCESS_ONCE(is_running))
                    break;
                usleep(100000);// 100 ms
            }
        }

        void Resume() override
        {
            is_accept_msg = true;
            UpdateConnectors();
        }

        ~StageBase() override
        {

            delete thr;
            delete event_manager;
        }

        bool Start() override
        {
            if(is_running)
                return true;
            if(name.length() == 0)
            {
                name = "stage_";
            }
            name += IndexGenerator::GetNewIndex();
            UpdateConnectors();
            if(this->conn_count <= 1)
                is_sp = true;
            if (!is_same_context)
            {
                event_manager = new SimpleEventManager(polling_nano, backoff_limit);
                if(is_sp)
                    input_queue = (MPSCQueue*)SPSCQueue<QueuePayload>::Create(name + "_input", queue_size);
                else
                    input_queue = MPSCQueue::Create(name + "_input", sizeof(QueuePayload), queue_size);
                is_running = true;
                thr = new boost::thread();
                name += "_";
                name += this->GetIndex();

                pipeline_utils::DoChangeToRealtime(50, SCHED_FIFO);

                if (is_ordered)
                {
                    *thr = boost_thread("pipeline", name.data(), boost::bind(&StageType::Run_Order, this));
                }
                else
                {
                    *thr = boost_thread("pipeline", name.data(), boost::bind(&StageType::Run, this));
                }

                pipeline_utils::DoRestoreToOther();
            }
            return true;
        }

    private:


    };


    template<class T, class Base>
    class ReorderImpl : public Base, public OrderFlag<T>
    {
    public:
        using Base::Forward;

        virtual uint32_t Forward(const T &element)
        {
            this->stats.order = OrderFlag<T>::order;
            OrderFlag<T>::order++;
            return Base::Forward(element);
        }
    };

    struct returnValues_Flag
    {
    };

    template<class... RetTypes>
    struct returnValues : public returnValues_Flag
    {
        int index;

        template<class T, class... Rest>
        static constexpr size_t GetSize()
        {
            return (sizeof(T) < GetSize<Rest...>()) ? GetSize<Rest...>() : sizeof(T);
        }

        template<class... Rest>
        static constexpr size_t GetSize(typename std::enable_if<sizeof...(Rest) == 0, int>::type = 0)
        {
            return 0;
        }

        static constexpr size_t size = GetSize<RetTypes...>();

        uint8_t data[size];

        template<class T, int index, class current, class... SearchTypes>
        static constexpr int getIndex()
        {
            return std::is_same<current, T>::value ? index : (getIndex<T, index + 1, SearchTypes...>());
        }

        template<class T, int index>
        static constexpr int getIndex()
        {
            return -1;
        }

        template<class T>
        returnValues(const T &dat,
                     typename std::enable_if<getIndex<T, 0, RetTypes...>() != -1, int>::type = 0)
        {
            index = getIndex<T, 0, RetTypes...>();
            std::memcpy(data, &dat, sizeof(dat));
        }

        returnValues() : index(-1){}
    };

    template<bool isOverwrite, class BaseType, class Function>
    class OnMessageImpl : public BaseType
    {
    private:
        struct FwdProxy
        {
        public:
            uint32_t Forward(void *ptr)
            {return 0;}
        };

        using InvokeType = decltype(&FwdProxy::Forward);

        template<class returnArg, class ReceiveArgs>
        struct RedirectHelper
        {

            template<class T, int index, class tupleType>
            static constexpr int
            getIndex(typename std::enable_if<!(index < std::tuple_size<tupleType>::value), int>::type = 0)
            {
                return -1;
            }

            template<class T, int index, class tupleType>
            static constexpr int
            getIndex(typename std::enable_if<index < std::tuple_size<tupleType>::value, int>::type = 0)
            {
                return std::is_same<T, typename std::tuple_element<index, tupleType>::type>::value ? index : getIndex<T,
                        index + 1, tupleType>();
            }


            static constexpr bool isExist = (getIndex<returnArg, 0, typename BaseType::OutTypes>() != -1);

            static_assert(isExist, "OnMessage(): Return Types of returnValues<> must fit with OutputType.");

            using StageType = typename BaseType::StageType;
            using Decay_Arg = typename std::decay<returnArg>::type;

            static constexpr uint32_t (StageType::*FwdAddr)(const Decay_Arg &) = &StageType::Forward;

            static constexpr InvokeType FuncAddr()
            {
                return reinterpret_cast<InvokeType>(FwdAddr);
            }
        };

        template<class returnArg>
        using Redirect = RedirectHelper<returnArg, typename BaseType::OutTypes>;

        template<class returnArg>
        static constexpr InvokeType GetForward()
        {
            return Redirect<returnArg>::FuncAddr();
        }

        template<class... returnArgs>
        struct FwdTable
        {
            static InvokeType *Get()
            {
                static InvokeType entries[sizeof...(returnArgs)]{GetForward<returnArgs>()...};
                return entries;
            }
            //static constexpr std::array<InvokeType,sizeof...(returnArgs)> entries{{GetForward<returnArgs>()...}};
        };


        using FuncInfo = function_traits<Function>;

        using argInfo = typename BaseType::ArgType;
        using outInfo = typename BaseType::OutTypes;

        static_assert(std::is_same<typename FuncInfo::return_type, void>::value ||
                      (BaseType::template getNConnectorIndex<typename std::decay<typename FuncInfo::return_type>::type>() !=
                       -1) ||
                      std::is_base_of<returnValues_Flag, typename std::decay<typename FuncInfo::return_type>::type>::value,
                      "OnMessage(): Input Function's return value must be void, or one of target Step's output type.");
        static_assert(FuncInfo::args_count <= 1, "OnMessage(): Input Function can only have 0 or 1 argument.");

        static constexpr bool isNotReturn = std::is_same<typename FuncInfo::return_type, void>::value;
        static constexpr bool isArg = FuncInfo::args_count != 0;

        template<class ReturnType>
        uint32_t inline HandleForward(ReturnType &value)
        {
            return BaseType::Forward(value);
        }


        template<class... RetTypes>
        uint32_t inline HandleForward(returnValues<RetTypes...> &value)
        {
            auto tablex = decltype(FwdTable<RetTypes...>::Get())(this->table);
            if(value.index == -1)
                return ErrorCode::kSuccess;
            auto func = tablex[value.index];
            auto offset_this = (FwdProxy *) ((typename BaseType::StageType *) this);
            return ((*offset_this).*func)((void *) value.data);
        }

        template<bool BRet, bool BArg>
        uint32_t inline Exec(const argInfo &in,
                             typename std::enable_if<!BRet && !BArg, int>::type = 0)
        {
            (*caller)();
            return ErrorCode::kSuccess;
        }

        template<bool BRet, bool BArg>
        uint32_t inline Exec(const argInfo &in,
                             typename std::enable_if<BRet && !BArg, int>::type = 0)
        {
            auto ret = (*caller)();
            return HandleForward(ret);
        }

        template<bool BRet, bool BArg>
        uint32_t inline Exec(const argInfo &in,
                             typename std::enable_if<!BRet && BArg, int>::type = 0)
        {
            static_assert(
                    std::is_same<typename std::decay<typename FuncInfo::template argument<0>::type>::type, argInfo>::value,
                    "OnMessage(): argument type must equal to Step input type.");
            (*caller)(in);
            return ErrorCode::kSuccess;
        }

        template<bool BRet, bool BArg>
        uint32_t inline Exec(const argInfo &in,
                             typename std::enable_if<BRet && BArg, int>::type = 0)
        {
            static_assert(
                    std::is_same<typename std::decay<typename FuncInfo::template argument<0>::type>::type, argInfo>::value,
                    "OnMessage(): argument type must equal to Step input type.");
            using FuncArg1 = typename FuncInfo::template argument<0>::type;
            static_assert(std::is_same<const typename std::decay<FuncArg1>::type &, FuncArg1>::value,
                          "OnMessage(): argument type must be const T&");
            auto ret = (*caller)(in);
            return HandleForward(ret);
        }

        void *table;

        template<class ReturnType>
        void inline initTable(ReturnType *value)
        {
            //Do Nothing
        }

        template<class... RetTypes>
        void inline initTable(returnValues<RetTypes...> *value)
        {
            table = FwdTable<RetTypes...>::Get();
        }

    public:
        Function *caller;

        OnMessageImpl()
        {
            table = nullptr;
            static_assert(std::is_base_of<StageType, BaseType>::value, "Base must be a StepBase");
            initTable((typename std::decay<typename FuncInfo::return_type>::type *) nullptr);
        }

        ~OnMessageImpl() override
        {
            delete caller;
        }

        void Message(const typename BaseType::ArgType &arg) override
        {
            Exec<!isNotReturn, isArg>(arg);
            if (!isOverwrite)
                BaseType::Message(arg);
        }

        bool isLoadedConfig = false;

        using BaseType::LoadConfig;
        void LoadConfig(Function& config)
        {
            if (!isLoadedConfig)
                caller = new Function(std::move((config)));
            else
                CallNextConfigLoader<BaseType, Function>(static_cast<BaseType *>(this), config);
            isLoadedConfig = true;
        }
    };

    template<bool isOverwrite, class BaseType, class Function>
    class OnStartImpl : public BaseType
    {
    public:
        Function *caller;
        static_assert(std::is_base_of<StageType, BaseType>::value, "Base must be a StepBase");

        ~OnStartImpl() override
        {
            delete caller;
        }

        bool Start() override
        {
            (*caller)();
            if (!isOverwrite)
                return BaseType::Start();
            return true;
        }

        bool isLoadedConfig = false;
        using BaseType::LoadConfig;
        void LoadConfig(Function& config)
        {
            if (!isLoadedConfig)
                caller = new Function(std::move((config)));
            else
                CallNextConfigLoader<BaseType, Function>(static_cast<BaseType *>(this), config);
            isLoadedConfig = true;
        }
    };

    template<bool isOverwrite, class BaseType, class Function>
    class OnStopImpl : public BaseType
    {
    public:
        Function *caller;
        static_assert(std::is_base_of<StageType, BaseType>::value, "Base must be a StepBase");

        ~OnStopImpl() override
        {
            delete caller;
        }

        void Stop() override
        {
            (*caller)();
            if (!isOverwrite)
                BaseType::Stop();
        }

        bool isLoadedConfig = false;
        using BaseType::LoadConfig;

        void LoadConfig(Function& config)
        {
            if (!isLoadedConfig)
                caller = new Function(std::move((config)));
            else
                CallNextConfigLoader<BaseType, Function>(static_cast<BaseType *>(this), config);
            isLoadedConfig = true;
        }
    };

    template<bool isOverwrite, class BaseType, class Function>
    class OnIdleImpl : public BaseType
    {
    public:
        Function *caller;
        static_assert(std::is_base_of<StageType, BaseType>::value, "Base must be a StepBase");

        ~OnIdleImpl() override
        {
            delete caller;
        }

        void Idle() override
        {
            (*caller)();
            if (!isOverwrite)
                BaseType::Idle();
        }

        bool isLoadedConfig = false;
        using BaseType::LoadConfig;

        void LoadConfig(Function& config)
        {
            if (!isLoadedConfig)
                caller = new Function(std::move((config)));
            else
                CallNextConfigLoader<BaseType, Function>(static_cast<BaseType *>(this), config);
            isLoadedConfig = true;
        }
    };



    template<bool isOverwrite, class BaseType, class Function>
    class OnPauseImpl : public BaseType
    {
    public:
        Function *caller;
        static_assert(std::is_base_of<StageType, BaseType>::value, "Base must be a StepBase");

        ~OnPauseImpl() override
        {
            delete caller;
        }

        void Pause() override
        {
            (*caller)();
            if (!isOverwrite)
                BaseType::Pause();
        }

        bool isLoadedConfig = false;
        using BaseType::LoadConfig;

        void LoadConfig(Function& config)
        {
            if (!isLoadedConfig)
                caller = new Function(std::move((config)));
            else
                CallNextConfigLoader<BaseType, Function>(static_cast<BaseType *>(this), config);
            isLoadedConfig = true;
        }
    };

    template<bool isOverwrite, class BaseType, class Function>
    class OnResumeImpl : public BaseType
    {
    public:
        Function *caller;
        static_assert(std::is_base_of<StageType, BaseType>::value, "Base must be a StepBase");

        ~OnResumeImpl() override
        {
            delete caller;
        }

        void Resume() override
        {
            (*caller)();
            if (!isOverwrite)
                BaseType::Resume();
        }

        bool isLoadedConfig = false;
        using BaseType::LoadConfig;

        void LoadConfig(Function& config)
        {
            if (!isLoadedConfig)
                caller = new Function(std::move((config)));
            else
                CallNextConfigLoader<BaseType, Function>(static_cast<BaseType *>(this), config);
            isLoadedConfig = true;
        }
    };

    template<bool isOverwrite, class BaseType, class Function>
    class OnErrorImpl : public BaseType
    {
    public:
        Function *caller;
        static_assert(std::is_base_of<StageType, BaseType>::value, "Base must be a StepBase");
        using FunctionInfo = function_traits<Function>;
        static_assert(FunctionInfo::args_count == 1,
                      "OnError: OnError Function should at least and only require one argument.");
        static_assert(std::is_same<typename FunctionInfo::template argument<0>::type, const std::string &>::value,
                      "OnError: OnError Function 1st argument should be `const std::string&`");

        ~OnErrorImpl() override
        {
            delete caller;
        }

        void Error(const std::string &err) override
        {
            (*caller)(err);
            if (!isOverwrite)
                BaseType::Error(err);
        }

        bool isLoadedConfig = false;
        using BaseType::LoadConfig;

        void LoadConfig(Function& config)
        {
            if (!isLoadedConfig)
                caller = new Function(std::move((config)));
            else
                CallNextConfigLoader<BaseType, Function>(static_cast<BaseType *>(this), config);
            isLoadedConfig = true;
        }
    };



    template<class BalanceType, class BaseType, class Function>
    class LoadBalanceImpl : public BaseType
    {
    private:
        std::vector<NConnector *> *Connector = nullptr;
    public:
        static_assert(BaseType::template getNConnectorIndex<BalanceType>() != -1,
                      "LoadBalance: BalanceType should be one of OutputTypes.");
        Function *caller;
        using FunctionInfo = function_traits<Function>;
        static_assert(std::is_same<typename FunctionInfo::return_type, int>::value,
                      "LoadBalance: Return Type of LoadBalance Function should be int.");
        static_assert(FunctionInfo::args_count == 1,
                      "LoadBalance: LoadBalance Function should at least and only require one argument.");
        static_assert(std::is_same<typename FunctionInfo::template argument<0>::type, const BalanceType &>::value,
                      "LoadBalance: LoadBalance Function's 1st argument should be `const BalanceType&`");


        ~LoadBalanceImpl() override
        {
            delete caller;
        }

        template< class Base>
        void ResolveOrder(typename std::enable_if<std::is_base_of<OrderFlag<BalanceType>, Base>::value, int>::type = 0)
        {
            auto& order_data = static_cast<OrderFlag<BalanceType>&>(*this);
            this->stats.order = order_data.order;
            order_data.order++;
        }

        template< class Base>
        void ResolveOrder(typename std::enable_if<!std::is_base_of<OrderFlag<BalanceType>, Base>::value, int>::type = 0)
        {
            //Do nothing...
            //No Reorder case.
        }

        uint32_t Forward(const BalanceType &obj) override
        {
            //Reorder trade off.
            //Load Balance will replace Forward(), may conflict with Reorder()
            ResolveOrder<BaseType>();
            int index = ((*caller)(obj)) % (Connector->size());
            assert(index >= 0 && index < Connector->size());
            auto ret = Connector->at(index)->Forward(this->stats, obj);
            return ret;


        }

        bool Start() override
        {
            Connector = BaseType::template getNConnector<BalanceType>();
            return BaseType::Start();
        }

        bool isLoadedConfig = false;
        using BaseType::LoadConfig;

        void LoadConfig(Function& config)
        {
            if (!isLoadedConfig)
                caller = new Function(std::move((config)));
            else
                CallNextConfigLoader<BaseType, Function>(static_cast<BaseType *>(this), config);
            isLoadedConfig = true;
        }
    };

    template<class CurrentType, class... ConfigTypes>
    struct GroupTypeHolder;


    template<class CurrentType, class... ConfigTypes>
    struct TypeHolder
    {
        std::vector<std::function<void *()>> *configLoaders;
        using ThisType = TypeHolder<CurrentType, ConfigTypes...>;

        /**
         * @brief Reorder current dataflow. Reorder is required when you want to order partial dataflow.
         * Sample:
         *          - Reorder Stage - work 1_1  -\
         *         /                \             >- Order,Stage3
         * Entrance                  - work 1_2 -/
         *        \
         *         - Other stage.
         * Order will not work correctly when only partial data are send to this stage.
         * @tparam ReorderType
         * @return
         */
        template<class ReorderType>
        auto Reorder() -> TypeHolder<ReorderImpl<ReorderType, CurrentType>, ConfigTypes...>
        {
            static_assert(std::is_same<ReorderType, typename std::decay<ReorderType>::type>::value,
                          "Reorder: Reorder Type should not contain any cv-specifier");
            static_assert(!std::is_base_of<OrderFlag<ReorderType>, CurrentType>::value, "Reorder: One output can not reorder more than once.");
            return TypeHolder<ReorderImpl<ReorderType, CurrentType>, ConfigTypes...>{this->configLoaders};
        }



        template<class BalanceType, class Function>
        struct LBHelper
        {
            using func = typename std::decay<Function>::type;
            using NextType = TypeHolder<LoadBalanceImpl<BalanceType, CurrentType, Function>, ConfigTypes..., Function>;

        };

        template<class BalanceType>
        class DefaultLB
        {
        public:
            int index = 0;
            int operator()(const BalanceType&)
            {
                return ++index;
            }
        };

        /**
         * @brief Attach a load balancer to specfied output, Default load balancer is Round-robin
         * @tparam BalanceType Base type
         * @tparam Function default load balancer
         * @return
         */
        template<class BalanceType, class Function = DefaultLB<BalanceType> >
        auto LoadBalancer() -> typename LBHelper<BalanceType, Function>::NextType
        {
            //static_assert is in LoadBalanceImpl
            auto pointer = new Function();
            this->configLoaders->push_back(
                    [pointer]() -> void * {
                        return pointer;
                    });
            return typename LBHelper<BalanceType, Function>::NextType{this->configLoaders};
        }

        /**
         * @brief Attach a load balancer to specfied output, Default load balancer is Round-robin
         * @tparam BalanceType Base type
         * @tparam Function load balance function
         * @return
         */
        template<class BalanceType, class Function>
        auto LoadBalancer(Function &&in) -> typename LBHelper<BalanceType, Function>::NextType
        {
            static_assert(std::is_same<BalanceType, typename std::decay<BalanceType>::type>::value,
                          "LoadBalancer(): Balance Type should not contain any cv-specifier");
            //static_assert is in LoadBalanceImpl
            auto pointer = new Function(std::move(in));
            this->configLoaders->push_back(
                    [pointer]() -> void * {
                        return pointer;
                    });
            return typename LBHelper<BalanceType, Function>::NextType{this->configLoaders};
        }

        template<template<bool, class, class> class Src, class Config, class Function, bool isOverwrite>
        struct FuncHelper
        {
            using func = typename std::decay<Function>::type;
            using NextType = TypeHolder<Src<isOverwrite, CurrentType, Function>, ConfigTypes..., Config>;
            using ConfigType = Config;
        };

        template<class Function, bool isOverwrite = false>
        using OIHelper = FuncHelper<OnStartImpl, Function, Function, isOverwrite>;

        /**
         * @brief Override Start() function
         * @tparam isOverwrite Is invoke Base::Start() automatically
         * @tparam Function override functor
         * @param in override functor
         * @return
         */
        template<bool isOverwrite = false, class Function>
        auto OnStart(Function &&in) -> typename OIHelper<Function, isOverwrite>::NextType
        {
            static_assert(FunctionChecker<Function, void>::value, "OnStart(): Invalid function Type: void() is required.");
            auto pointer = new Function(std::move(in));
            this->configLoaders->push_back(
                    [pointer]() -> void * {
                        return pointer;
                    });
            return typename OIHelper<Function, isOverwrite>::NextType{this->configLoaders};
        }

        template<class Function, bool isOverwrite = false>
        using OSHelper = FuncHelper<OnStopImpl, Function, Function, isOverwrite>;

        /**
         * @brief
         * @brief Override Stop() function
         * @tparam isOverwrite Is invoke Base::Stop() automatically
         * @tparam Function override functor
         * @param in override functor
         * @return
         */
        template<bool isOverwrite = false, class Function>
        auto OnStop(Function &&in) -> typename OSHelper<Function, isOverwrite>::NextType
        {
            static_assert(FunctionChecker<Function, void>::value, "OnStop(): Invalid function Type: void() is required.");
            auto pointer = new Function(std::move(in));
            this->configLoaders->push_back(
                    [pointer]() -> void * {
                        return pointer;
                    });
            return typename OSHelper<Function, isOverwrite>::NextType{this->configLoaders};
        }

        template<class Function, bool isOverwrite = false>
        using OIdleHelper = FuncHelper<OnIdleImpl, Function, Function, isOverwrite>;

        /**
         * @brief
         * @brief Override Idle() function
         * @tparam isOverwrite Is invoke Base::Idle() automatically
         * @tparam Function override functor
         * @param in override functor
         * @return
         */
        template<bool isOverwrite = false, class Function>
        auto OnIdle(Function &&in) -> typename OIdleHelper<Function, isOverwrite>::NextType
        {
            static_assert(FunctionChecker<Function, void>::value, "OnIdle(): Invalid function Type: void() is required.");
            auto pointer = new Function(std::move(in));
            this->configLoaders->push_back(
                    [pointer]() -> void * {
                        return pointer;
                    });
            return typename OIdleHelper<Function, isOverwrite>::NextType{this->configLoaders};
        }

        template<class Function, bool isOverwrite = false>
        using OPHelper = FuncHelper<OnPauseImpl, Function, Function, isOverwrite>;

        /**
         * @brief
         * @brief Override Pause() function
         * @tparam isOverwrite Is invoke Base::Pause() automatically
         * @tparam Function override functor
         * @param in override functor
         * @return
         */
        template<bool isOverwrite = false, class Function>
        auto OnPause(Function &&in) -> typename OPHelper<Function, isOverwrite>::NextType
        {
            static_assert(FunctionChecker<Function, void>::value, "OnPause(): Invalid function Type: void() is required.");
            auto pointer = new Function(std::move(in));
            this->configLoaders->push_back(
                    [pointer]() -> void * {
                        return pointer;
                    });
            return typename OPHelper<Function, isOverwrite>::NextType{this->configLoaders};
        }

        template<class Function, bool isOverwrite = false>
        using OEHelper = FuncHelper<OnErrorImpl, Function, Function, isOverwrite>;

        /**
         * @brief
         * @brief Override Error() function
         * @tparam isOverwrite Is invoke Base::Error() automatically
         * @tparam Function override functor
         * @param in override functor
         * @return
         */
        template<bool isOverwrite = false, class Function>
        auto OnError(Function &&in) -> typename OEHelper<Function, isOverwrite>::NextType
        {
            //static_assert(FunctionChecker<Function, void>::value, "Invalid function Type: void() is required.");
            auto pointer = new Function(std::move(in));
            this->configLoaders->push_back(
                    [pointer]() -> void * {
                        return pointer;
                    });
            return typename OEHelper<Function, isOverwrite>::NextType{this->configLoaders};
        }

        template<class Function, bool isOverwrite = false>
        using ORHelper = FuncHelper<OnResumeImpl, Function, Function, isOverwrite>;

        /**
         * @brief
         * @brief Override Resume() function
         * @tparam isOverwrite Is invoke Base::Resume() automatically
         * @tparam Function override functor
         * @param in override functor
         * @return
         */
        template<bool isOverwrite = false, class Function>
        auto OnResume(Function &&in) -> typename ORHelper<Function, isOverwrite>::NextType
        {
            static_assert(FunctionChecker<Function, void>::value, "OnResume(): Invalid function Type: void() is required.");
            auto pointer = new Function(std::move(in));
            this->configLoaders->push_back(
                    [pointer]() -> void * {
                        return pointer;
                    });
            return typename ORHelper<Function, isOverwrite>::NextType{this->configLoaders};
        }


        template<class Function, bool isOverwrite = false>
        using OMHelper = FuncHelper<OnMessageImpl, Function, Function, isOverwrite>;

        /**
         * @brief
         * @brief Implements Message() function
         * @tparam isOverwrite Is invoke Base::Message() automatically
         * @tparam Function override functor
         * @param in override functor
         * @return
         */
        template<bool isOverwrite = false, class Function>
        auto OnMessage(Function &&in) -> typename OMHelper<Function, isOverwrite>::NextType
        {
            auto pointer = new Function(std::move(in));
            this->configLoaders->push_back(
                    [pointer]() -> void * {
                        return pointer;
                    });
            return typename OMHelper<Function, isOverwrite>::NextType{this->configLoaders};
        }

        template<class InputType, int index, bool isRemove>
        static void CallLoader(InputType &obj, std::vector<std::function<void *()>> *configs)
        {}

        template<class InputType, int index, bool isRemove, class current, class... CTypes>
        static void CallLoader(InputType &obj, std::vector<std::function<void *()>> *configs)
        {
            CallLoader<InputType, index + 1, isRemove, CTypes...>(obj, configs);
            auto config = (current *) (configs->at(index)());
            obj.LoadConfig(*config);
            if(isRemove)
                delete config;
        }

        /**
         * @brief Build this stage definition to a stage object.
         * @return
         */
        auto Build() -> CurrentType *
        {
            if (configLoaders->size() != sizeof...(ConfigTypes))
                throw new std::exception();//ADK_THROW("config size dismatch!");
            auto ret = new CurrentType;
            CallLoader<CurrentType, 0, true, ConfigTypes...>(*ret, configLoaders);
            delete configLoaders;
            //ret->Init();
            return ret;
        }

        /**
         * @brief Let Build() generates several instance of this Stage.
         * Implement LoadConfig(DuplicateInfo&) in Stage can receive the index of current Stage.
         * @param count amount of instances
         */
        auto Duplicate(int count) -> GroupTypeHolder<CurrentType, ConfigTypes...>
        {
            return GroupTypeHolder<CurrentType, ConfigTypes...>{configLoaders, count};
        }
    };

    template<class CurrentType, class... ConfigTypes>
    struct GroupTypeHolder
    {
    public:
        std::vector<std::function<void *()>> *configLoaders;
        int group_count;

        /**
         * @brief Build this stage definition to a stage object.
         * @return
         */
        auto Build() -> std::vector<CurrentType *>
        {
            std::vector<CurrentType *> ret;
            for (int i = 0; i < group_count; i++)
            {
                if (configLoaders->size() != sizeof...(ConfigTypes))
                    throw new std::exception();//ADK_THROW("config size dismatch!");
                auto inst = new CurrentType;
                if(i != (group_count - 1))
                    TypeHolder<CurrentType, ConfigTypes...>::template CallLoader<CurrentType, 0, false, ConfigTypes...>(*inst,
                                                                                                                        configLoaders);
                else
                    TypeHolder<CurrentType, ConfigTypes...>::template CallLoader<CurrentType, 0, true, ConfigTypes...>(*inst,
                                                                                                                        configLoaders);
                DuplicateInfo dup;
                dup.index = i;
                CallNextConfigLoader<CurrentType, DuplicateInfo>(inst, dup);
                //inst->Init();
                ret.push_back(inst);
            }
            delete configLoaders;
            return ret;
        }
    };

    bool topologySort(StageType* base, std::vector<StageType*>& topologyOrder);

    template<class InputType, class... OutputTypes>
    class EntranceImpl : public FwdHelper<ReorderImpl, StageBase<InputType, OutputTypes...>, OutputTypes...>::NextType
    {
    private:
        std::vector<StageType*> topologyOrder;
        bool sort()
        {
            auto ret = topologySort(this, topologyOrder);
            if(!ret)
            {
                this->Error("Pipeline contains at least one cycle.");
                return false;
            }
            return true;
        }
    public:
        using BaseType = typename FwdHelper<ReorderImpl, StageBase<InputType, OutputTypes...>, OutputTypes...>::NextType;


        void Message(const InputType &)
        {
            throw new std::exception();
        }

        template<class Function>
        void TopologyInvoke(Function func)
        {
            for(int i = 0; i < topologyOrder.size(); i++)
            {
                func(topologyOrder[i]);
            }
        }

        template<class Function>
        void ReverseTopologyInvoke(Function func)
        {
            for(int i = topologyOrder.size() - 1; i >= 0; i--)
            {
                func(topologyOrder[i]);
            }
        }

        void Pause() override
        {
            BaseType::Pause();
            TopologyInvoke([](StageType *current) { current->Pause(); });
        }

        void Resume() override
        {
            BaseType::Resume();
            ReverseTopologyInvoke([](StageType *current) { current->Resume(); });
        }

        bool Start() override
        {
            BaseType::Start();
            bool result = true;
            if(!sort())
                return false;
            ReverseTopologyInvoke([&result](StageType *current) {
                result = result && current->Start();
            });
            return result;
        }

        void Indicator(boost::property_tree::ptree &tgt)
        {
            BaseType::Indicator(tgt);
            TopologyInvoke([&tgt](StageType *current) {
                boost::property_tree::ptree ele;
                current->Indicator(ele);
                tgt.add_child(current->Name(), ele);
            });
        }

        void Stop() override
        {
            BaseType::Stop();
            TopologyInvoke([](StageType *current) { current->Stop(); });
        }
    };

    template<class BaseType>
    class ExternalStageTypeImpl : public BaseType
    {
        /*
         * There is no need to check whether BaseType implemented these virtual function.
         *
         * BaseType::StageType != StageType
         * naming error.....
         */
    public:
        bool Start() override
        {
            auto result = BaseType::Start();
            result = BaseType::StageType::Start() && result;
            //Do Init...
            return result;
        }

        void Pause() override
        {
            //Do Pause...
            BaseType::Pause();
            BaseType::StageType::Pause();
        }

        void Resume() override
        {
            BaseType::StageType::Resume();
            BaseType::Resume();
            //Do Resume...
        }

        void Stop() override
        {
            BaseType::StageType::Stop();
            BaseType::Stop();
            //Do Stop...
        }
    };

    /**
     * @brief Select a class as base stage definition
     * @tparam T Base Stage
     * @return
     */
    template<class T, class DST = ExternalStageTypeImpl<T>>
    auto StageBuilder() -> TypeHolder<DST>
    {
        static_assert(std::is_base_of<StageType, T>::value, "Input Class Must Public Derived From StageBase<>");
        auto config = new std::vector<std::function<void *()>>;
        return TypeHolder<DST>{config};
    };

    using boost_ptree = boost::property_tree::ptree;

    /**
     * @brief Select a class as base stage definition
     * @tparam T
     * @param setting settings of this stage.
     * @return
     */
    template<class T, class DST = ExternalStageTypeImpl<T>>
    auto StageBuilder(boost_ptree &setting) -> TypeHolder<DST, boost_ptree>
    {
        static_assert(std::is_base_of<StageType, T>::value, "Input Class Must Public Derived From StageBase<>");
        auto config = new std::vector<std::function<void *()>>;
        auto cfg = new boost_ptree(setting);
        config->push_back([cfg]() -> void * { return cfg; });
        return TypeHolder<DST, boost_ptree>{config};
    }

    /**
     * @brief Select a class as base stage definition
     * @tparam T
     * @param setting settings of this stage.
     * @return
     */
    template<class T, class DST = ExternalStageTypeImpl<T>>
    auto StageBuilder(StageConfig &setting) -> TypeHolder<DST, StageConfig>
    {
        static_assert(std::is_base_of<StageType, T>::value, "Input Class Must Public Derived From StageBase<>");
        auto config = new std::vector<std::function<void *()>>;
        auto cfg = new StageConfig(setting);
        config->push_back([cfg]() -> void * { return cfg; });
        return TypeHolder<DST, StageConfig>{config};
    }

    /**
     * @brief Select default stage definition with specified Input Type , and Output Types.
     * @tparam ArgType Input Type
     * @tparam OutputTypes Output Types
     * @return
     */
    template<class ArgType, class... OutputTypes>
    auto DefaultBuilder() -> decltype(StageBuilder<StageBase<ArgType, OutputTypes...> >())
    {
        return StageBuilder<StageBase<ArgType, OutputTypes...> >();
    }

    /**
     * @brief Select default stage definition with specified Input Type , and Output Types.
     * @tparam ArgType Input Type
     * @tparam OutputTypes Output Types
     * @param setting settings of this stage.
     * @return
     */
    template<class ArgType, class... OutputTypes>
    auto DefaultBuilder(boost_ptree &setting) -> decltype(StageBuilder<StageBase<ArgType, OutputTypes...> >(setting))
    {
        return StageBuilder<StageBase<ArgType, OutputTypes...> >(setting);
    }

    /**
     * @brief Select default stage definition with specified Input Type , and Output Types.
     * @tparam ArgType Input Type
     * @tparam OutputTypes Output Types
     * @param setting settings of this stage.
     * @return
     */
    template<class ArgType, class... OutputTypes>
    auto DefaultBuilder(StageConfig &setting) -> decltype(StageBuilder<StageBase<ArgType, OutputTypes...> >(setting))
    {
        return StageBuilder<StageBase<ArgType, OutputTypes...> >(setting);
    }

/**
 * @brief Returns a Entrance definition to build.
 * @tparam OutputTypes
 * @return
 */
    template<class... OutputTypes>
    auto Entrance() -> decltype(StageBuilder<EntranceImpl<int, OutputTypes...>>(std::declval<StageConfig&>()))
    {
        StageConfig cfg;
        cfg.name = "DefaultEntrance";
        cfg.is_same_context = true;
        return StageBuilder<EntranceImpl<int, OutputTypes...>>(cfg);
    }

/**
 * @brief Returns a Entrance definition to build.
 * @tparam OutputTypes
 * @return
 */
    template<class... OutputTypes>
    auto Entrance(StageConfig& cfg) -> decltype(StageBuilder<EntranceImpl<int, OutputTypes...>>(cfg))
    {
        cfg.is_same_context = true;
        return StageBuilder<EntranceImpl<int, OutputTypes...>>(cfg);
    }

    template<class F>
    struct transformHelper
    {
        using FuncInfo = function_traits<F>;

        static_assert(FuncInfo::args_count == 1, "select(): select functor can only have one parameter.");
        static_assert(std::is_same<typename FuncInfo::return_type, void>::value == false,
                      "select(): select functor must return a value.");

        using RetType = typename std::decay<typename FuncInfo::return_type>::type;
        using ArgType = typename std::decay<typename FuncInfo::template argument<0>::type>::type;

        static_assert(std::is_same<typename FuncInfo::template argument<0>::type, const ArgType &>::value,
                      "select(): 1st parameter's cv specifier should be `const T&`");

        using TargetType = decltype(DefaultBuilder<ArgType, RetType>().OnMessage(std::declval<F &&>()).Build());

        static TargetType Gen(F &&func)
        {
            StageConfig cfg;
            cfg.is_same_context = true;
            return DefaultBuilder<ArgType, RetType>(cfg).OnMessage(std::forward<F &&>(func)).Build();
        }
    };

    /**
     * @brief create a transform stage in-place.
     * @tparam F
     * @param function
     * @return
     */
    template<class F>
    auto transform(F &&function) -> decltype((*std::declval<typename transformHelper<F>::TargetType>()))
    {
        return *transformHelper<F>::Gen(std::forward<F &&>(function));
    }

    template<class F>
    struct whereHelper
    {
        using FuncInfo = function_traits<F>;
        static_assert(FuncInfo::args_count == 1, "where(): where functor can only have one parameter.");
        static_assert(std::is_same<typename FuncInfo::return_type, bool>::value,
                      "where(): where functor must return a bool value.");

        using ArgType = typename std::decay<typename FuncInfo::template argument<0>::type>::type;

        using StageType = StageBase<ArgType, ArgType>;

        class whereStage : public StageType
        {
            F *func;
        public:
            void Message(const ArgType &in) override
            {
                bool ret = (*func)(in);
                if (ret)
                    this->Forward(in);
            }

            bool isLoaded;

            using StageType::LoadConfig;

            void LoadConfig(F* function)
            {
                if (!isLoaded)
                    func = function;
                isLoaded = true;
            }

            ~whereStage() override
            {
                delete func;
            }
        };

        static whereStage *Gen(F &&func)
        {
            StageConfig cfg;
            cfg.is_same_context = true;
            whereStage *ret = StageBuilder<whereStage>(cfg).Build();
            ret->LoadConfig(new F(std::forward<F &&>(func)));
            return ret;
        }
    };

    /**
     * @brief create a filter stage in-place.
     * @tparam F
     * @param function filter functor.
     * @return
     */
    template<class F>
    auto where(F &&function) -> decltype(*(whereHelper<F>::Gen(std::declval<F &&>())))
    {
        return *whereHelper<F>::Gen(std::forward<F &&>(function));
    }

}

#endif //NANOLOGTEST_PIPELINE_NEW_H
