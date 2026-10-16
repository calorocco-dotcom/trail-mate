#pragma once
#include <cstddef>
#include <cstdint>
namespace music
{
class Reader
{
  public:
    virtual ~Reader() = default;
    virtual uint32_t size() const = 0;
    virtual std::size_t read_at(uint32_t offset, void* output, std::size_t bytes) = 0;
};
} // namespace music
