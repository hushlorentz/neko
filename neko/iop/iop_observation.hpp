#ifndef IOP_OBSERVATION_HPP
#define IOP_OBSERVATION_HPP

#include <cstddef>

// Shared only during one observed master cycle; orders mutation-local journals
// without callbacks or dependencies between CPU and device owners.
struct IOPObservationOrder
{
  std::size_t next = 0;
};

#endif
