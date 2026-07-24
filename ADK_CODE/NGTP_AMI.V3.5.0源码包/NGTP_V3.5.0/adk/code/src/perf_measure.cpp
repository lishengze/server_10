#include <unistd.h>
#include <sys/types.h>

#include <boost/format.hpp>
#include <boost/thread/thread.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <adk/util.h>
#include <adk/perf_measure.h>
#include <adk/entry_wrapper.h>
#include <adk/high_performance_clock.h>

namespace adk_impl
{

namespace perf
{

__thread TickRecord*  		g_tick_record = NULL;
__thread TickSaveContext* 	g_tick_save_context = NULL;

class TickSaver
{
public:
	TickSaver()
	{
		is_running_ = false;
		fp_ = NULL;
		is_init_ = false;
	}

	struct PerfStats
	{
		int64_t avg;
		int64_t min;
		int64_t max;
		uint64_t nr_records;

		PerfStats()
		{
			Reset();
		}

		void Reset()
		{
			avg = 0;
			min = (1L << 62);
			max = -1;
			nr_records = 0;			
		}
	};

	struct IndexNameTable
	{
		int num_index;
		int index_exist[ADK_MAX_PERF_DURATIONS];
		std::string index_name[ADK_MAX_PERF_DURATIONS];

		IndexNameTable()
		{
			num_index = 0;
		}
	};

	struct ThreadContext
	{
		TickSaveContext save_context;
		uint64_t recorder_pos_cached;
		uint64_t saver_position;
		PerfStats pf_stats[ADK_MAX_PERF_DURATIONS];
		IndexNameTable* index_name_table;
		ThreadContext()
		{
			saver_position = 0;
			index_name_table = NULL;
		}
	};

	struct ThreadDuration
	{
		IndexNameTable* index_name_table;
		RegisterHelper reg_helper;
		ThreadDuration()
		{
			index_name_table = NULL;
		}	
	};

	void TickSaverMain()
	{
		pid_t pid = getpid();
		FILE* fp;
		if (!data_file_path_.empty())
		{
			fp = fopen(data_file_path_.c_str(), "w+");
		}
		else
		{
			fp = fopen((boost::format("%1%adk_perf_%2%") % data_file_path_ % pid).str().c_str(), "w+");
		}

		if (fp == NULL)
		{
			is_running_ = false;	// FIXME: on_error?
			return;
		}

		struct timespec next_save_time;
		clock_gettime(CLOCK_MONOTONIC_RAW, &next_save_time);
		next_save_time.tv_sec += 1;
		while (is_running_)
		{
			bool has_job = false;
			{
				boost::mutex::scoped_lock lock_guard(mutex_);

				for (auto it = contexts_.begin(); it != contexts_.end(); ++it)
				{
					ThreadContext& context = it->second;
					context.recorder_pos_cached = context.save_context.position;
					if (context.saver_position < context.recorder_pos_cached)
					{
						if (context.saver_position + ADK_PERF_RECORDS_MASK < context.recorder_pos_cached)
						{
							context.saver_position = context.recorder_pos_cached 
													 - ADK_PERF_RECORDS_MASK 
													 + ADK_PERF_OVERRUN_DROPS;
						}
						has_job = true;
						DoSave(fp, it->first, context);
					}
				}
			}

			struct timespec time_now;
			clock_gettime(CLOCK_MONOTONIC_RAW, &time_now);
			if (time_before(next_save_time, time_now))
			{
				has_job = true;
				next_save_time.tv_sec += 1;
				boost::posix_time::ptime time_now_ = boost::posix_time::microsec_clock::local_time();
				time_str_ = boost::posix_time::to_iso_extended_string(time_now_);
				time_str_.replace(10, 1, 1, ' ');

				for (auto it = contexts_.begin(); it != contexts_.end(); ++it)
				{
					ThreadContext& context = it->second;
					DoSave2(fp, it->first, context);
				}
			}

			if (!has_job)
			{
				fflush(fp);
				usleep(1000);
				tick::Adjust();
			}
		}

		fclose(fp);
	}

