#ifndef ADK_IMPL_PERF_MEASURE_H_
#define ADK_IMPL_PERF_MEASURE_H_

#include "constant.h"
#include "high_performance_clock.h"

#include <sched.h>
#include <sys/syscall.h>

#include <string>

#include <boost/function.hpp>
#include <boost/static_assert.hpp>
// #include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/variadic.hpp>
// #include <boost/preprocessor/tuple/elem.hpp>
#include <boost/preprocessor/seq/for_each_i.hpp>
// #include <boost/preprocessor/seq/for_each.hpp>

namespace adk_impl
{

#define ADK_MAX_PERF_DURATIONS		10
#define ADK_MAX_PERF_RECORDS		8192
#define ADK_PERF_RECORDS_MASK		((ADK_MAX_PERF_RECORDS - 1))
#define ADK_PERF_OVERRUN_DROPS		128

#define ADK_DEFINE_PERF_DURATION_RAW(ns, name, value) namespace adk_impl { namespace ns { const int name = value; } }
#define ADK_DEFINE_PERF_DURATION(name, value) ADK_DEFINE_PERF_DURATION_RAW(perf, name, value)

// #define ADK_PEEK_DURATION_NAME(tuple_field) BOOST_PP_TUPLE_ELEM(1, 0, tuple_field)
#define ADK_DEFINE_PERF_DURATION_INTERNAL(r, data, n, name)	namespace adk_impl { namespace perf { namespace duration { const int name = n; } } }
#define ADK_DEFINE_PERF_DURATIONS(...) \
	BOOST_PP_SEQ_FOR_EACH_I(ADK_DEFINE_PERF_DURATION_INTERNAL, _, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))

#define ADK_CAT(a, b) ADK_CAT_I(a, b)
#define ADK_CAT_I(a, b) ADK_CAT_II(~, a ## b ## _)
#define ADK_CAT_II(p, res) res

#define ADK_GEN_UNIQUE_VAR(a) ADK_CAT(a, __LINE__)
#define ADK_GEN_UNIQUE_VAR_I(a, i) ADK_CAT(a ## i ## _, __LINE__)

