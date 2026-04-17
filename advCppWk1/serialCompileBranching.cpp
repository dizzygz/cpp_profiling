/*
 * Non‑matching branches are discarded entirely
✅ No if instruction in the generated code
✅ No virtual dispatch
✅ No RTTI
*/

/*
 *Why std::string_view is the right solution
It unifies:

char*
const char*
char[N]
std::string
std::string_view

Without fragile type matching.
This is exactly why string_view exists.
*/
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>

template <typename T> void serialize(const T &value)
{
   using D = std::decay_t<T>;
   if constexpr (std::is_integral_v<D>)
   {
      // integral serialization
      std::cout << "int:" << value << "\n";
   }
   else if constexpr (std::is_floating_point_v<D>)
   {
      // floating-point serialization
      std::cout << "float:" << value << "\n";
   }
   else if constexpr (std::is_convertible_v<D, std::string_view>)
   {
      // string serialization
      std::cout << "string:" << value << "\n";
   }
   else
   {
      static_assert(!sizeof(T), "serialize(): unsupported type");
   }
}

int main()
{
   serialize(123);
   serialize(3.14159);
   serialize("hello");
   serialize(std::string("world"));
}