	RegisterHelper& RegisterThread(const std::string& thread_class)
	{
		boost::mutex::scoped_lock lock_guard(mutex_);
		auto it = thread_duration_map_.find(thread_class);
		if (it != thread_duration_map_.end())
			return it->second.reg_helper;

		ThreadDuration& thread_duration = thread_duration_map_[thread_class];
		thread_duration.index_name_table = new IndexNameTable();
		thread_duration.reg_helper.thread_class_ = &(thread_duration_map_.find(thread_class)->first);
		return thread_duration.reg_helper;
	}

	bool ThreadOnline(const std::string& thread_class, const std::string& thread_name)
	{
		boost::mutex::scoped_lock lock_guard(mutex_);
		auto it = thread_duration_map_.find(thread_class);
		if (it != thread_duration_map_.end())
		{
			ThreadContext& context = contexts_[thread_name];

			g_tick_save_context = &(context.save_context);
			g_tick_record = &(g_tick_save_context->tick_records[0]);

			context.index_name_table = it->second.index_name_table;
			return true;
		}
		return false;
	}

	void Start()
	{
		{
			boost::mutex::scoped_lock lock_guard(mutex_);
			if (is_running_)
				return;
			is_running_ = true;
		}

		saver_thread_ = boost_thread("adk-perfmeasure", "tick saver thread", boost::bind(&TickSaver::TickSaverMain, this));
	}

	void Stop()
	{
		{
			boost::mutex::scoped_lock lock_guard(mutex_);
			if (!is_running_)
				return;
			is_running_ = false;
		}

		if (saver_thread_.joinable())
			saver_thread_.join();
	}

private:
	FILE*			fp_;
	bool 			is_running_;
	bool 			is_init_;
	boost::thread 	saver_thread_;
	boost::mutex    mutex_;
	std::map<std::string, ThreadContext> contexts_;
	std::map<std::string, ThreadDuration> thread_duration_map_;
	std::string 	data_file_path_;
	std::string 	time_str_;
	
	void DoSave2(FILE* fp, const std::string& thread_name, ThreadContext& context)
	{
		IndexNameTable& table = *(context.index_name_table);
		bool first = true;
		for (int i = 0; i < table.num_index - 1; ++i)
		{
			int index = table.index_exist[i];
			if (context.pf_stats[index].nr_records > 0)
			{
				if (ADK_UNLIKELY(first))
				{
					fprintf(fp, "%s thread_name : %s, ", time_str_.c_str(), thread_name.c_str());
				}
				first = false;

				context.pf_stats[index].avg = 
						context.pf_stats[index].avg / context.pf_stats[index].nr_records;

				// fprintf(fp, "{%s : %11ld %11ld %11ld}, ", 
				fprintf(fp, "{%s : %ld %ld %ld}, ", 
					table.index_name[index].c_str(),
					context.pf_stats[index].avg,
					context.pf_stats[index].min,
					context.pf_stats[index].max);

				context.pf_stats[index].Reset();
			}
		}

		int index = table.index_exist[table.num_index - 1];
		if (context.pf_stats[index].nr_records > 0)
		{
			context.pf_stats[index].avg = 
					context.pf_stats[index].avg / context.pf_stats[index].nr_records;

			fprintf(fp, "{%s : %ld %ld %ld} \n", 
				table.index_name[index].c_str(),
				context.pf_stats[index].avg,
				context.pf_stats[index].min,
				context.pf_stats[index].max);
			context.pf_stats[index].Reset();
		}
	}

