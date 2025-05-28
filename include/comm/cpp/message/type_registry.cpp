#include "raw.hpp"

namespace comm::message {

std::atomic<std::uint32_t> TypeRegistry::next_id_{1000}; // Start from 1000 to avoid conflicts with reserved IDs

}