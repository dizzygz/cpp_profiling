#include <cstring>
#include <iostream>

class UniqueBuffer
{
 public:
   size_t size{};
   uint8_t *data{};

   explicit UniqueBuffer(size_t sz) : size(sz), data(sz ? new uint8_t[sz] : nullptr)
   {
      std::cout << "[Ctor] Allocated " << size << " bytes at " << (void *)data << "\n";
   }

   // Delete copy operations → move‑only
   UniqueBuffer(const UniqueBuffer &) = delete;
   UniqueBuffer &operator=(const UniqueBuffer &) = delete;

   // Move constructor
   UniqueBuffer(UniqueBuffer &&other) noexcept : size(other.size), data(other.data)
   {
      other.size = 0;
      other.data = nullptr;
      std::cout << "[Move Ctor] Moved buffer to " << (void *)data << "\n";
   }

   // Move assignment
   UniqueBuffer &operator=(UniqueBuffer &&other) noexcept
   {
      if (this != &other)
      {
         delete[] data;
         size = other.size;
         data = other.data;
         other.size = 0;
         other.data = nullptr;
         std::cout << "[Move Assign] Moved buffer to " << (void *)data << "\n";
      }
      return *this;
   }

   ~UniqueBuffer()
   {
      std::cout << "[Dtor] Freeing " << (void *)data << "\n";
      delete[] data;
   }
};

UniqueBuffer f1(UniqueBuffer buf)
{
   std::cout << "Inside f1\n";
   return buf; // NRVO or move
}

UniqueBuffer f2(UniqueBuffer buf)
{
   std::cout << "Inside f2\n";
   return buf; // NRVO or move
}

UniqueBuffer f3(UniqueBuffer buf)
{
   std::cout << "Inside f3\n";
   return buf; // NRVO or move
}

int main()
{
   UniqueBuffer b = f3(f2(f1(UniqueBuffer(55))));
}
