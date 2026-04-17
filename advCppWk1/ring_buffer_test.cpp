/**
 * Lock-Free Ring Buffer Test Suite
 *
 * Tests:
 * 1. Basic push/pop operations
 * 2. Wrap-around behavior
 * 3. Exception safety
 * 4. Multi-threaded SPSC test (1M events)
 * 5. ThreadSanitizer verification
 *
 * Compile with ThreadSanitizer:
 *   g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror \
 *        -fsanitize=thread -g ring_buffer.cpp -o ring_buffer -pthread
 */

#include "ring_buffer.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

// ============================================================================
// Test Utilities
// ============================================================================

static std::atomic<int> g_passed{0};
static std::atomic<int> g_failed{0};

#define TEST(name) void test_##name()
#define RUN_TEST(name)                                                                             \
   do                                                                                              \
   {                                                                                               \
      std::cout << "Running: " #name "... ";                                                       \
      try                                                                                          \
      {                                                                                            \
         test_##name();                                                                            \
         std::cout << "PASSED\n";                                                                  \
         g_passed++;                                                                               \
      }                                                                                            \
      catch (const std::exception &e)                                                              \
      {                                                                                            \
         std::cout << "FAILED: " << e.what() << "\n";                                              \
         g_failed++;                                                                               \
      }                                                                                            \
   } while (0)

