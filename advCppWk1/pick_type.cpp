/*
 * What this shows:

if constexpr picks the branch at compile time.
decltype(auto) preserves the exact return type of the chosen branch.
For int, the function returns int&.
For double, the function returns double.
*/

#include <iostream>
#include <type_traits>

template <typename T> decltype(auto) pick_result(T &value)
{
   if constexpr (std::is_integral_v<T>)
   {
      return (value); // returns T& because of decltype(auto) + parentheses
   }
   else
   {
      return value * 0.5; // returns a value, e.g. double
   }
}

int main()
{
   int i = 10;
   double d = 8.0;

   static_assert(std::is_same_v<decltype(pick_result(i)), int &>);
   static_assert(std::is_same_v<decltype(pick_result(d)), double>);

   pick_result(i) = 42; // proves we got an int&
   std::cout << "i = " << i << '\n';
   std::cout << "pick_result(d) = " << pick_result(d) << '\n';
}
