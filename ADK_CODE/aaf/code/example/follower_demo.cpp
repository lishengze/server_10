#include <aaf.h>
#include <adk/util.h>
#include <adk/pipeline.h>
#include <adk/lock_free_msg_queue.h>

#include <time.h>
#include <ami/message.h>

using namespace aaf;


struct BussinessType
{
    uint32_t bussiness_id;
    uint32_t msg_len;
    char    msg[512];
};

struct AppMessage
{
    uint64_t app_id;
    uint32_t app_type;
};

class EntranceStage : public adk::StageWorker<ADK_IO(ami::Message*, AppMessage*)>
{
public:
    EntranceStage(const std::string& name)
        :   adk::StageWorker<ADK_IO(ami::Message*, AppMessage*)>(name)
    {}

    ~EntranceStage()
    {}  

    void OnMessage(ami::Message*& msg, short dim, short idx) override
    {
        usleep(1);
        ++cnt_;
        
        std::string word("message_sqn=");
        word.append(std::to_string(cnt_));
        BussinessType* user = (BussinessType*)business_info();
        memcpy(user->msg, word.c_str(), word.size());
        user->msg_len = word.size();
        user->bussiness_id = syscall(SYS_gettid);
        // message += offset_;

        AppMessage* app_msg = new AppMessage();
        app_msg->app_id = msg->get_total_order_seq_num();
        app_msg->app_type = msg->get_endpoint_id();
        Forward(app_msg);
    }

    int64_t offset_;
    uint64_t cnt_ = 0;
};


class AppStageWorker : public adk::StageWorker<ADK_IO(AppMessage*, AppMessage*)>
{
public:
    AppStageWorker(const std::string& name)
        :   StageWorker<ADK_IO(AppMessage*, AppMessage*)>(name)
    {}

    ~AppStageWorker()
    {}  

    virtual void OnMessage(AppMessage*& message, short dim, short idx)
    {
        // cout << "message = " << message << endl;
        usleep(1);
        ++cnt_;
        // SetTotalOrderSqn(cnt_);
        std::string word("message_sqn=");
        word.append(std::to_string(cnt_));
        BussinessType* user = (BussinessType*)business_info();
        memcpy(user->msg, word.c_str(), word.size());
        user->msg_len = word.size();
        user->bussiness_id = syscall(SYS_gettid);
        // message += offset_;
        Forward(message);
    }

    int64_t offset_;
    uint64_t cnt_ = 0;
};

class FinalStage : public adk::StageWorker<ADK_INPUT(1, AppMessage*)>
{
public:
    FinalStage(const std::string& name)
        :   StageWorker<ADK_INPUT(1, AppMessage*)>(name)
    {}

    ~FinalStage()
    {}  

    virtual void OnMessage(AppMessage*& message, short dim, short idx)
    {
        usleep(1);
        ++cnt_;
        std::string word("message_sqn=");
        word.append(std::to_string(cnt_));
        BussinessType* user = (BussinessType*)business_info();
        memcpy(user->msg, word.c_str(), word.size());
        user->msg_len = word.size();
        user->bussiness_id = syscall(SYS_gettid);
        // GetContext()->ProcessMessageDone(app_msg->app_id);
        context_->ProcessMessageDone(adk::g_pipeline_total_order_seq_num);
        delete message;
    }
    uint64_t cnt_ = 0;
    ami::Context* context_;
};


class FollowerDemo : public GenericAmiApplication
{
    ADK_LOG_DECLARE_AC(300000);
public:
    FollowerDemo()
    {
    }

    ~FollowerDemo()
    {}

    virtual void OnConfigureFramework(ami::Property& fw_props)
    {
        fw_props.SetValue(config::kEnableHighAvailableContext, true);
        fw_props.SetValue(config::kEnableSingletonContext, false);
        fw_props.SetValue(config::kEnableAppNameCheck, false);
    }

    virtual void OnConfigureContextProperty(const std::string& context_name,
                                            bool is_ha_ctx,
                                            ami::Property& props)
    {
        // props.SetValue(ami::config::context::kBusinessSwitchFileDirectory, "./atp_switch/");
    }

    virtual int32_t OnAmiInitBegin()
    {
        adk::EnableShareMemoryDump(nullptr);

        EntranceStage* stage_worker_1 = new EntranceStage("stage_worker_1");
        AppStageWorker* stage_worker_2 = new AppStageWorker("stage_worker_2");
        AppStageWorker* stage_worker_3 = new AppStageWorker("stage_worker_3");
        stage_worker_3->SetBackoffPolicy<adk::policy::Delay>();

        final_stage_ = new FinalStage("stage_worker_sequencial");

        pipeline_ = new adk::Pipeline("ATP", GetApplicationName() + "_TE_Cash", 1024);

        pipeline_->Connect<adk::Pipeline::kInplace, AppMessage*>(*stage_worker_1, *stage_worker_2);
        pipeline_->Connect<adk::Pipeline::kMessaging, AppMessage*>(*stage_worker_2, *stage_worker_3);
        pipeline_->Connect<adk::Pipeline::kMessaging, AppMessage*>(*stage_worker_3, *final_stage_);

        entrance_ = pipeline_->CreateEntrance<adk::Pipeline::kInplace>(*stage_worker_1, 0, 1024);

        if (!pipeline_->Start())
        {
            ADK_LOG_ERROR_AC_TF("Pipeline Start fialed", "error: {1}", pipeline_->GetLastError());
            return ErrorCode::kFailure;
        }

        return ErrorCode::kSuccess;
    }

    int32_t OnTxEndpointCreationBegin() override
    {
        final_stage_->context_ = GetContext();
        return ErrorCode::kSuccess;
    }

    void OnDiscardMessage(std::string& msg) override
    {
        ADK_LOG_INFO_AC_TF("discard messages", "file: {1}", msg);
    }

    virtual void OnMessage(ami::Message* msg)
    {
        InnerOnMessage(msg);
    }

    void InnerOnMessage(ami::Message* msg)
    {
        auto sqn = msg->get_total_order_seq_num();
        adk::g_pipeline_total_order_seq_num = sqn;

        entrance_->Forward(msg);
    }

    virtual int32_t OnRun()
    {
        sleep(1);
        return ErrorCode::kPassed;
    }

private:
    adk::Pipeline*       pipeline_ = nullptr; 
    adk::PipelineEntrance<ami::Message*, 1>* entrance_ = nullptr;
    FinalStage* final_stage_ = nullptr;

}g_ami_follower;

ADK_LOG_DEFINE(FollowerDemo);
