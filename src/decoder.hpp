#pragma once

#include <cstddef>
#include <string>

namespace intercept::decoder {

struct DecodeResult
{
    std::string name;
    std::string details;
};

DecodeResult decode_rpc(
    unsigned char id,
    const unsigned char* data,
    std::size_t size
);

DecodeResult decode_packet(
    const unsigned char* data,
    std::size_t size
);

} // namespace intercept::decoder