	void DoSave(FILE* fp, const std::string& thread_name, ThreadContext& context)
	{
		uint64_t g_ticks_per_sec_save = tick::g_ticks_per_sec;
		// FIXME : to break the starvation
		uint32_t rounds = 0;
		do
		{
			if (rounds == ADK_MAX_PERF_RECORDS)
			{
				break;
			}
			++rounds;

			TickRecord& record = context.save_context.tick_records[context.saver_position & ADK_PERF_RECORDS_MASK];
			TickRecord tmp_record;
			const uint64_t guard_end = record.guard_end;
			ADK_BARRIER();

			memcpy(&tmp_record, &record, sizeof(tmp_record));

			ADK_BARRIER();
			const uint64_t guard_begin = record.guard_begin;

			uint64_t round_indx = (context.saver_position / ADK_MAX_PERF_RECORDS) + 1;
			if (guard_end != round_indx || guard_begin != guard_end)
			{
				context.recorder_pos_cached = context.save_context.position;
				context.saver_position += (context.recorder_pos_cached - context.saver_position) / 2;
				continue;
			}

			IndexNameTable& table = *(context.index_name_table);

			for (int i = 0; i < table.num_index; ++i)
			{
				int index = table.index_exist[i];
				int64_t la_time_diff = 
					(tmp_record.tick_durations[index][1] - tmp_record.tick_durations[index][0])
					 * 1000000000ul / g_ticks_per_sec_save;
				context.pf_stats[index].avg += la_time_diff;
				++context.pf_stats[index].nr_records;
				context.pf_stats[index].min = std::min(context.pf_stats[index].min, la_time_diff);
				context.pf_stats[index].max = std::max(context.pf_stats[index].max, la_time_diff);
			}
			++context.saver_position;
			continue;
			fprintf(fp, "thread_name : %s, msg_id : %ld, parent_id : %ld, ", thread_name.c_str(), tmp_record.msg_id, tmp_record.parent_id);
			for (int i = 0; i < table.num_index - 1; ++i)
			{
				int index = table.index_exist[i];
				fprintf(fp, "{%s : %11ld %11ld}, ", table.index_name[index].c_str(),
													(tmp_record.tick_durations[index][1] - tmp_record.tick_durations[index][0]) * 1000000000ul / g_ticks_per_sec_save,
													tmp_record.tick_durations[index][1] - tmp_record.tick_durations[index][0]);
			}

			int index = table.index_exist[table.num_index - 1];
			fprintf(fp, "{%s : %11ld %11ld} \n", table.index_name[index].c_str(),
												 (tmp_record.tick_durations[index][1] - tmp_record.tick_durations[index][0]) * 1000000000ul / g_ticks_per_sec_save,
												 tmp_record.tick_durations[index][1] - tmp_record.tick_durations[index][0]);

			++context.saver_position;
		} while (context.saver_position < context.recorder_pos_cached);

		return;
		fflush(fp);
	}

	friend RegisterHelper& RegisterHelper::operator()(const std::string&, const int);
	friend void SetDataFilePath(const std::string& file_path);
	friend void ThreadOnline(const std::string& thread_class, const std::string& thread_name);
	// friend int32_t PerfRun(BudgetInfo budget);
}* g_ticks_saver = NULL;

void SetDataFilePath(const std::string& file_path)
{
	if (g_ticks_saver == NULL)
		return;

	g_ticks_saver->data_file_path_ = file_path;
}

#ifndef __ADK_DISABLE_PERFING__
RegisterHelper& RegisterHelper::operator()(const std::string& index_name_str, const int index)
{
	if (index >= ADK_MAX_PERF_DURATIONS)
		return *this;

	assert(g_ticks_saver != NULL);

	TickSaver::IndexNameTable& table = *(g_ticks_saver->thread_duration_map_[*thread_class_].index_name_table);
	if (! table.index_name[index].empty())
		return *this;

	table.index_exist[table.num_index] = index;
	table.index_name[index] = index_name_str;
	ADK_BARRIER();
	++table.num_index;
	return *this;
}

void StopService()
{
	if (g_ticks_saver != NULL)
		g_ticks_saver->Stop();
}

// int32_t PerfRun(BudgetInfo budget)
// {
// 	boost::mutex::scoped_lock lock_guard(mutex_);
// 	if (!is_init_)
// 		g_ticks_saver.Init();

// }

RegisterHelper& RegisterThreadInternal(const std::string& thread_class)
{
	if (g_ticks_saver == NULL)
		g_ticks_saver = new TickSaver();

	return g_ticks_saver->RegisterThread(thread_class);
}

void ThreadOnline(const std::string& thread_class, const std::string& thread_name)
{
	assert(g_ticks_saver != NULL);
	if (g_ticks_saver->ThreadOnline(thread_class, thread_name))
	{
		g_ticks_saver->Start();
		return;
	}
	assert(false);
}

#endif

} // perf
} // adk
