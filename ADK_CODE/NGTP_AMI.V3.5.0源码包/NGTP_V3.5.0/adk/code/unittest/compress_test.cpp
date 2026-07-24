#define BOOST_TEST_MODULE compress
#include <boost/test/included/unit_test.hpp>
#include <stdlib.h>
#include <time.h>
#include <vector>
#include <string>
#include <exception>
#include <adk/compress.h>

BOOST_AUTO_TEST_CASE(test_compress)
{
    using namespace adk;    
    const int test_size = 1024*32; 
    std::vector<unsigned char> src_vec;
    std::vector<unsigned char> com_vec;
    std::vector<unsigned char> ret_vec;
    std::string src_str;
    std::string com_str;
    std::string ret_str;

    srand(time(0));
    for (int i = 0; i < test_size; ++i)
    {
        unsigned char ch = rand() % 256;
        src_vec.push_back(ch);
        src_str.push_back(ch);
    }

    Compression *zlib_compress_ptr = nullptr;
    Compression *gzip_compress_ptr = nullptr;
    BOOST_REQUIRE_NO_THROW(zlib_compress_ptr = new Compression(Compression::kMethodDeflate, Compression::kBoth));
    BOOST_REQUIRE_NO_THROW(gzip_compress_ptr = new Compression(Compression::kMethodGzip, Compression::kBoth));
    BOOST_REQUIRE(zlib_compress_ptr->Compress(&src_vec[0], src_vec.size(), com_vec) == 0);
    BOOST_REQUIRE(zlib_compress_ptr->Decompress(&com_vec[0], com_vec.size(), ret_vec) == 0);
    BOOST_REQUIRE(src_vec == ret_vec);
    BOOST_REQUIRE(zlib_compress_ptr->Compress((unsigned char*)&src_str[0], src_str.size(), com_str) == 0);
    BOOST_REQUIRE(zlib_compress_ptr->Decompress((unsigned char*)&com_str[0], com_str.size(), ret_str) == 0);
    BOOST_REQUIRE(src_str == ret_str);

    com_vec.clear();
    ret_vec.clear();
    com_str.clear();
    ret_str.clear();
    BOOST_REQUIRE(gzip_compress_ptr->Compress(&src_vec[0], src_vec.size(), com_vec) == 0);
    BOOST_REQUIRE(gzip_compress_ptr->Decompress(&com_vec[0], com_vec.size(), ret_vec) == 0);
    BOOST_REQUIRE(src_vec == ret_vec);
    BOOST_REQUIRE(gzip_compress_ptr->Compress((unsigned char*)&src_str[0], src_str.size(), com_str) == 0);
    BOOST_REQUIRE(gzip_compress_ptr->Decompress((unsigned char*)&com_str[0], com_str.size(), ret_str) == 0);
    BOOST_REQUIRE(src_str == ret_str);

    BOOST_REQUIRE_THROW(Compression(Compression::kMethodDeflate, Compression::kBoth, 10), std::runtime_error);
    BOOST_REQUIRE_THROW(Compression(Compression::kMethodDeflate, Compression::kBoth, -2), std::runtime_error);
}