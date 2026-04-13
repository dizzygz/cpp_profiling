#include <iostream>
Buffer makeBuffer(size_t n)
{
   Buffer b(n); // local named variable
   return b;    // NRVO should happen
}

int main()
{
   Buffer b = makeBuffer(5);
   std::cout << "Buffer b:" << &b << std::endl;
}
