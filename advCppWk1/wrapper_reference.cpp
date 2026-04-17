/*
 * Why this works:

T&& is a forwarding reference, so T deduces differently for lvalues vs rvalues.
std::forward<T>(value) preserves the original value category.
decltype(auto) preserves the exact returned type:
lvalue argument -> int&
rvalue argument -> int&&
One important caveat:

make_wrapper(42) returns an int&& referring to a temporary, so storing that result can dangle. This
pattern is great for forwarding, but not for long-term storage.
*/

#include <type_traits>
#include <utility>

template <typename T> constexpr decltype(auto) make_wrapper(T &&value) noexcept
{
   return std::forward<T>(value);
}

int main()
{
   int x = 10;

   static_assert(std::is_same_v<decltype(make_wrapper(x)), int &>);
   static_assert(std::is_same_v<decltype(make_wrapper(42)), int &&>);

   make_wrapper(x) = 99; // returns int&, so this updates x
}
