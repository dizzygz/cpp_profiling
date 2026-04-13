#include <algorithm>
#include <cstdint>
#include <iostream>

class Buffer
{
 public:
   explicit Buffer(uint8_t sz) : size(sz)
   {
      data = size > 0 ? new uint8_t[size] : nullptr;
      std::cout << "[Constructor] Allocated " << size << " bytes at " << (void *)data << "\n";
   }
   // copy constructer
   Buffer(const Buffer &b) : size(b.size)
   {
      data = new uint8_t[size];
      std::copy(b.data, b.data + b.size, data);
      std::cout << "[Copy Constructor] Deep copied " << size << " bytes to " << (void *)data
                << "\n";
   }
   // Move constructor
   Buffer(Buffer &&other) noexcept : size(other.size), data(other.data)
   {
      other.data = nullptr;
      other.size = 0;
      std::cout << "[Move Constructor] " << size << " bytes at " << (void *)data << "\n";
   }
   // 4. Copy Assignment Operator
   Buffer &operator=(const Buffer &other)
   {
      std::cout << "[Copy Assignment] Deep copying via assignment\n";
      if (this != &other)
      {
         // Clean up existing memory
         delete[] data;

         size = other.size;
         data = new uint8_t[size];
         std::copy(other.data, other.data + size, data);
      }
      return *this;
   }
   // 5. Move Assignment Operator
   Buffer &operator=(Buffer &&other) noexcept
   {
      std::cout << "[Move Assignment] Transferring ownership via assignment\n";
      if (this != &other)
      {
         // Release our own resource first
         delete[] data;

         // Take the other object's resource
         data = other.data;
         size = other.size;

         // Leave the other object in a valid, empty state
         other.data = nullptr;
         other.size = 0;
      }
      return *this;
   }
   ~Buffer()
   {
      std::cout << "[Destructor] Freeing memory at " << (void *)data << "\n";
      delete[] data;
   }

 private:
   uint8_t size;
   uint8_t *data;
};
Buffer makeBuffer(size_t n)
{
   Buffer b(n); // local named variable
   return b;    // NRVO should happen
}

int main()
{
   /*std::cout << "--- Creating b1 ---\n";
   Buffer b1(1024);

   std::cout << "\n--- Creating b2 (Copy of b1) ---\n";
   Buffer b2 = b1;

   std::cout << "\n--- Creating b3 (Move of b1) ---\n";
   Buffer b3 = std::move(b1);

   std::cout << "\n--- Assigning b2 to b3 (Copy Assignment) ---\n";
   b3 = b2;

   std::cout << "\n--- Assigning move(b2) to b3 (Move Assignment) ---\n";
   b3 = std::move(b2);

   std::cout << "\n--- Exiting scope ---\n";
   */
   Buffer bb = makeBuffer(5);
   std::cout << "\nBuffer bb: " << &bb << std::endl;
   return 0;
}