#define ADK_REGISTER_THREAD_HELPER(i, thread_class, duration_name)	\
	static adk_impl::perf::RegisterPerfThreadHelper ADK_GEN_UNIQUE_VAR_I(g_register_perf_thread, i)(thread_class, #duration_name, duration_name);

// user interfaces begin
#define ADK_DEFINE_AND_REGISTER_PERF_DURATION(r, thread_class, i, duration_name)	\
	namespace adk_impl {	\
	namespace perf { 	\
	namespace duration {	\
	const int duration_name = i; 	\
	ADK_REGISTER_THREAD_HELPER(i, thread_class, duration_name)	\
	}	\
	}	\
	}
// static adk_impl::perf::RegisterPerfThreadHelper ADK_GEN_UNIQUE_VAR_I(g_register_perf_thread, i)(thread_class, #duration_name, duration_name);

#define ADK_DEFINE_PERF_DURATION_SEQ(...)	BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__)
#define ADK_DEFINE_PERF_THREAD(thread_class, _seq)	\
	BOOST_PP_SEQ_FOR_EACH_I(ADK_DEFINE_AND_REGISTER_PERF_DURATION, thread_class, _seq)
// user interfaces end

	// ADK_DEFINE_PERF_DURATIONS(_seq);
	// static adk_impl::perf::RegisterPerfThreadHelper g_register_perf_thread(thread_name);


#define ADK_PERF_COUNT_GUARD(duration) adk_impl::perf::ScopedCount<duration> ADK_GEN_UNIQUE_VAR(count_guard)
#define ADK_PERF_SAVE_GUARD() adk_impl::perf::ScopedSave save_guard

namespace perf
{

struct TickRecord
{
	uint64_t 		guard_begin __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
	uint64_t 		msg_id;
	uint64_t 		parent_id;
	uint64_t 		tick_durations[ADK_MAX_PERF_DURATIONS][2]; // __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
	uint64_t 		guard_end;								// 10 * 2 + 4 == 24 == 8 * 3

	TickRecord()
	{
		guard_begin = 0;
		guard_end = 0;
	}
};

struct TickSaveContext
{
	uint64_t position;
	uint32_t is_save_start;
	TickRecord tick_records[ADK_MAX_PERF_RECORDS];
	
	TickSaveContext()
	{
		position = 0;
		is_save_start = 0;
	}
};

// struct BudgetInfo
// {
// 	uint32_t nr_jobs;
// 	uint32_t nr_ticks;
// };

// int32_t PerfRun(BudgetInfo budget);

// inline int32_t PerfRun(int32_t budget)
// {
// 	BudgetInfo budget_info = {budget, -1U};
// 	return PerfRun(budget_info);
// }


// class PerfService
// {
// public:
// 	PerfService()	// FIXME: support IO_SERVICE!
// 	{
// 		data_file_path_ = data_file_path;
// 	}

// 	~PerfService()
// 	{}

// 	int32_t Init(const std::string& data_file_path);

// 	int32_t Run();

// 	int32_t Run(int32_t budget);

// 	int32_t Run(BudgetInfo budget);

// 	int32_t RunOne();

// 	int32_t Stop();

// 	int32_t Reset();

// private:
// 	std::string data_file_path_;
// };

// user interfaces begin
void SetDataFilePath(const std::string& file_path);
// user interfaces end

#ifndef __ADK_DISABLE_PERFING__

extern __thread TickRecord*  	 	g_tick_record;
extern __thread TickSaveContext*  	g_tick_save_context;

// user interfaces begin
extern void ThreadOnline(const std::string& thread_class, const std::string& thread_name);

inline void ThreadOnline3rd(const std::string& thread_class, const std::string& thread_name, int cpu_no = -1)
{
	if (ADK_UNLIKELY(g_tick_save_context == NULL))
	{
		if (cpu_no != -1)
		{
			cpu_set_t var_cpuset;
			CPU_ZERO(&var_cpuset);
			CPU_SET(cpu_no, &var_cpuset);
			sched_setaffinity(0, sizeof(cpu_set_t), &var_cpuset);
			// struct sched_param var_sched_param;
			// var_sched_param.sched_priority = 50;
			// sched_setscheduler(0, SCHED_FIFO, &var_sched_param);
		}

		if (!thread_name.empty())
        {
            ThreadOnline(thread_class, thread_name);
        }
		else
        {
            ThreadOnline(thread_class, std::to_string(syscall(SYS_gettid)));
        }
	}
}

void StopService();
// user interfaces end

struct RegisterHelper
{
public:
	RegisterHelper()
		:	thread_class_(NULL)
	{}

	RegisterHelper& operator()(const std::string& record_point, const int index);

	const std::string* thread_class_;
};

RegisterHelper& RegisterThreadInternal(const std::string& thread_class);

template<int index>
inline void CountBegin()	// const int& index
{
	BOOST_STATIC_ASSERT(index < ADK_MAX_PERF_DURATIONS);
	g_tick_record->tick_durations[index][0] = tick::SyncRead();
}

template<int index>
inline void CountEnd() 		// const int& index
{
	BOOST_STATIC_ASSERT(index < ADK_MAX_PERF_DURATIONS);
	g_tick_record->tick_durations[index][1] = tick::SyncRead();
}

inline bool SaveBegin(uint64_t msg_id = 0, uint64_t parent_id = 0)
{
	if (ADK_UNLIKELY((++g_tick_save_context->is_save_start) != 1))
		return true;

	const uint64_t ring_pos = g_tick_save_context->position & ADK_PERF_RECORDS_MASK;
	g_tick_record = &(g_tick_save_context->tick_records[ring_pos]);
	++(g_tick_record->guard_begin);
	ADK_BARRIER();
	return true;
}

inline bool SaveEnd()
{
	TickSaveContext* const tick_save_context = g_tick_save_context;
	if (ADK_UNLIKELY((--g_tick_save_context->is_save_start)))
		return true;

	TickRecord* const tick_record = g_tick_record;

	ADK_BARRIER();
	++(tick_record->guard_end);
	ADK_BARRIER();
	++(tick_save_context->position);
	return true;
}
#if 0
inline void CountBegin3rd(const int& index, const char* name)
{
	if (ADK_UNLIKELY(g_tick_record == NULL))
		RegisterThread(name);
	assert(index < ADK_MAX_PERF_DURATIONS);
	g_tick_record->tick_durations[index][0] = Read();
}

inline void CountEnd3rd(const int& index)
{
	if (ADK_UNLIKELY(g_tick_record == NULL))
		RegisterThread(name);
	assert(index < ADK_MAX_PERF_DURATIONS);
	g_tick_record->tick_durations[index][1] = Read();
}

inline bool SaveBegin3rd(uint64_t msg_id = 0, uint64_t parent_id = 0)
{
	if (ADK_UNLIKELY(g_tick_save_context == NULL))
		RegisterThread(name);
	const ring_pos = g_tick_save_context->position & ADK_PERF_RECORDS_MASK;
	g_tick_record = &(g_tick_save_context->tick_durations[ring_pos]);
	++(g_tick_record->guard_begin);
	ADK_BARRIER();
	return true;
}

inline bool SaveEnd3rd()
{
	if (ADK_UNLIKELY(g_tick_save_context == NULL))
		RegisterThread(name);
	++(g_tick_record->.guard_end);
	ADK_BARRIER();
	++(g_tick_save_context->position);
	return true;
}
#endif

template<int index>
class ScopedCount 
{
public:
	inline ScopedCount()
	{
		BOOST_STATIC_ASSERT(index < ADK_MAX_PERF_DURATIONS);
		CountBegin<index>();
	}

	inline ~ScopedCount()
	{
		BOOST_STATIC_ASSERT(index < ADK_MAX_PERF_DURATIONS);
		CountEnd<index>();
	}
};

template<int index>
class ScopedCountCondition 
{
public:
	inline ScopedCountCondition(bool condition)
	{
		condition_ = condition;
		BOOST_STATIC_ASSERT(index < ADK_MAX_PERF_DURATIONS);
		if (condition)
			CountBegin<index>();
	}

	inline ~ScopedCountCondition()
	{
		BOOST_STATIC_ASSERT(index < ADK_MAX_PERF_DURATIONS);
		if (condition_)
			CountEnd<index>();
	}

	bool condition_;
};

class ScopedSave
{
public:
	inline ScopedSave(uint64_t msg_id = 0, uint64_t parent_id = 0)
	{
		SaveBegin(msg_id, parent_id);
	}

	inline ~ScopedSave()
	{
		SaveEnd();
	}
};

class ScopedSaveCondition
{
public:
	inline ScopedSaveCondition(bool condition, uint64_t msg_id = 0, uint64_t parent_id = 0)
	{
		condition_ = condition;
		if (condition)
			SaveBegin(msg_id, parent_id);
	}

	inline ~ScopedSaveCondition()
	{
		if (condition_)
			SaveEnd();
	}

	bool condition_;
};

#if 0
struct ScopedCount3rd
{
	inline ScopedCount3rd(const int index)
	{
		index_ = index;
		CountBegin3rd(index_);
	}

	inline ~ScopedCount3rd()
	{
		CountEnd3rd(index_);
	}

private:
	const int index_;
};

struct ScopedSave3rd
{
	inline ScopedSave3rd(uint64_t msg_id = 0, uint64_t parent_id = 0)
	{
		CountBegin3rd(msg_id, parent_id);
	}

	inline ~ScopedSave3rd()
	{
		CountEnd3rd();
	}
};
#endif

#else

inline void ThreadOnline(const std::string& thread_class, const std::string& thread_name) {}
void StopService() {}
inline void ThreadOnline3rd(boost::function<void ()>* func) {}

struct RegisterHelper
{
public:
	RegisterHelper()
		:	thread_class_(NULL)
	{}

	RegisterHelper& operator()(const string&， const int)
	{}

	const std::string* thread_class_;
}

RegisterHelper& RegisterThreadInternal(const std::string& thread_class) {}

template<int index>
inline void CountBegin() {}

template<int index>
inline void CountEnd() {}

inline bool SaveBegin(uint64_t msg_id = 0, uint64_t parent_id = 0) {}
inline bool SaveEnd() {}

// inline void CountBegin3rd(const int& index, const char* name){}
// inline void CountEnd3rd(const int& index) {}
// inline bool SaveBegin3rd(uint64_t msg_id = 0, uint64_t parent_id = 0) {}
// inline bool SaveEnd3rd() {}

template<int index>
class ScopedCount { public: ScopedCount(const int) {} };

template<int index>
class ScopedCountCondition { public: ScopedCountCondition(bool condition) {} };

class ScopedSave { public: inline ScopedSave(uint64_t msg_id = 0, uint64_t parent_id = 0) {} };

class ScopedSaveCondition { public: ScopedSaveCondition(bool condition, uint64_t msg_id = 0, uint64_t parent_id = 0) {} };

// struct ScopedCount3rd { ScopedCount3rd(const int) {} };
// struct ScopedSave3rd { inline ScopedSave3rd(uint64_t msg_id = 0, uint64_t parent_id = 0) {} };

#endif

class RegisterPerfThreadHelper
{
public:
	RegisterPerfThreadHelper(const std::string& thread_class,
							 const std::string& duration_name,
							 const int duration)
	{
		RegisterThreadInternal(thread_class)
							  (duration_name, duration);
	}

	~RegisterPerfThreadHelper()
	{}
};

} // perf

} // adk

#endif // ADK_PERF_MEASURE_H_
