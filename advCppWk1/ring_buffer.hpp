#ifndef RING_BUFFER_HPP
#define RING_BUFFER_HPP

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

/**
 * Lock-Free Single-Producer Single-Consumer Ring Buffer
 *
 * Template Parameters:
 *   T     - Element type (must be movable)
 *   N     - Buffer capacity (must be a power of 2)
 *
 * Notes:
 *   - Uses acquire/release synchronization between the producer and consumer.
 *   - Stores elements in std::optional<T> slots so slot lifetime is explicit.
 *   - Supports the full N elements because head/tail are monotonic counters.
 */
/**
 *  What actually happens with std::optional<T> slots[N]
1️⃣ Memory for all slots is allocated first
✅ Correct
C++std::optional<T> slots[N];Show more lines
This allocates one fixed block of memory large enough for:

N optionals
each optional contains:

raw, properly aligned storage for T
a small “engaged” flag (usually a bool)



👉 No T objects are constructed at this point.
Only storage exists.

2️⃣ emplace() constructs the object at runtime
✅ Correct
C++slots[i].emplace(args...);Show more lines
What happens internally:
C++new (&storage) T(args...);  // placement-newengaged = true;``Show more lines
✅ The T object is constructed in-place
✅ No heap allocation
✅ No copying of other slots
✅ No resizing
✅ Happens exactly when you call emplace()

🔍 Important clarification: “runtime” vs “dynamic allocation”
When we say “constructed at runtime”, that does not mean:
❌ allocated on the heap
❌ new memory block created
It only means:
✅ constructor runs when the program is executing
✅ object lifetime begins at that moment
The memory was already there.

3️⃣ reset() destroys the object (but keeps the memory)
C++slots[i].reset();``Show more lines
Does:
C++if (engaged) {    stored_T.~T();    engaged = false;}Show more lines
✅ Destructor runs
✅ Slot becomes empty
✅ Storage remains reusable

✅ One‑line mental model (keep this)

Storage is fixed and allocated once; object lifetime is started and ended explicitly with emplace()
/ reset().


✅ Comparison with other containers (for intuition)


ContainerMemory allocationObject lifetimeT slots[N]onceall objects always
alivestd::vector<T>growsobjects move / reallocatestd::optional<T> slots[N]onceper‑slot,
explicitplacement newmanualerror‑pronestd::optionaloncesafe, explicit

✅ Why this is ideal for ring buffers

Fixed capacity
No heap
Predictable memory layout
Correct destructor timing
No UB
Clear “empty vs full” semantics

That’s why your earlier sentence was accurate:

“Stores elements in std::optional<T> slots so slot lifetime is explicit.”
*/

