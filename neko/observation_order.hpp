#ifndef OBSERVATION_ORDER_HPP
#define OBSERVATION_ORDER_HPP

#include <cstddef>

// Shared only during one observed master cycle; orders mutation-local journals
// without callbacks or dependencies between CPU and device owners.
struct NekoObservationOrder
{
  std::size_t next = 0;
};

#endif
