#define BOOST_TEST_MODULE singleton_process
#include <boost/test/included/unit_test.hpp>

#include <unistd.h>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <chrono>

#include <boost/property_tree/json_parser.hpp>

#include <adk/monitor/monitor.h>
#include <adk/monitor/indicator_writer.h>

#include <adk/singleton_process.h>

static bool Write(const int fd, const char *data, int len, int seconds)
{
    seconds = seconds*1000;
    int count = seconds/200;
    int write_len = 0;
    while (true)
    {
        int ret = write(fd, data + write_len, len);
        if (ret > 0)
        {
            len -= ret;
            write_len += ret;
        }

        if (len <= 0)
        {
            return true;
        }

        if (count-- <= 0)
        {
            return len <= 0 ? true : false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

static bool Read(const int fd, char *data, int len, int seconds)
{
    seconds = seconds*1000;
    int count = seconds/200;
    int read_len = 0;
    while (true)
    {
        int ret = read(fd, data + read_len, len);
        if (ret > 0)
        {
            len -= ret;
            read_len += ret;
        }

        if (len <= 0)
        {
            return true;
        }

        if (count-- <= 0)
        {
            return len <= 0 ? true : false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

BOOST_AUTO_TEST_CASE(file_lock_test)
{
    system("rm ./singleton_process_test.pid >> /dev/null 2>&1");

    int pipe_fd1[2];
    int pipe_fd2[2];
    BOOST_REQUIRE(pipe2(pipe_fd1, O_NONBLOCK) == 0);
    BOOST_REQUIRE(pipe2(pipe_fd2, O_NONBLOCK) == 0);

    auto s_g = new adk::SingletonProcess("./singleton_process_test");
    BOOST_REQUIRE(s_g->Lock() == adk::ErrorCode::kSuccess);

    auto pid = fork();
    if (pid > 0)
    {
        close(pipe_fd1[1]);
        close(pipe_fd2[0]);

        char flag;
        BOOST_REQUIRE(Read(pipe_fd1[0], &flag, 1, 5));
        BOOST_REQUIRE(flag == 'f');

        sleep(1);
        delete s_g;
        sleep(1);

        char ch = 'c';
        BOOST_REQUIRE(Write(pipe_fd2[1], &ch, 1, 5));

        BOOST_REQUIRE(Read(pipe_fd1[0], &flag, 1, 5));
        BOOST_REQUIRE(flag == 's');
    }
    else if (pid == 0)
    {
        close(pipe_fd1[0]);
        close(pipe_fd2[1]);

        adk::SingletonProcess s_g("./singleton_process_test");
        if (s_g.Lock() == adk::ErrorCode::kFailure)
        {
            char ch = 'f';
            Write(pipe_fd1[1], &ch, 1, 5);
        }
        else 
        {
            char ch = 's';
            Write(pipe_fd1[1], &ch, 1, 5);
        }

        char flag = '0';
        BOOST_REQUIRE(Read(pipe_fd2[0], &flag, 1, 5));
        BOOST_REQUIRE(flag == 'c');
        if (s_g.Lock() == adk::ErrorCode::kFailure)
        {
            char ch = 'f';
            Write(pipe_fd2[1], &ch, 1, 5);
        }
        else 
        {
            char ch = 's';
            Write(pipe_fd1[1], &ch, 1, 5);
        }
    }
    system("rm ./singleton_process_test.pid >> /dev/null 2>&1");
}