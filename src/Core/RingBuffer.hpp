#ifndef _TERMIFY_CORE_RINGBUFFER_HPP
#define _TERMIFY_CORE_RINGBUFFER_HPP

#include "Common.hpp"
#include "../Platform/Services.hpp"

namespace termify::core {

class RingBuffer {
public:
  static void Update(AtomicContext &ctx, platform::AudioOutput &output);
};


}

#endif // _TERMIFY_CORE_RINGBUFFER_HPP