template <typename T, std::size_t N> class RingBuffer
{
   static_assert(N > 0, "Capacity must be greater than 0");
   static_assert((N & (N - 1)) == 0, "Capacity must be a power of 2");
   static_assert(std::is_move_constructible_v<T>, "T must be move constructible");
   static_assert(std::is_move_assignable_v<T>, "T must be move assignable");

 public:
   using value_type = T;
   using size_type = std::size_t;
   using difference_type = std::ptrdiff_t;
   using reference = T &;
   using const_reference = const T &;
   using pointer = T *;
   using const_pointer = const T *;

   static constexpr size_type capacity = N;

 private:
   static constexpr std::size_t cache_line_size = 64;
   static constexpr size_type mask = N - 1;

   std::array<std::optional<T>, N> data_{};

   alignas(cache_line_size) std::atomic<size_type> head_{0};
   alignas(cache_line_size) std::atomic<size_type> tail_{0};

   [[nodiscard]] bool isFull() const noexcept
   {
      const auto head = head_.load(std::memory_order_relaxed);
      const auto tail = tail_.load(std::memory_order_acquire);
      return (head - tail) == N;
   }

   [[nodiscard]] bool isEmpty() const noexcept
   {
      const auto head = head_.load(std::memory_order_acquire);
      const auto tail = tail_.load(std::memory_order_relaxed);
      return head == tail;
   }

   [[nodiscard]] size_type usedSlots() const noexcept
   {
      const auto head = head_.load(std::memory_order_relaxed);
      const auto tail = tail_.load(std::memory_order_acquire);
      return head - tail;
   }

   template <typename U> bool pushImpl(U &&value)
   {
      const auto head = head_.load(std::memory_order_relaxed);
      const auto tail = tail_.load(std::memory_order_acquire);

      if ((head - tail) == N)
      {
         return false;
      }

      data_[head & mask].emplace(std::forward<U>(value));
      head_.store(head + 1, std::memory_order_release);
      return true;
   }

 public:
   RingBuffer() = default;
   ~RingBuffer() = default;

   RingBuffer(const RingBuffer &) = delete;
   RingBuffer &operator=(const RingBuffer &) = delete;

   RingBuffer(RingBuffer &&other) noexcept
       : data_(std::move(other.data_)), head_(other.head_.load(std::memory_order_relaxed)),
         tail_(other.tail_.load(std::memory_order_relaxed))
   {
      other.clear();
      other.head_.store(0, std::memory_order_relaxed);
      other.tail_.store(0, std::memory_order_relaxed);
   }

   RingBuffer &operator=(RingBuffer &&other) noexcept
   {
      if (this == &other)
      {
         return *this;
      }

      clear();
      data_ = std::move(other.data_);
      head_.store(other.head_.load(std::memory_order_relaxed), std::memory_order_relaxed);
      tail_.store(other.tail_.load(std::memory_order_relaxed), std::memory_order_relaxed);

      other.clear();
      other.head_.store(0, std::memory_order_relaxed);
      other.tail_.store(0, std::memory_order_relaxed);
      return *this;
   }

   /**
    * Push an element into the buffer.
    *
    * Thread-safety: Single producer only.
    */
   bool push(const T &value)
   {
      static_assert(std::is_copy_constructible_v<T>,
                    "push(const T&) requires T to be copy constructible");
      return pushImpl(value);
   }

   bool push(T &&value)
   {
      return pushImpl(std::move(value));
   }

   /**
    * Pop an element from the buffer.
    *
    * Thread-safety: Single consumer only.
    */
   [[nodiscard]] std::optional<T> pop()
   {
      const auto tail = tail_.load(std::memory_order_relaxed);
      const auto head = head_.load(std::memory_order_acquire);

      if (head == tail)
      {
         return std::nullopt;
      }

      auto &slot = data_[tail & mask];
      T value = std::move(*slot);
      slot.reset();
      tail_.store(tail + 1, std::memory_order_release);
      return value;
   }

   [[nodiscard]] std::optional<std::reference_wrapper<const T>> peek() const
   {
      const auto tail = tail_.load(std::memory_order_relaxed);
      const auto head = head_.load(std::memory_order_acquire);

      if (head == tail)
      {
         return std::nullopt;
      }

      return std::cref(*data_[tail & mask]);
   }

   [[nodiscard]] bool empty() const noexcept
   {
      return isEmpty();
   }

   [[nodiscard]] bool full() const noexcept
   {
      return isFull();
   }

   [[nodiscard]] size_type size() const noexcept
   {
      return usedSlots();
   }

   [[nodiscard]] static constexpr size_type getCapacity() noexcept
   {
      return N;
   }

   [[nodiscard]] size_type available() const noexcept
   {
      return N - usedSlots();
   }

   void clear() noexcept
   {
      auto tail = tail_.load(std::memory_order_relaxed);
      const auto head = head_.load(std::memory_order_relaxed);

      while (tail != head)
      {
         auto &slot = data_[tail & mask];
         slot.reset();
         ++tail;
      }

      tail_.store(head, std::memory_order_relaxed);
   }
};

enum class EventType
{
   FunctionEnter,
   FunctionExit,
   MemoryAlloc,
   MemoryFree,
   GPUKernelStart,
   GPUKernelEnd,
   Custom
};

struct ProfilingEvent
{
   EventType type{};
   std::uint64_t timestamp{};
   const char *name{};
   std::uint64_t data{};

   ProfilingEvent() = default;

   ProfilingEvent(EventType t, std::uint64_t ts, const char *n, std::uint64_t d = 0)
       : type(t), timestamp(ts), name(n), data(d)
   {
   }

   ProfilingEvent(const ProfilingEvent &) = delete;
   ProfilingEvent &operator=(const ProfilingEvent &) = delete;

   ProfilingEvent(ProfilingEvent &&other) noexcept
       : type(other.type), timestamp(other.timestamp), name(other.name), data(other.data)
   {
   }

   ProfilingEvent &operator=(ProfilingEvent &&other) noexcept
   {
      if (this != &other)
      {
         type = other.type;
         timestamp = other.timestamp;
         name = other.name;
         data = other.data;
      }
      return *this;
   }
};

inline std::uint64_t getTimestamp()
{
   using namespace std::chrono;
   return duration_cast<nanoseconds>(high_resolution_clock::now().time_since_epoch()).count();
}

template <typename Buffer> class ScopedProfiler
{
   Buffer &buffer_;
   const char *name_;
   std::uint64_t startTime_;

 public:
   ScopedProfiler(Buffer &buffer, const char *name)
       : buffer_(buffer), name_(name), startTime_(getTimestamp())
   {
      buffer_.push(ProfilingEvent{EventType::FunctionEnter, startTime_, name_});
   }

   ~ScopedProfiler()
   {
      const auto endTime = getTimestamp();
      buffer_.push(ProfilingEvent{EventType::FunctionExit, endTime, name_, endTime - startTime_});
   }

   ScopedProfiler(const ScopedProfiler &) = delete;
   ScopedProfiler &operator=(const ScopedProfiler &) = delete;
   ScopedProfiler(ScopedProfiler &&) = delete;
   ScopedProfiler &operator=(ScopedProfiler &&) = delete;
};

#endif // RING_BUFFER_HPP
