#include "monitor.hpp"

#include "console.hpp"
#include "decoder.hpp"
#include "logger.hpp"

#include <RakHook/rakhook.hpp>
#include <RakNet/PacketEnumerations.h>

#include <atomic>
#include <cstddef>
#include <sstream>
#include <string>

namespace {

std::atomic_bool g_installed{false};

constexpr std::size_t MAX_HEX_DUMP = 256;

std::string packet_hex(
    RakNet::BitStream* bs
)
{
    if (!bs)
        return {};

    const auto bytes =
        bs->GetNumberOfBytesUsed();

    const auto* data =
        bs->GetData();

    const std::size_t dump_size =
        bytes < MAX_HEX_DUMP
            ? bytes
            : MAX_HEX_DUMP;

    return intercept::log::hex_dump(
        data,
        dump_size
    );
}

std::string packet_hex(
    Packet* packet
)
{
    if (!packet ||
        !packet->data ||
        packet->length <= 0)
    {
        return {};
    }

    const std::size_t bytes =
        static_cast<std::size_t>(
            packet->length
        );

    const std::size_t dump_size =
        bytes < MAX_HEX_DUMP
            ? bytes
            : MAX_HEX_DUMP;

    return intercept::log::hex_dump(
        packet->data,
        dump_size
    );
}

void print_decoded_rpc(
    const char* direction,
    unsigned char id,
    RakNet::BitStream* bs
)
{
    const std::size_t bytes =
        bs
            ? bs->GetNumberOfBytesUsed()
            : 0;

    const unsigned char* data =
        bs
            ? bs->GetData()
            : nullptr;

    const auto decoded =
        intercept::decoder::decode_rpc(
            id,
            data,
            bytes
        );

    std::ostringstream ss;

    ss
        << '['
        << direction
        << " RPC] "
        << decoded.name
        << " | "
        << decoded.details;

    intercept::console::print(
        ss.str()
    );

    intercept::log::info(
        "DECODED " +
        std::string(direction) +
        " RPC: " +
        decoded.name +
        " | " +
        decoded.details
    );
}

void print_decoded_packet(
    const char* direction,
    RakNet::BitStream* bs
)
{
    if (!bs)
        return;

    const std::size_t bytes =
        bs->GetNumberOfBytesUsed();

    const unsigned char* data =
        bs->GetData();

    const auto decoded =
        intercept::decoder::decode_packet(
            data,
            bytes
        );

    std::ostringstream ss;

    ss
        << '['
        << direction
        << " PACKET] "
        << decoded.name
        << " | "
        << decoded.details;

    intercept::console::print(
        ss.str()
    );

    intercept::log::info(
        "DECODED " +
        std::string(direction) +
        " PACKET: " +
        decoded.name +
        " | " +
        decoded.details
    );
}

void print_decoded_packet(
    const char* direction,
    Packet* packet
)
{
    if (!packet ||
        !packet->data ||
        packet->length <= 0)
    {
        return;
    }

    const auto bytes =
        static_cast<std::size_t>(
            packet->length
        );

    const auto* data =
        reinterpret_cast<const unsigned char*>(
            packet->data
        );

    const auto decoded =
        intercept::decoder::decode_packet(
            data,
            bytes
        );

    std::ostringstream ss;

    ss
        << '['
        << direction
        << " PACKET] "
        << decoded.name
        << " | "
        << decoded.details;

    intercept::console::print(
        ss.str()
    );

    intercept::log::info(
        "DECODED " +
        std::string(direction) +
        " PACKET: " +
        decoded.name +
        " | " +
        decoded.details
    );
}

void register_callbacks()
{
    // ========================================================
    // OUTGOING PACKET
    // ========================================================

    rakhook::on_send_packet +=
        [](
            RakNet::BitStream* bs,
            PacketPriority& priority,
            PacketReliability& reliability,
            char& ord_channel
        ) -> bool
        {
            std::ostringstream ss;

            ss
                << "OUT PACKET"
                << " bytes="
                << (
                    bs
                        ? bs->GetNumberOfBytesUsed()
                        : 0
                )
                << " bits="
                << (
                    bs
                        ? bs->GetNumberOfBitsUsed()
                        : 0
                )
                << " priority="
                << static_cast<int>(
                    priority
                )
                << " reliability="
                << static_cast<int>(
                    reliability
                )
                << " channel="
                << static_cast<int>(
                    ord_channel
                )
                << " hex=["
                << packet_hex(bs)
                << ']';

            intercept::log::info(
                ss.str()
            );

            print_decoded_packet(
                "OUT",
                bs
            );

            return true;
        };

    // ========================================================
    // INCOMING PACKET
    // ========================================================

    rakhook::on_receive_packet +=
        [](
            Packet* packet
        ) -> bool
        {
            const int id =
                (
                    packet &&
                    packet->data &&
                    packet->length > 0
                )
                    ? static_cast<unsigned char>(
                        packet->data[0]
                    )
                    : -1;

            std::ostringstream ss;

            ss
                << "IN PACKET"
                << " id="
                << id
                << " bytes="
                << (
                    packet
                        ? packet->length
                        : 0
                )
                << " hex=["
                << packet_hex(packet)
                << ']';

            intercept::log::info(
                ss.str()
            );

            print_decoded_packet(
                "IN",
                packet
            );

            return true;
        };

    // ========================================================
    // OUTGOING RPC
    // ========================================================

    rakhook::on_send_rpc +=
        [](
            int& id,
            RakNet::BitStream* bs,
            PacketPriority& priority,
            PacketReliability& reliability,
            char& ord_channel,
            bool& sh_timestamp
        ) -> bool
        {
            std::ostringstream ss;

            ss
                << "OUT RPC"
                << " id="
                << id
                << " bytes="
                << (
                    bs
                        ? bs->GetNumberOfBytesUsed()
                        : 0
                )
                << " bits="
                << (
                    bs
                        ? bs->GetNumberOfBitsUsed()
                        : 0
                )
                << " priority="
                << static_cast<int>(
                    priority
                )
                << " reliability="
                << static_cast<int>(
                    reliability
                )
                << " channel="
                << static_cast<int>(
                    ord_channel
                )
                << " timestamp="
                << std::boolalpha
                << sh_timestamp
                << " hex=["
                << packet_hex(bs)
                << ']';

            intercept::log::info(
                ss.str()
            );

            print_decoded_rpc(
                "OUT",
                static_cast<unsigned char>(
                    id
                ),
                bs
            );

            return true;
        };

    // ========================================================
    // INCOMING RPC
    // ========================================================

    rakhook::on_receive_rpc +=
        [](
            unsigned char& id,
            RakNet::BitStream* bs
        ) -> bool
        {
            std::ostringstream ss;

            ss
                << "IN RPC"
                << " id="
                << static_cast<unsigned int>(
                    id
                )
                << " bytes="
                << (
                    bs
                        ? bs->GetNumberOfBytesUsed()
                        : 0
                )
                << " bits="
                << (
                    bs
                        ? bs->GetNumberOfBitsUsed()
                        : 0
                )
                << " hex=["
                << packet_hex(bs)
                << ']';

            intercept::log::info(
                ss.str()
            );

            print_decoded_rpc(
                "IN",
                id,
                bs
            );

            return true;
        };
}

} // namespace

namespace intercept::monitor {

void install()
{
    if (g_installed.exchange(true))
        return;

    register_callbacks();

    log::info(
        "INTERCEPT monitor callbacks registered."
    );

    console::print(
        "[MONITOR] RakHook callbacks registered."
    );
}

void uninstall()
{
    /*
     * RakHook's event container does not expose
     * a stable public callback-clear API.
     *
     * Callbacks remain installed until process teardown.
     */

    g_installed = false;
}

} // namespace intercept::monitor