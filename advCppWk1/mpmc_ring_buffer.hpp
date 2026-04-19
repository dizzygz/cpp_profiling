/**
 * Lock-Free Multi-Producer Multi-Consumer (MPMC) Ring Buffer
 *
 * A high-performance ring buffer supporting concurrent:
 * - Multiple producers
 * - Multiple consumers
 *
 * Uses a simpler CAS-based algorithm with bounded retries.
 *
 * Compile:
 *   g++ -std=c++17 -Wall -Wextra -Wpedantic -fsanitize=thread \
 *        -g mpmc_ring_buffer_test.cpp -o mpmc_ring_buffer_test -pthread
 */

#ifndef MPMC_RING_BUFFER_HPP
#define MPMC_RING_BUFFER_HPP

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <type_traits>

/**
 * MPMCRingBuffer<T, N> - Lock-free Multi-Producer Multi-Consumer ring buffer
 *
 * Template Parameters:
 *   T     - Element type (must be movable)
 *   N     - Buffer capacity (must be power of 2)
 *
 * Uses a ticket-based algorithm:
 * - headTicket: monotonically increasing ticket for producers
 * - tailTicket: monotonically increasing ticket for consumers
 * - Each slot has a sequence number indicating state
 *
 * The buffer is logically full when:
 *   (headTicket - tailTicket) >= N
 */