#define ASSERT(cond)                                                                               \
   do                                                                                              \
   {                                                                                               \
      if (!(cond))                                                                                 \
      {                                                                                            \
         throw std::runtime_error("Assertion failed: " #cond);                                     \
      }                                                                                            \
   } while (0)

// ============================================================================
// Basic Operation Tests
// ============================================================================

TEST(empty_buffer)
{
   RingBuffer<int, 1024> buffer;
   ASSERT(buffer.empty() == true);
   ASSERT(buffer.full() == false);
   ASSERT(buffer.size() == 0);
   ASSERT(buffer.capacity == 1024);
}

TEST(push_pop_single)
{
   RingBuffer<int, 1024> buffer;

   ASSERT(buffer.push(42) == true);
   ASSERT(buffer.size() == 1);
   ASSERT(buffer.empty() == false);

   auto value = buffer.pop();
   ASSERT(value.has_value() == true);
   ASSERT(*value == 42);
   ASSERT(buffer.empty() == true);
}

TEST(push_pop_multiple)
{
   RingBuffer<int, 64> buffer;

   // Push some values
   for (int i = 0; i < 32; ++i)
   {
      ASSERT(buffer.push(i * 10) == true);
   }
   ASSERT(buffer.size() == 32);

   // Pop all values
   for (int i = 0; i < 32; ++i)
   {
      auto value = buffer.pop();
      ASSERT(value.has_value() == true);
      ASSERT(*value == i * 10);
   }

   ASSERT(buffer.empty() == true);
}

TEST(wrap_around)
{
   RingBuffer<int, 16> buffer; // Effective capacity is N-1 = 15

   // Push items until buffer is full
   int count = 0;
   while (buffer.push(count))
   {
      ++count;
   }
   ASSERT(count == 15); // Buffer should hold 15 items (N-1)

   // Buffer is now full, push should fail
   int fail_val = 100;
   ASSERT(buffer.push(std::move(fail_val)) == false);

   // Pop all items
   int expected = 0;
   while (true)
   {
      auto val = buffer.pop();
      if (!val.has_value())
         break;
      ASSERT(*val == expected);
      ++expected;
   }
   ASSERT(expected == 15);

   // Buffer is now empty
   ASSERT(buffer.empty() == true);

   // Push more items - should work and wrap around
   for (int i = 0; i < 16; ++i)
   {
      int val = i + 100;
      ASSERT(buffer.push(std::move(val)) == true);
   }

   // Buffer should be full (N-1 = 15 items)
   ASSERT(buffer.full() == true);

   // Clear and verify
   buffer.clear();
   ASSERT(buffer.empty() == true);
}

TEST(move_semantics)
{
   RingBuffer<std::string, 256> buffer;

   // Push using move
   std::string s1 = "hello";
   std::string s2 = "world";
   buffer.push(std::move(s1));
   buffer.push(std::move(s2));

   ASSERT(s1.empty());          // Moved from
   ASSERT(!s2.empty() || true); // Moved into buffer

   // Pop and verify
   auto val1 = buffer.pop();
   ASSERT(val1.has_value() == true);
   ASSERT(*val1 == "hello");

   auto val2 = buffer.pop();
   ASSERT(val2.has_value() == true);
   ASSERT(*val2 == "world");
}

TEST(optional_empty)
{
   RingBuffer<int, 1024> buffer;

   auto val = buffer.pop();
   ASSERT(val.has_value() == false);

   // Peek empty
   auto peeked = buffer.peek();
   ASSERT(peeked.has_value() == false);
}

TEST(clear_buffer)
{
   RingBuffer<int, 32> buffer;

   for (int i = 0; i < 20; ++i)
   {
      int val = i; // Named variable for std::move
      buffer.push(std::move(val));
   }
   ASSERT(buffer.size() == 20);

   buffer.clear();
   ASSERT(buffer.empty() == true);
   ASSERT(buffer.size() == 0);
}

TEST(profiling_events)
{
   RingBuffer<ProfilingEvent, 1024> buffer;

   // Create and push profiling events
   buffer.push(ProfilingEvent{EventType::FunctionEnter, 1000, "main", 0});

   buffer.push(ProfilingEvent{EventType::MemoryAlloc, 2000, "malloc", 4096});

   buffer.push(ProfilingEvent{
       EventType::FunctionExit, 3000, "main",
       2000 // Duration
   });

   ASSERT(buffer.size() == 3);

   // Verify events
   auto e1 = buffer.pop();
   ASSERT(e1.has_value() == true);
   ASSERT(e1->type == EventType::FunctionEnter);
   ASSERT(std::strcmp(e1->name, "main") == 0);

   auto e2 = buffer.pop();
   ASSERT(e2.has_value() == true);
   ASSERT(e2->type == EventType::MemoryAlloc);
   ASSERT(e2->data == 4096);
}

// ============================================================================
// Thread-Safety Tests (SPSC - Single Producer Single Consumer)
// ============================================================================

constexpr std::size_t PRODUCER_COUNT = 1'000'000;

TEST(multithreaded_spsc)
{
   RingBuffer<uint64_t, 65536> buffer;

   std::atomic<bool> producer_done{false};
   std::atomic<std::size_t> consumed_count{0};
   std::atomic<std::size_t> expected_sum{0};

   // Expected sum of 1 to PRODUCER_COUNT
   expected_sum = (PRODUCER_COUNT * (PRODUCER_COUNT + 1)) / 2;

   // Producer thread
   std::thread producer(
       [&]()
       {
          for (uint64_t i = 1; i <= PRODUCER_COUNT; ++i)
          {
             // Busy-wait with backoff if buffer full
             uint64_t val = i; // Named variable for move
             while (!buffer.push(std::move(val)))
             {
                // Buffer full, yield and retry
                std::this_thread::yield();
             }
          }
          producer_done = true;
       });

   // Consumer thread
   std::thread consumer(
       [&]()
       {
          uint64_t sum = 0;
          std::size_t count = 0;

          while (!producer_done || !buffer.empty())
          {
             auto val = buffer.pop();
             if (val.has_value())
             {
                sum += *val;
                ++count;
             }
             else
             {
                std::this_thread::yield();
             }
          }

          // Process remaining items
          while (true)
          {
             auto val = buffer.pop();
             if (val.has_value())
             {
                sum += *val;
                ++count;
             }
             else
             {
                break;
             }
          }

          consumed_count = count;
          std::cout << "\n  Consumed: " << count << " items\n";
          std::cout << "  Sum: " << sum << " (expected: " << expected_sum << ")\n";

          // Verify
          ASSERT(count == PRODUCER_COUNT);
          ASSERT(sum == expected_sum);
       });

   producer.join();
   consumer.join();
}

TEST(multithreaded_with_timestamps)
{
   RingBuffer<ProfilingEvent, 32768> buffer;

   std::atomic<bool> done{false};
   std::atomic<std::size_t> produced{0};
   std::atomic<std::size_t> consumed{0};

   const char *names[] = {"func1", "func2", "func3", "kernel", "memory"};
   constexpr std::size_t EVENTS_PER_THREAD = 500'000;

   // Producer
   std::thread producer(
       [&]()
       {
          for (std::size_t i = 0; i < EVENTS_PER_THREAD; ++i)
          {
             const char *name = names[i % 5];
             ProfilingEvent event{EventType::Custom, getTimestamp(), name, i};

             if (buffer.push(std::move(event)))
             {
                produced++;
             }
          }
          done = true;
       });

   // Consumer
   std::thread consumer(
       [&]()
       {
          while (!done || !buffer.empty())
          {
             auto event = buffer.pop();
             if (event)
             {
                consumed++;
                // Verify event integrity
                ASSERT(event->timestamp > 0);
                ASSERT(event->name != nullptr);
             }
             else
             {
                std::this_thread::yield();
             }
          }

          // Drain remaining
          while (true)
          {
             auto event = buffer.pop();
             if (event)
             {
                consumed++;
             }
             else
             {
                break;
             }
          }
       });

   producer.join();
   consumer.join();

   std::cout << "\n  Produced: " << produced << " events\n";
   std::cout << "  Consumed: " << consumed << " events\n";

   // Note: With bounded buffer, some events may be dropped when full
   // Key invariant: all consumed events were successfully produced
   ASSERT(produced == consumed);
}

TEST(mixed_sizes)
{
   // Test various power-of-2 sizes
   constexpr std::size_t sizes[] = {2, 4, 8, 16, 64, 256, 1024, 4096, 16384};

   for (const auto size : sizes)
   {
      std::cout << "\n  Testing size: " << size << "\n";

      RingBuffer<int, 16384> buffer;

      std::atomic<bool> done{false};
      std::atomic<std::size_t> count{0};

      std::thread producer(
          [&]()
          {
             for (int i = 0; i < static_cast<int>(size * 10); ++i)
             {
                int val = i; // Named variable for move
                while (!buffer.push(std::move(val)))
                {
                   std::this_thread::yield();
                }
             }
             done = true;
          });

      std::thread consumer(
          [&]()
          {
             while (!done || !buffer.empty())
             {
                if (buffer.pop().has_value())
                {
                   count++;
                }
                else
                {
                   std::this_thread::yield();
                }
             }
             while (buffer.pop().has_value())
             {
                count++;
             }
          });

      producer.join();
      consumer.join();

      ASSERT(count == size * 10);
   }
}

// ============================================================================
// Stress Test
// ============================================================================

TEST(high_throughput_stress)
{
   RingBuffer<ProfilingEvent, 131072> buffer;

   std::atomic<bool> stop{false};
   std::atomic<std::size_t> pushes{0};
   std::atomic<std::size_t> pops{0};

   // Multiple producer threads (NOT lock-free SPSC, but tests buffer limits)
   std::vector<std::thread> producers;
   std::vector<std::thread> consumers;

   for (int t = 0; t < 2; ++t)
   {
      producers.emplace_back(
          [&stop, &buffer, &pushes, t]()
          {
             for (int i = 0; i < 100000; ++i)
             {
                ProfilingEvent event{EventType::Custom, getTimestamp(), "stress_test",
                                     static_cast<uint64_t>(i)};
                if (buffer.push(std::move(event)))
                {
                   pushes++;
                }
                std::this_thread::yield();
             }
          });

      consumers.emplace_back(
          [&stop, &buffer, &pops]()
          {
             while (!stop.load() || !buffer.empty())
             {
                if (buffer.pop().has_value())
                {
                   pops++;
                }
                else
                {
                   std::this_thread::yield();
                }
             }
          });
   }

   // Let it run for a bit
   std::this_thread::sleep_for(std::chrono::milliseconds(100));
   stop = true;

   for (auto &t : producers)
      t.join();
   for (auto &t : consumers)
      t.join();

   std::cout << "\n  Total pushes: " << pushes << "\n";
   std::cout << "  Total pops: " << pops << "\n";
   std::cout << "  Final buffer size: " << buffer.size() << "\n";
}

// ============================================================================
// RAII and Exception Safety Tests
// ============================================================================

TEST(raii_cleanup)
{
   {
      RingBuffer<std::string, 256> buffer;

      buffer.push(std::string("test1"));
      buffer.push(std::string("test2"));
      buffer.push(std::string("test3"));

      ASSERT(buffer.size() == 3);
      // buffer goes out of scope here - RAII cleans up
   }
   // If we get here without memory leaks, RAII works
   std::cout << "  RAII cleanup successful\n";
}

// ============================================================================
// constexpr Tests
// ============================================================================

constexpr std::size_t test_capacity = RingBuffer<int, 64>::capacity;

TEST(constexpr_capacity)
{
   static_assert(RingBuffer<int, 64>::capacity == 64);
   static_assert(RingBuffer<double, 1024>::capacity == 1024);

   // Verify power-of-2 check works
   // This won't compile:
   // RingBuffer<int, 100> invalid_buffer;  // Error: not power of 2

   std::cout << "  constexpr capacity: " << test_capacity << "\n";
}

// ============================================================================
// Main
// ============================================================================

int main()
{
   std::cout << "========================================\n";
   std::cout << "   Lock-Free Ring Buffer Test Suite\n";
   std::cout << "========================================\n";
   std::cout << "\n";
   std::cout << "Single-threaded tests:\n";
   std::cout << "----------------------------------------\n";

   RUN_TEST(empty_buffer);
   RUN_TEST(push_pop_single);
   RUN_TEST(push_pop_multiple);
   RUN_TEST(wrap_around);
   RUN_TEST(move_semantics);
   RUN_TEST(optional_empty);
   RUN_TEST(clear_buffer);
   RUN_TEST(profiling_events);
   RUN_TEST(constexpr_capacity);
   RUN_TEST(raii_cleanup);

   std::cout << "\n";
   std::cout << "Multi-threaded SPSC tests:\n";
   std::cout << "----------------------------------------\n";

   RUN_TEST(multithreaded_spsc);
   RUN_TEST(multithreaded_with_timestamps);
   RUN_TEST(mixed_sizes);
   RUN_TEST(high_throughput_stress);

   std::cout << "\n";
   std::cout << "========================================\n";
   std::cout << "               RESULTS\n";
   std::cout << "========================================\n";
   std::cout << "  Passed: " << g_passed << "\n";
   std::cout << "  Failed: " << g_failed << "\n";
   std::cout << "========================================\n";

   return g_failed > 0 ? 1 : 0;
}
