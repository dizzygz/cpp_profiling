/**
 * Complete RAII Wrapper Examples for C-Style Resources
 *
 * Demonstrates two modern C++ approaches for safely wrapping C APIs:
 * 1. std::unique_ptr with custom deleter
 * 2. Custom RAII class (Rule of Five)
 *
 * Compile: g++ -std=c++17 -fsanitize=address -g raii_wrappers.cpp -o raii_wrappers
 */

#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// ============================================================================
// MOCK C API (Replace with actual HSA/CUDA/OpenCL headers)
// ============================================================================

typedef struct
{
   uint64_t handle;
} hsa_agent_t;
typedef struct
{
   uint64_t handle;
} hsa_signal_t;
typedef int hsa_status_t;

// Status codes
#define HSA_STATUS_SUCCESS 0
#define HSA_STATUS_ERROR 1

// C API functions (mock implementations)
extern "C"
{
   hsa_status_t hsa_agent_start(hsa_agent_t agent)
   {
      std::cout << "  [C API] hsa_agent_start(handle=" << agent.handle << ")\n";
      return HSA_STATUS_SUCCESS;
   }

   hsa_status_t hsa_agent_stop(hsa_agent_t agent)
   {
      std::cout << "  [C API] hsa_agent_stop(handle=" << agent.handle << ")\n";
      return HSA_STATUS_SUCCESS;
   }

   hsa_status_t hsa_signal_create(int64_t initial_value, int32_t num_consumers, const void *agents,
                                  hsa_signal_t *signal)
   {
      std::cout << "  [C API] hsa_signal_create(initial_value=" << initial_value << ")\n";
      signal->handle = 0xDEADBEEF; // Mock handle
      return HSA_STATUS_SUCCESS;
   }

   hsa_status_t hsa_signal_destroy(hsa_signal_t signal)
   {
      std::cout << "  [C API] hsa_signal_destroy(handle=" << signal.handle << ")\n";
      return HSA_STATUS_SUCCESS;
   }

   hsa_status_t hsa_region_allocate(hsa_agent_t agent, size_t size, void **ptr)
   {
      std::cout << "  [C API] hsa_region_allocate(size=" << size << ")\n";
      *ptr = malloc(size);
      return HSA_STATUS_SUCCESS;
   }

   void hsa_memory_free(void *ptr)
   {
      std::cout << "  [C API] hsa_memory_free(ptr=" << ptr << ")\n";
      free(ptr);
   }
}

// ============================================================================
// APPROACH 1: std::unique_ptr with Custom Deleter
// ============================================================================

namespace UniquePtrApproach
{

// --------------------------------------------------------------------------
// Example 1a: Simple struct handle (heap-allocated)
// --------------------------------------------------------------------------

struct AgentDeleter
{
   void operator()(hsa_agent_t *agent) const
   {
      if (agent)
      {
         std::cout << "  [AgentDeleter] Calling hsa_agent_stop()\n";
         hsa_agent_stop(*agent);
         delete agent;
      }
   }
};

using ScopedAgent = std::unique_ptr<hsa_agent_t, AgentDeleter>;

ScopedAgent make_scoped_agent(hsa_agent_t raw_agent)
{
   hsa_agent_start(raw_agent);
   return ScopedAgent(new hsa_agent_t(raw_agent));
}

// --------------------------------------------------------------------------
// Example 1b: Signal handle (value semantics with unique_ptr)
// --------------------------------------------------------------------------

struct SignalDeleter
{
   void operator()(hsa_signal_t *signal) const
   {
      if (signal)
      {
         std::cout << "  [SignalDeleter] Calling hsa_signal_destroy()\n";
         hsa_signal_destroy(*signal);
         delete signal;
      }
   }
};

using ScopedSignal = std::unique_ptr<hsa_signal_t, SignalDeleter>;

ScopedSignal make_scoped_signal(int64_t initial_value = 0)
{
   hsa_signal_t signal;
   if (hsa_signal_create(initial_value, 0, nullptr, &signal) != HSA_STATUS_SUCCESS)
   {
      throw std::runtime_error("Failed to create HSA signal");
   }
   return ScopedSignal(new hsa_signal_t(signal));
}

// --------------------------------------------------------------------------
// Example 1c: Memory allocation with custom deleter
// --------------------------------------------------------------------------

struct MemoryDeleter
{
   void operator()(void *ptr) const
   {
      if (ptr)
      {
         std::cout << "  [MemoryDeleter] Calling hsa_memory_free()\n";
         hsa_memory_free(ptr);
      }
   }
};

using ScopedMemory = std::unique_ptr<void, MemoryDeleter>;

ScopedMemory allocate_scoped_memory(hsa_agent_t agent, size_t size)
{
   void *ptr = nullptr;
   hsa_status_t status = hsa_region_allocate(agent, size, &ptr);
   if (status != HSA_STATUS_SUCCESS || !ptr)
   {
      throw std::runtime_error("Failed to allocate HSA memory");
   }
   return ScopedMemory(ptr);
}

// --------------------------------------------------------------------------
// Example 1d: Type-erased deleter using std::function
// --------------------------------------------------------------------------

using GenericResource = std::unique_ptr<void, std::function<void(void *)>>;

template <typename T, typename Deleter> GenericResource make_generic_resource(T *ptr, Deleter d)
{
   return GenericResource(ptr, d);
}

} // namespace UniquePtrApproach

