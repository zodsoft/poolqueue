#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Delay

#include <algorithm>
#include <atomic>
#include <thread>
#include <boost/test/unit_test.hpp>

#include "Delay.hpp"

using poolqueue::Delay;
using poolqueue::Promise;

BOOST_AUTO_TEST_CASE(basic) {
   std::vector<int> v = { 30, 20, -10, 0, 30, 50, 25, 40, 20, 20 };

   std::mutex m;
   std::vector<int> results;
   std::atomic<size_t> count(v.size());
   const auto start = std::chrono::steady_clock::now();
   for (int t : v) {
      Delay::create(std::chrono::milliseconds(t))
         .then([&, t]() {
               const auto elapsed = std::chrono::steady_clock::now() - start;
               std::lock_guard<std::mutex> lock(m);
               BOOST_CHECK_GE(
                  elapsed.count(),
                  std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::milliseconds(t)).count());
               results.push_back(t);
               --count;
            });
   }

   while (count)
      std::this_thread::yield();
   
   BOOST_CHECK_EQUAL(results.size(), v.size());
   BOOST_WARN(std::is_sorted(results.begin(), results.end()));
}

BOOST_AUTO_TEST_CASE(cancel) {
   std::vector<Promise> v(6);
   std::vector<int> results;
   std::atomic<size_t> count(v.size());
   int exceptCount = 0;
   for (size_t i = 0; i < v.size(); ++i) {
      auto p = Delay::create(std::chrono::milliseconds(500 + i));
      p.then([&, i]() {
            results.push_back(i);
            --count;
         })
         .except([&](const std::exception_ptr& e) {
               try {
                  if (e)
                     std::rethrow_exception(e);
               }
               catch (const std::runtime_error& e) {
                  ++exceptCount;
                  BOOST_CHECK_EQUAL(e.what(), std::string());
               }
               catch (...) {
                  BOOST_CHECK(false);
               }

               --count;
            });
      
      v[i].swap(p);
   }

   Delay::cancel(v[1]);
   Delay::cancel(v[2], std::make_exception_ptr(std::runtime_error("")));
   Delay::cancel(v[4]);

   while (count)
      std::this_thread::yield();

   BOOST_CHECK_EQUAL(results.size(), v.size() - 3);
   BOOST_CHECK_EQUAL(std::count(results.begin(), results.end(), 1), 0);
   BOOST_CHECK_EQUAL(std::count(results.begin(), results.end(), 2), 0);
   BOOST_CHECK_EQUAL(std::count(results.begin(), results.end(), 4), 0);
   BOOST_CHECK_EQUAL(exceptCount, 1);
}
