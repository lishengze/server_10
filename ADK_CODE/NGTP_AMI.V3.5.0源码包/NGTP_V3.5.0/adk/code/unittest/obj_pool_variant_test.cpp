#define BOOST_TEST_MODULE util
#include <boost/test/included/unit_test.hpp>

#include <adk/obj_pool_variant.h>
#include <thread>

BOOST_AUTO_TEST_SUITE(SingleThread)
BOOST_AUTO_TEST_CASE(basic) {
  // create a pool
  auto *pool = adk::variant::ObjPool<int>::Create();
  int *a = pool->New();
  *a = 1;
  pool->Delete(a);

  for (int i = 0; i < 1000000; ++i) {
    auto *ptr = pool->New();
    *ptr = i;
    adk::variant::ObjPool<int>::UniformDelete(ptr);
  }
}

BOOST_AUTO_TEST_CASE(hold) {
  auto *pool = adk::variant::ObjPool<std::string>::Create();
  std::vector<std::string *> storage;

  for (int i = 0; i < 1000000; ++i) {
    auto *ptr = pool->New();
    *ptr = std::to_string(i) + "multi-threading";
    storage.push_back(ptr);
  }

  // release
  for (auto p : storage) {
    adk::variant::Delete(p);
  }
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(MultiThread)
BOOST_AUTO_TEST_CASE(basic) {
  auto *pool = adk::variant::ObjPool<int>::Create();

  std::vector<std::thread> vec;
  vec.resize(10);
  for (int i = 0; i < 10; ++i) {
    vec[i] = std::thread([&]() {
      for (int j = 0; j < 100000; ++j) {
        auto *ptr = pool->New();
        *ptr = j;
        pool->Delete(ptr);
      }
    });
  }

  for (auto &t : vec) {
    t.join();
  }
}

BOOST_AUTO_TEST_CASE(hold) {
  auto *pool = adk::variant::ObjPool<std::string>::Create();

  std::vector<std::thread> vec;
  vec.resize(10);
  for (int i = 0; i < 10; ++i) {
    vec[i] = std::thread([&]() {
      std::vector<std::string *> storage;
      for (int j = 0; j < 100000; ++j) {
        auto *ptr = pool->New();
        *ptr = std::to_string(j) + "multi-threading";
        storage.push_back(ptr);
      }
      for (auto p : storage) {
        adk::variant::Delete(p);
      }
    });
  }

  for (auto &t : vec) {
    t.join();
  }
}
BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(MultiPool)
BOOST_AUTO_TEST_CASE(basic) {
  auto *pool_string = adk::variant::ObjPool<std::string>::Create();
  auto *pool_int = adk::variant::ObjPool<int>::Create();
  auto *pool_double = adk::variant::ObjPool<double>::Create();

  std::vector<std::thread> vec;
  vec.resize(10);
  for (int i = 0; i < 10; ++i) {
    vec[i] = std::thread([&]() {
      std::vector<std::string *> storage;
      for (int j = 0; j < 100000; ++j) {
        auto *ptr = pool_string->New();
        *ptr = std::to_string(j) + "multi-threading";
        storage.push_back(ptr);
      }

      for (int j = 0; j < 100000; ++j) {
        auto *ptr = pool_int->New();
        *ptr = j;
        pool_int->Delete(ptr);
      }

      for (int j = 0; j < 100000; ++j) {
        auto *ptr = pool_double->New();
        *ptr = 1.0 * j;
        pool_double->Delete(ptr);
      }

      for (auto p : storage) {
        adk::variant::Delete(p);
      }
    });
  }

  for (auto &t : vec) {
    t.join();
  }
}
BOOST_AUTO_TEST_SUITE_END()