template <typename T, std::size_t N> class MPMCRingBuffer
{
   static_assert(N > 0, "Capacity must be greater than 0");
   static_assert((N & (N - 1)) == 0, "Capacity must be a power of 2");

 public:
   using value_type = T;
   using size_type = std::size_t;

   static constexpr size_type capacity = N;

 private:
   // Cache line size
   static constexpr std::size_t cache_line_size = 64;

   // Slot state enum values
   enum SlotState : size_type
   {
      STATE_AVAILABLE = 0,
      STATE_DATA_READY = 1
   };

   struct alignas(64) Slot
   {
      std::atomic<size_type> state{STATE_AVAILABLE};
      alignas(64) char data[sizeof(T)];

      Slot() = default;

      Slot(const Slot &) = delete;
      Slot &operator=(const Slot &) = delete;
      Slot(Slot &&) noexcept = default;
      Slot &operator=(Slot &&) noexcept = default;

      T *getData()
      {
         return reinterpret_cast<T *>(data);
      }
      const T *getData() const
      {
         return reinterpret_cast<const T *>(data);
      }
   };

   // Backing store
   std::unique_ptr<Slot[]> slots_;

   // Ticket-based indices (instead of head/tail positions)
   alignas(cache_line_size) std::atomic<size_type> headTicket_{0};
   alignas(cache_line_size) std::atomic<size_type> tailTicket_{0};

   // Mask for power-of-2 modulo
   static constexpr size_type mask = N - 1;

   // Maximum spin iterations before giving up
   static constexpr int max_retries = 1000;

   // Get slot index from ticket
   size_type slotIndex(size_type ticket) const
   {
      return ticket & mask;
   }

 public:
   MPMCRingBuffer() : slots_(std::make_unique<Slot[]>(N))
   {
      for (size_type i = 0; i < N; ++i)
      {
         slots_[i].state.store(0, std::memory_order_relaxed);
      }
   }

   ~MPMCRingBuffer()
   {
      // Drain any remaining items
      while (!empty())
      {
         static_cast<void>(pop());
      }
   }

   // Non-copyable
   MPMCRingBuffer(const MPMCRingBuffer &) = delete;
   MPMCRingBuffer &operator=(const MPMCRingBuffer &) = delete;

   // Move constructor
   MPMCRingBuffer(MPMCRingBuffer &&other) noexcept
       : slots_(std::move(other.slots_)),
         headTicket_(other.headTicket_.load(std::memory_order_relaxed)),
         tailTicket_(other.tailTicket_.load(std::memory_order_relaxed))
   {
      other.headTicket_.store(0, std::memory_order_relaxed);
      other.tailTicket_.store(0, std::memory_order_relaxed);
   }

   // Move assignment
   MPMCRingBuffer &operator=(MPMCRingBuffer &&other) noexcept
   {
      if (this != &other)
      {
         while (!empty())
            pop();
         slots_ = std::move(other.slots_);
         headTicket_.store(other.headTicket_.load(std::memory_order_relaxed),
                           std::memory_order_relaxed);
         tailTicket_.store(other.tailTicket_.load(std::memory_order_relaxed),
                           std::memory_order_relaxed);
      }
      return *this;
   }

   /**
    * Push an element (Multi-Producer safe)
    *
    * @param value Element to move into the buffer
    * @return true if pushed, false if buffer is full
    */
   bool push(T &&value)
   {
      // Get current head ticket
      size_type head = headTicket_.load(std::memory_order_relaxed);

      for (int retry = 0; retry < max_retries; ++retry)
      {
         // Check if buffer is full
         size_type tail = tailTicket_.load(std::memory_order_acquire);
         if (head - tail >= N)
         {
            return false; // Buffer full
         }

         // Get slot for this ticket
         Slot *slot = slots_.get() + slotIndex(head);

         // Try to claim this slot
         size_type expected = head;
         if (headTicket_.compare_exchange_weak(expected, head + 1, std::memory_order_acq_rel,
                                               std::memory_order_relaxed))
         {
            // Successfully claimed the slot
            // Write data
            T *dest = slot->getData();
            ::new (dest) T(std::move(value));
            slot->state.store(1, std::memory_order_release); // Data ready

            return true;
         }

         // CAS failed, head was modified
         head = expected;
      }

      return false; // Give up after max retries
   }

   /**
    * Pop an element (Multi-Consumer safe)
    *
    * @return Element if available, empty optional otherwise
    */
   [[nodiscard]] std::optional<T> pop()
   {
      // Get current tail ticket
      size_type tail = tailTicket_.load(std::memory_order_relaxed);

      for (int retry = 0; retry < max_retries; ++retry)
      {
         // Check if buffer is empty
         size_type head = headTicket_.load(std::memory_order_acquire);
         if (head <= tail)
         {
            return std::nullopt; // Buffer empty
         }

         // Get slot for this ticket
         Slot *slot = slots_.get() + slotIndex(tail);

         // Check if data is ready
         size_type state = slot->state.load(std::memory_order_acquire);
         if (state == 1)
         {
            // Data is ready, try to claim this slot
            size_type expected = tail;
            if (tailTicket_.compare_exchange_weak(expected, tail + 1, std::memory_order_acq_rel,
                                                  std::memory_order_relaxed))
            {
               // Successfully claimed the slot
               // Read data
               T *src = slot->getData();
               T value = std::move(*src);
               src->~T();

               // Reset slot state to available
               slot->state.store(0, std::memory_order_release);

               return value;
            }

            tail = expected;
         }
         else
         {
            // Data not ready, help by yielding
            for (int i = 0; i < 4; ++i)
            {
               // Brief spin
            }
         }
      }

      return std::nullopt; // Give up after max retries
   }

   /**
    * Check if buffer is empty (approximate)
    */
   [[nodiscard]] bool empty() const noexcept
   {
      return headTicket_.load(std::memory_order_acquire) <=
             tailTicket_.load(std::memory_order_acquire);
   }

   /**
    * Check if buffer is full (approximate)
    */
   [[nodiscard]] bool full() const noexcept
   {
      return headTicket_.load(std::memory_order_relaxed) -
                 tailTicket_.load(std::memory_order_acquire) >=
             N;
   }

   /**
    * Get approximate size
    */
   [[nodiscard]] size_type size() const noexcept
   {
      return headTicket_.load(std::memory_order_relaxed) -
             tailTicket_.load(std::memory_order_acquire);
   }

   /**
    * Get capacity
    */
   [[nodiscard]] static constexpr size_type getCapacity() noexcept
   {
      return N;
   }

   /**
    * Get approximate available slots
    */
   [[nodiscard]] size_type available() const noexcept
   {
      return N - size();
   }

   /**
    * Clear buffer (not thread-safe)
    */
   void clear() noexcept
   {
      while (!empty())
      {
         pop();
      }
   }
};

