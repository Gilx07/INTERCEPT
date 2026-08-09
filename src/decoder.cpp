#include "decoder.hpp"

#include <sstream>

namespace {

const char* rpc_name(
    unsigned char id
)
{
    switch (id)
    {
    case 44:
        return "CreateObject";

    case 84:
        return "SetObjectMaterial";

    case 105:
        return "TextDrawSetString";

    default:
        return nullptr;
    }
}

const char* packet_name(
    unsigned char id
)
{
    switch (id)
    {
    case 203:
        return "AimSync";

    case 200:
        return "VehicleSync";

    case 206:
        return "BulletSync";

    case 207:
        return "PlayerSync";

    case 209:
        return "UnoccupiedSync";

    case 210:
        return "TrailerSync";

    case 211:
        return "PassengerSync";

    case 212:
        return "SpectatorSync";

    default:
        return nullptr;
    }
}

} // namespace

namespace intercept::decoder {

DecodeResult decode_rpc(
    unsigned char id,
    const unsigned char* data,
    std::size_t size
)
{
    DecodeResult result{};

    const char* name = rpc_name(id);

    if (name)
    {
        result.name = name;

        std::ostringstream ss;

        ss
            << "id="
            << static_cast<unsigned int>(id)
            << " bytes="
            << size;

        result.details = ss.str();

        return result;
    }

    result.name = "UNKNOWN_RPC";

    std::ostringstream ss;

    ss
        << "id="
        << static_cast<unsigned int>(id)
        << " bytes="
        << size;

    result.details = ss.str();

    return result;
}

DecodeResult decode_packet(
    const unsigned char* data,
    std::size_t size
)
{
    DecodeResult result{};

    if (!data || size == 0)
    {
        result.name = "EMPTY_PACKET";
        result.details = "bytes=0";

        return result;
    }

    const unsigned char id = data[0];

    const char* name = packet_name(id);

    if (name)
    {
        result.name = name;

        std::ostringstream ss;

        ss
            << "id="
            << static_cast<unsigned int>(id)
            << " bytes="
            << size;

        result.details = ss.str();

        return result;
    }

    result.name = "UNKNOWN_PACKET";

    std::ostringstream ss;

    ss
        << "id="
        << static_cast<unsigned int>(id)
        << " bytes="
        << size;

    result.details = ss.str();

    return result;
}

} // namespace intercept::decoder