// ============================================================================
// APPROACH 2: Custom RAII Class (Rule of Five)
// ============================================================================

namespace RAIIClassApproach
{

// --------------------------------------------------------------------------
// Example 2a: ProfilerGuard - RAII for profiling state
// --------------------------------------------------------------------------

class ProfilerGuard
{
 private:
   hsa_agent_t agent_;
   bool active_;
   std::string name_;

 public:
   // Constructor: Start the profiler
   explicit ProfilerGuard(hsa_agent_t agent, const std::string &name = "unnamed")
       : agent_(agent), active_(false), name_(name)
   {

      hsa_status_t status = hsa_agent_start(agent_);
      if (status != HSA_STATUS_SUCCESS)
      {
         throw std::runtime_error("Failed to start profiler for agent " + name_);
      }
      std::cout << "  [RAII ProfilerGuard] Started: " << name_ << "\n";
      active_ = true;
   }

   // Destructor: Automatic cleanup
   ~ProfilerGuard()
   {
      if (active_)
      {
         std::cout << "  [RAII ProfilerGuard] Stopping: " << name_ << "\n";
         hsa_agent_stop(agent_);
      }
   }

   // Copy disabled (move-only resource)
   ProfilerGuard(const ProfilerGuard &) = delete;
   ProfilerGuard &operator=(const ProfilerGuard &) = delete;

   // Move constructor
   ProfilerGuard(ProfilerGuard &&other) noexcept
       : agent_(other.agent_), active_(other.active_), name_(std::move(other.name_))
   {
      other.active_ = false; // Transfer ownership
   }

   // Move assignment
   ProfilerGuard &operator=(ProfilerGuard &&other) noexcept
   {
      if (this != &other)
      {
         if (active_)
         {
            hsa_agent_stop(agent_); // Release current resource
         }
         agent_ = other.agent_;
         active_ = other.active_;
         name_ = std::move(other.name_);
         other.active_ = false;
      }
      return *this;
   }

   // Accessors
   hsa_agent_t get() const
   {
      return agent_;
   }
   bool isActive() const
   {
      return active_;
   }
   const std::string &name() const
   {
      return name_;
   }

   void pause()
   {
      if (active_)
      {
         hsa_agent_stop(agent_);
         active_ = false;
         std::cout << "  [RAII ProfilerGuard] Paused: " << name_ << "\n";
      }
   }

   void resume()
   {
      if (!active_)
      {
         hsa_status_t status = hsa_agent_start(agent_);
         if (status == HSA_STATUS_SUCCESS)
         {
            active_ = true;
            std::cout << "  [RAII ProfilerGuard] Resumed: " << name_ << "\n";
         }
      }
   }
};

// --------------------------------------------------------------------------
// Example 2b: DeviceBuffer - RAII for GPU memory
// --------------------------------------------------------------------------

class DeviceBuffer
{
 private:
   hsa_agent_t agent_;
   void *data_;
   size_t size_;
   bool owner_;

