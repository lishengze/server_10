#include <adk/monitor/indicator_writer.h>
#include <adk_pack/monitor/indicator_writer.h>

namespace adk
{

IndicatorWriter::IndicatorWriter()
{
    indicator_writer_impl_ = reinterpret_cast<void*>(new adk_impl::IndicatorWriter);
}

IndicatorWriter::~IndicatorWriter()
{
    assert(indicator_writer_impl_);
    // delete reinterpret_cast<adk_impl::IndicatorWriter*>(indicator_writer_impl_);
}

int32_t IndicatorWriter::Init(const boost::filesystem::path& dir_path, const std::string& app_name)
{
    return reinterpret_cast<adk_impl::IndicatorWriter*>(indicator_writer_impl_)->Init(dir_path, app_name);
}

int32_t IndicatorWriter::Write(const std::string& key, const std::string& desc, const boost::property_tree::ptree& ptree)
{
    return reinterpret_cast<adk_impl::IndicatorWriter*>(indicator_writer_impl_)->Write(key, desc, ptree);
}

}