/**
 * MPMC Ring Buffer Test Suite (Simplified)
 *
 * Compile: g++ -std=c++17 -Wall -Wextra -fsanitize=thread -g mpmc_ring_buffer_test.cpp -o
 * mpmc_ring_buffer_test -pthread
 */

#include "mpmc_ring_buffer.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#define TEST(name) void test_##name()
#define RUN_TEST(name)                                                                             \
   do                                                                                              \
   {                                                                                               \
      std::cout << "Running: " << #name << "... ";                                                 \
      test_##name();                                                                               \
      std::cout << "PASSED" << std::endl;                                                          \
   } while (0)

#define ASSERT(cond)                                                                               \
   do                                                                                              \
   {                                                                                               \
      if (!(cond))                                                                                 \
      {                                                                                            \
         std::cerr << "FAILED: " << #cond << std::endl;                                            \
         std::exit(1);                                                                             \
      }                                                                                            \
   } while (0)

// ============================================================================
// Single-threaded tests
// ============================================================================

TEST(empty_buffer)
{
   MPMCRingBuffer<int, 64> buffer;
   ASSERT(buffer.empty());
}

TEST(push_pop)
{
   MPMCRingBuffer<int, 64> buffer;
   ASSERT(buffer.push(42));
   ASSERT(!buffer.empty());
   auto v = buffer.pop();
   ASSERT(v.has_value());
   ASSERT(*v == 42);
}

TEST(multiple_push_pop)
{
   MPMCRingBuffer<int, 64> buffer;
   for (int i = 0; i < 20; ++i)
   {
      int val = i;
      ASSERT(buffer.push(std::move(val)));
   }
   ASSERT(buffer.size() == 20);
   for (int i = 0; i < 20; ++i)
   {
      auto v = buffer.pop();
      ASSERT(v.has_value());
      ASSERT(*v == i);
   }
   ASSERT(buffer.empty());
}

TEST(wrap_around)
{
   MPMCRingBuffer<int, 8> buffer;
   int count = 0;
   while (true)
   {
      int val = count;
      if (!buffer.push(std::move(val)))
         break;
      ++count;
   }

   ASSERT(count == 8);
   int failVal = 999;
   ASSERT(!buffer.push(std::move(failVal))); // Buffer full

   int popped = 0;
   while (buffer.pop().has_value())
      ++popped;
   ASSERT(popped == 8);
   ASSERT(buffer.empty());

   // Fill again
   for (int i = 0; i < 8; ++i)
   {
      int val = i + 100;
      ASSERT(buffer.push(std::move(val)));
   }
   ASSERT(buffer.size() == 8);
}

// ============================================================================
// Multi-threaded tests
// ============================================================================

TEST(multi_producer)
{
   constexpr int PRODUCERS = 4;
   constexpr int PER_PRODUCER = 1000;
   constexpr int TOTAL = PRODUCERS * PER_PRODUCER;

   MPMCRingBuffer<int, 16384> buffer;
   std::atomic<int> pushed{0};
   std::vector<std::thread> threads;

   for (int p = 0; p < PRODUCERS; ++p)
   {
      threads.emplace_back(
          [&buffer, &pushed, p]()
          {
             for (int i = 0; i < PER_PRODUCER; ++i)
             {
                int v = p * PER_PRODUCER + i;
                int val = v;
                while (!buffer.push(std::move(val)))
                {
                   std::this_thread::yield();
                }
                pushed.fetch_add(1);
             }
          });
   }

   for (auto &t : threads)
      t.join();

   std::cout << "\n  Pushed=" << pushed.load();
   ASSERT(pushed.load() == TOTAL);
}