 public:
   DeviceBuffer(hsa_agent_t agent, size_t size)
       : agent_(agent), data_(nullptr), size_(size), owner_(true)
   {

      hsa_status_t status = hsa_region_allocate(agent_, size_, &data_);
      if (status != HSA_STATUS_SUCCESS || !data_)
      {
         throw std::runtime_error("Failed to allocate device buffer of size " +
                                  std::to_string(size));
      }
      std::cout << "  [RAII DeviceBuffer] Allocated: " << size_ << " bytes at " << data_ << "\n";
   }

   ~DeviceBuffer()
   {
      if (data_ && owner_)
      {
         std::cout << "  [RAII DeviceBuffer] Freeing: " << size_ << " bytes at " << data_ << "\n";
         hsa_memory_free(data_);
      }
   }

   // Disable copy (can't copy GPU memory handles)
   DeviceBuffer(const DeviceBuffer &) = delete;
   DeviceBuffer &operator=(const DeviceBuffer &) = delete;

   // Move semantics
   DeviceBuffer(DeviceBuffer &&other) noexcept
       : agent_(other.agent_), data_(other.data_), size_(other.size_), owner_(other.owner_)
   {
      other.data_ = nullptr;
      other.owner_ = false;
      std::cout << "  [RAII DeviceBuffer] Moved to: " << data_ << "\n";
   }

   DeviceBuffer &operator=(DeviceBuffer &&other) noexcept
   {
      if (this != &other)
      {
         if (data_ && owner_)
         {
            hsa_memory_free(data_);
         }
         agent_ = other.agent_;
         data_ = other.data_;
         size_ = other.size_;
         owner_ = other.owner_;
         other.data_ = nullptr;
         other.owner_ = false;
      }
      return *this;
   }

   // Accessors
   void *data() const
   {
      return data_;
   }
   size_t size() const
   {
      return size_;
   }
   explicit operator bool() const
   {
      return data_ != nullptr;
   }
};

// --------------------------------------------------------------------------
// Example 2c: ScopedTransaction - RAII for atomic operations
// --------------------------------------------------------------------------

class ScopedTransaction
{
 private:
   bool committed_;
   std::vector<int64_t> *log_;

 public:
   ScopedTransaction() : committed_(false), log_(nullptr)
   {
      std::cout << "  [RAII ScopedTransaction] Started\n";
   }

   ~ScopedTransaction()
   {
      if (!committed_)
      {
         std::cout << "  [RAII ScopedTransaction] Rolled back\n";
      }
   }

   void commit()
   {
      committed_ = true;
      std::cout << "  [RAII ScopedTransaction] Committed\n";
   }

   bool isCommitted() const
   {
      return committed_;
   }
};

// --------------------------------------------------------------------------
// Example 2d: ScopedLock - RAII for synchronization primitives
// --------------------------------------------------------------------------

class ScopedLock
{
 private:
   bool locked_;
   uint32_t *mutex_;

 public:
   explicit ScopedLock(uint32_t *mutex) : locked_(true), mutex_(mutex)
   {
      std::cout << "  [RAII ScopedLock] Lock acquired\n";
   }

   ~ScopedLock()
   {
      if (locked_)
      {
         std::cout << "  [RAII ScopedLock] Lock released\n";
      }
   }

   ScopedLock(const ScopedLock &) = delete;
   ScopedLock &operator=(const ScopedLock &) = delete;

   void unlock()
   {
      locked_ = false;
      std::cout << "  [RAII ScopedLock] Unlocked early\n";
   }
};

// --------------------------------------------------------------------------
// Example 2e: FileHandle - RAII for FILE* (C standard library)
// --------------------------------------------------------------------------

class FileHandle
{
 private:
   FILE *file_;
   std::string mode_;

