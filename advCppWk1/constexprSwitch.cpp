#include <cstddef>
#include <cstdint>

constexpr uint32_t hash_str(const char *str, size_t len)
{
   uint32_t hash = 2166136261u; // offset base
   for (size_t i = 0; i < len; i++)
   {
      hash ^= static_cast<uint32_t>(str[i]);
      hash *= 16777619u; // FNV prime
   }
   return hash;
}

template <size_t N> consteval uint32_t hash_literal(const char (&str)[N])
{
   return hash_str(str, N - 1);
}

constexpr uint32_t EVT_START = hash_literal("start");
constexpr uint32_t EVT_STOP = hash_literal("stop");
constexpr uint32_t EVT_FRAME = hash_literal("frame");
constexpr uint32_t EVT_RENDER = hash_literal("render");

void dispatch_event(uint32_t event_id)
{
   switch (event_id)
   {
      case EVT_START:
         // handle "start"
         break;
      case EVT_STOP:
         // handle "stop"
         break;

      case EVT_FRAME:
         // handle "frame"
         break;

      case EVT_RENDER:
         // handle "render"
         break;

      default:
         // unknown event
         break;
   }
}

int main()
{
   dispatch_event(hash_literal("start")); // resolves to EVT_START
   dispatch_event(hash_literal("frame")); // resolves to EVT_FRAME
}