TEST(multi_consumer)
{
   constexpr int TOTAL = 2000;
   constexpr int TOTAL_RUNS = 2;

   for (int run = 0; run < TOTAL_RUNS; ++run)
   {
      MPMCRingBuffer<int, 8192> buffer;
      std::atomic<int> consumed{0};

      // One producer
      std::thread prod(
          [&buffer, TOTAL]()
          {
             for (int i = 0; i < TOTAL; ++i)
             {
                int val = i;
                while (!buffer.push(std::move(val)))
                {
                   std::this_thread::yield();
                }
             }
          });

      // Two consumers
      std::thread cons1(
          [&buffer, &consumed, TOTAL]()
          {
             int count = 0;
             while (count < TOTAL / 2)
             {
                if (buffer.pop().has_value())
                   ++count;
                else
                   std::this_thread::yield();
             }
             consumed.fetch_add(count);
          });

      std::thread cons2(
          [&buffer, &consumed, TOTAL]()
          {
             int count = 0;
             while (count < TOTAL / 2)
             {
                if (buffer.pop().has_value())
                   ++count;
                else
                   std::this_thread::yield();
             }
             consumed.fetch_add(count);
          });

      prod.join();
      cons1.join();
      cons2.join();

      std::cout << "\n  Run " << run << ": Consumed=" << consumed.load();
      ASSERT(consumed.load() == TOTAL);
   }
}

TEST(full_mpmc)
{
   constexpr int PRODS = 2;
   constexpr int PER_PROD = 50;
   constexpr int TOTAL = PRODS * PER_PROD;
   constexpr int TOTAL_RUNS = 2;

   for (int run = 0; run < TOTAL_RUNS; ++run)
   {
      MPMCRingBuffer<int, 4096> buffer;
      std::vector<int> received;
      std::mutex m;
      std::atomic<int> consumed{0};

      // Producers
      std::thread prod1(
          [&buffer]()
          {
             for (int i = 0; i < PER_PROD; ++i)
             {
                int val = i;
                while (!buffer.push(std::move(val)))
                   std::this_thread::yield();
             }
          });

      std::thread prod2(
          [&buffer]()
          {
             for (int i = PER_PROD; i < PER_PROD * 2; ++i)
             {
                int val = i;
                while (!buffer.push(std::move(val)))
                   std::this_thread::yield();
             }
          });

      // Consumers - loop until all items consumed
      std::thread cons1(
          [&buffer, &received, &m, &consumed, TOTAL]()
          {
             while (consumed.load() < TOTAL)
             {
                auto v = buffer.pop();
                if (v.has_value())
                {
                   std::lock_guard<std::mutex> lg(m);
                   received.push_back(*v);
                   consumed.fetch_add(1);
                }
                else
                {
                   std::this_thread::yield();
                }
             }
          });

      std::thread cons2(
          [&buffer, &received, &m, &consumed, TOTAL]()
          {
             while (consumed.load() < TOTAL)
             {
                auto v = buffer.pop();
                if (v.has_value())
                {
                   std::lock_guard<std::mutex> lg(m);
                   received.push_back(*v);
                   consumed.fetch_add(1);
                }
                else
                {
                   std::this_thread::yield();
                }
             }
          });

      prod1.join();
      prod2.join();
      cons1.join();
      cons2.join();

      std::set<int> unique(received.begin(), received.end());
      std::cout << "\n  Run " << run << ": Consumed=" << received.size()
                << " Unique=" << unique.size();
      ASSERT(static_cast<int>(received.size()) == TOTAL);
      ASSERT(static_cast<int>(unique.size()) == TOTAL);
   }
}

// ============================================================================
// Main
// ============================================================================

int main()
{
   std::cout << "===========================================\n";
   std::cout << "   MPMC Ring Buffer Test Suite\n";
   std::cout << "===========================================\n\n";

   std::cout << "Single-threaded tests:\n----------------------------------------\n";
   RUN_TEST(empty_buffer);
   RUN_TEST(push_pop);
   RUN_TEST(multiple_push_pop);
   RUN_TEST(wrap_around);

   std::cout << "\nMulti-threaded tests:\n----------------------------------------\n";
   RUN_TEST(multi_producer);
   RUN_TEST(multi_consumer);
   RUN_TEST(full_mpmc);

   std::cout << "\n===========================================\n";
   std::cout << "               RESULTS\n";
   std::cout << "===========================================\n";
   std::cout << "  All tests PASSED!\n";
   std::cout << "===========================================\n";

   return 0;
}