 public:
   explicit FileHandle(const std::string &filename, const std::string &mode = "r")
       : file_(nullptr), mode_(mode)
   {

      file_ = fopen(filename.c_str(), mode.c_str());
      if (!file_)
      {
         throw std::runtime_error("Failed to open file: " + filename);
      }
      std::cout << "  [RAII FileHandle] Opened: " << filename << "\n";
   }

   ~FileHandle()
   {
      if (file_)
      {
         std::cout << "  [RAII FileHandle] Closing file\n";
         fclose(file_);
      }
   }

   // Delete copy (FILE* can't be safely duplicated)
   FileHandle(const FileHandle &) = delete;
   FileHandle &operator=(const FileHandle &) = delete;

   // Allow move
   FileHandle(FileHandle &&other) noexcept : file_(other.file_), mode_(std::move(other.mode_))
   {
      other.file_ = nullptr;
   }

   FileHandle &operator=(FileHandle &&other) noexcept
   {
      if (this != &other)
      {
         if (file_)
            fclose(file_);
         file_ = other.file_;
         mode_ = std::move(other.mode_);
         other.file_ = nullptr;
      }
      return *this;
   }

   // Accessors
   FILE *get() const
   {
      return file_;
   }
   explicit operator bool() const
   {
      return file_ != nullptr;
   }

   void write(const std::string &data)
   {
      if (file_)
      {
         fputs(data.c_str(), file_);
      }
   }
};

} // namespace RAIIClassApproach

// ============================================================================
// TEST FUNCTIONS - Demonstrate exception safety and cleanup
// ============================================================================

void test_exception_safety()
{
   using namespace UniquePtrApproach;

   std::cout << "\n";
   std::cout << "========================================\n";
   std::cout << "   Test: Exception Safety\n";
   std::cout << "========================================\n\n";

   hsa_agent_t agent{123};

   // This will properly clean up even if exception is thrown
   auto scoped_agent = make_scoped_agent(agent);

   try
   {
      // Simulate operation that might throw
      throw std::runtime_error("Simulated error in critical section");
   }
   catch (const std::runtime_error &e)
   {
      std::cout << "  Caught exception: " << e.what() << "\n";
      std::cout << "  (Agent cleanup happens automatically)\n";
   }

   // scoped_agent goes out of scope here - proper cleanup guaranteed
}

void test_early_return()
{
   using namespace UniquePtrApproach;

   std::cout << "\n";
   std::cout << "========================================\n";
   std::cout << "   Test: Early Return Safety\n";
   std::cout << "========================================\n\n";

   hsa_agent_t agent{456};
   auto scoped_agent = make_scoped_agent(agent);

   bool condition = true;

   if (condition)
   {
      std::cout << "  Early return triggered\n";
      return; // Agent cleanup happens here automatically
   }

   std::cout << "  Normal execution path\n";
}

void test_nested_scopes()
{
   using namespace RAIIClassApproach;

   std::cout << "\n";
   std::cout << "========================================\n";
   std::cout << "   Test: Nested RAII Scopes\n";
   std::cout << "========================================\n\n";

   hsa_agent_t gpu0{100}, gpu1{200};

   {
      ProfilerGuard outer(gpu0, "GPU0_Profiler");

      {
         ProfilerGuard inner(gpu1, "GPU1_Profiler");
         // Inner profiler active
         inner.pause();

         {
            ProfilerGuard deep(gpu0, "Deep_Profiler");
            // All three active
         } // deep destroyed

      } // inner destroyed

   } // outer destroyed
}

void test_memory_allocation()
{
   using namespace UniquePtrApproach;

   std::cout << "\n";
   std::cout << "========================================\n";
   std::cout << "   Test: Memory Allocation\n";
   std::cout << "========================================\n\n";

   hsa_agent_t agent{789};

   {
      auto buffer = allocate_scoped_memory(agent, 1024);
      std::cout << "  Buffer allocated at: " << buffer.get() << "\n";

      if (!buffer)
      {
         throw std::runtime_error("Allocation failed");
      }

      // Work with buffer...
      std::cout << "  Work completed\n";

   } // buffer freed automatically
}