// ============================================================================
// SimpleMPMCBuffer - Simplified version for basic types
// ============================================================================

/**
 * SimpleMPMCBuffer<T, N> - Simplified MPMC ring buffer
 *
 * Uses atomic flag per slot for simpler semantics.
 */
template <typename T, std::size_t N> class SimpleMPMCBuffer
{
   static_assert(N > 0, "Capacity must be greater than 0");
   static_assert((N & (N - 1)) == 0, "Capacity must be a power of 2");

 public:
   using value_type = T;
   using size_type = std::size_t;
   static constexpr size_type capacity = N;

 private:
   struct alignas(64) Slot
   {
      std::atomic<bool> ready{false};
      T data;
   };

   std::unique_ptr<Slot[]> slots_;
   alignas(64) std::atomic<size_type> head_{0};
   alignas(64) std::atomic<size_type> tail_{0};

   static constexpr size_type mask = N - 1;

 public:
   SimpleMPMCBuffer() : slots_(std::make_unique<Slot[]>(N))
   {
      for (size_type i = 0; i < N; ++i)
      {
         slots_[i].ready.store(false, std::memory_order_relaxed);
      }
   }

   ~SimpleMPMCBuffer() = default;
   SimpleMPMCBuffer(const SimpleMPMCBuffer &) = delete;
   SimpleMPMCBuffer &operator=(const SimpleMPMCBuffer &) = delete;
   SimpleMPMCBuffer(SimpleMPMCBuffer &&) = default;
   SimpleMPMCBuffer &operator=(SimpleMPMCBuffer &&) = default;

   /**
    * Push element (multi-producer safe)
    */
   bool push(const T &value)
   {
      size_type h = head_.load(std::memory_order_relaxed);

      for (int retry = 0; retry < 1000; ++retry)
      {
         Slot *slot = slots_.get() + (h & mask);

         if (!slot->ready.load(std::memory_order_acquire))
         {
            if (head_.compare_exchange_weak(h, h + 1, std::memory_order_acq_rel,
                                            std::memory_order_relaxed))
            {
               slot->data = value;
               slot->ready.store(true, std::memory_order_release);
               return true;
            }
         }
         else
         {
            size_type t = tail_.load(std::memory_order_acquire);
            if ((h - t) >= N)
               return false;
            ++h;
         }
      }
      return false;
   }

   /**
    * Pop element (multi-consumer safe)
    */
   bool pop(T &result)
   {
      size_type t = tail_.load(std::memory_order_relaxed);

      for (int retry = 0; retry < 1000; ++retry)
      {
         Slot *slot = slots_.get() + (t & mask);

         if (slot->ready.load(std::memory_order_acquire))
         {
            if (tail_.compare_exchange_weak(t, t + 1, std::memory_order_acq_rel,
                                            std::memory_order_relaxed))
            {
               result = slot->data;
               slot->ready.store(false, std::memory_order_release);
               return true;
            }
         }
         else
         {
            size_type h = head_.load(std::memory_order_acquire);
            if (h == t)
               return false;
         }
      }
      return false;
   }

   [[nodiscard]] bool empty() const noexcept
   {
      return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
   }

   [[nodiscard]] bool full() const noexcept
   {
      size_type h = head_.load(std::memory_order_relaxed);
      size_type t = tail_.load(std::memory_order_acquire);
      return (h - t) >= N;
   }

   [[nodiscard]] static constexpr size_type getCapacity() noexcept
   {
      return N;
   }
};

#endif // MPMC_RING_BUFFER_HPP