void test_gpu_memory_class()
{
   using namespace RAIIClassApproach;

   std::cout << "\n";
   std::cout << "========================================\n";
   std::cout << "   Test: DeviceBuffer Class\n";
   std::cout << "========================================\n\n";

   hsa_agent_t agent{999};

   {
      DeviceBuffer buf1(agent, 2048);
      DeviceBuffer buf2(agent, 4096);

      // Move semantics
      DeviceBuffer buf3 = std::move(buf1);
      // buf1 is now empty

      std::cout << "  buf2 size: " << buf2.size() << "\n";
      std::cout << "  buf3 moved from buf1\n";

   } // Only buf2 and buf3 freed (buf1 was moved)
}

void test_transaction_semantics()
{
   using namespace RAIIClassApproach;

   std::cout << "\n";
   std::cout << "========================================\n";
   std::cout << "   Test: Transaction Semantics\n";
   std::cout << "========================================\n\n";

   {
      ScopedTransaction tx;

      // Do some work...
      std::cout << "  Performing transaction work...\n";

      bool success = false;

      if (success)
      {
         tx.commit();
      }
      // If not committed, destructor will rollback

   } // Transaction resolved (committed or rolled back)
}

void test_mixed_approaches()
{
   std::cout << "\n";
   std::cout << "========================================\n";
   std::cout << "   Test: Mixed Approaches\n";
   std::cout << "========================================\n\n";

   using namespace UniquePtrApproach;
   using namespace RAIIClassApproach;

   hsa_agent_t agent{1000};

   // Using unique_ptr for simple handles
   auto signal = make_scoped_signal(42);
   std::cout << "  Signal handle: " << signal->handle << "\n";

   // Using RAII class for complex state
   {
      ProfilerGuard profiler(agent, "Complex_Profiler");
      DeviceBuffer buffer(agent, 8192);

      std::cout << "  Mixed usage of both patterns\n";

   } // Both cleaned up in reverse order

   std::cout << "  signal still valid: " << (signal ? "yes" : "no") << "\n";
}

// ============================================================================
// MAIN
// ============================================================================

int main()
{
   std::cout << "========================================\n";
   std::cout << "   C++ RAII Wrapper Examples for C APIs\n";
   std::cout << "========================================\n";

   // Run all tests
   test_exception_safety();
   test_early_return();
   test_nested_scopes();
   test_memory_allocation();
   test_gpu_memory_class();
   test_transaction_semantics();
   test_mixed_approaches();

   std::cout << "\n";
   std::cout << "========================================\n";
   std::cout << "               SUMMARY\n";
   std::cout << "========================================\n";
   std::cout << "\n";
   std::cout << "APPROACH 1: std::unique_ptr + Custom Deleter\n";
   std::cout << "  Pros:\n";
   std::cout << "    - Lightweight, no heap allocation for simple structs\n";
   std::cout << "    - Type-safe and move-aware\n";
   std::cout << "    - Works with std::function for type erasure\n";
   std::cout << "  Best for:\n";
   std::cout << "    - Pointer-type handles (FILE*, HANDLE, pointers)\n";
   std::cout << "    - When you want minimal boilerplate\n";
   std::cout << "\n";
   std::cout << "APPROACH 2: Custom RAII Class (Rule of Five)\n";
   std::cout << "  Pros:\n";
   std::cout << "    - Can hold additional state and logic\n";
   std::cout << "    - Clear semantics with named methods\n";
   std::cout << "    - Better for complex resources\n";
   std::cout << "  Best for:\n";
   std::cout << "    - Resources needing setup/teardown logic\n";
   std::cout << "    - When you need pause/resume or transaction semantics\n";
   std::cout << "\n";
   std::cout << "KEY PRINCIPLES:\n";
   std::cout << "  1. Exception safety - cleanup happens automatically\n";
   std::cout << "  2. Early returns - resource released at scope end\n";
   std::cout << "  3. Move semantics - prevents double-free bugs\n";
   std::cout << "  4. RAII - constructor acquires, destructor releases\n";
   std::cout << "========================================\n";

   return 0;
}
