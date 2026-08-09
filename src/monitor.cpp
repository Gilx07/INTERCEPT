#include "monitor.hpp"
#include "decoder.hpp"
#include "event_queue.hpp"
#include "gui.hpp"
#include "logger.hpp"

#include <RakHook/rakhook.hpp>
#include <RakNet/BitStream.h>

#include <atomic>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace {
std::atomic_bool g_installed{false};
constexpr std::size_t MAX_HEX_DUMP = 256;

const char* direction_name(intercept::events::Direction direction)
{
    return direction == intercept::events::Direction::Incoming ? "IN" : "OUT";
}

void publish_gui(const intercept::events::Event& event)
{
    const char* type = event.kind == intercept::events::Kind::Packet ? "PACKET" : "RPC";

    std::string message;
    message.reserve(event.name.size() + event.details.size() + event.hex.size() + 64);

    message += type;
    message += '|';
    message += std::to_string(event.id);
    message += '|';
    message += std::to_string(event.bytes);
    message += '|';
    message += direction_name(event.direction);
    message += '|';
    message += event.name;

    if (!event.details.empty()) {
        message += " | ";
        message += event.details;
    }

    if (!event.hex.empty()) {
        message += " | HEX: ";
        message += event.hex;
    }

    intercept::gui::push_event(message);
}

std::string packet_hex(RakNet::BitStream* bs)
{
    if (!bs) return {};
    const auto bytes = bs->GetNumberOfBytesUsed();
    const auto* data = bs->GetData();
    const auto dump_size = bytes < MAX_HEX_DUMP ? bytes : MAX_HEX_DUMP;
    return intercept::log::hex_dump(data, dump_size);
}

std::string packet_hex(Packet* packet)
{
    if (!packet || !packet->data || packet->length <= 0) return {};
    const auto bytes = static_cast<std::size_t>(packet->length);
    const auto dump_size = bytes < MAX_HEX_DUMP ? bytes : MAX_HEX_DUMP;
    return intercept::log::hex_dump(
        reinterpret_cast<const unsigned char*>(packet->data), dump_size);
}

void push_packet(intercept::events::Direction direction, RakNet::BitStream* bs)
{
    intercept::events::Event e;
    e.kind = intercept::events::Kind::Packet;
    e.direction = direction;
    e.bytes = bs ? bs->GetNumberOfBytesUsed() : 0;
    const auto* data = bs ? bs->GetData() : nullptr;
    const auto decoded = intercept::decoder::decode_packet(data, e.bytes);
    e.name = decoded.name;
    e.details = decoded.details;
    e.id = (data && e.bytes) ? static_cast<unsigned char>(data[0]) : -1;
    e.hex = packet_hex(bs);

    publish_gui(e);
    intercept::events::push(std::move(e));
}

void push_packet(intercept::events::Direction direction, Packet* packet)
{
    intercept::events::Event e;
    e.kind = intercept::events::Kind::Packet;
    e.direction = direction;
    e.bytes = packet && packet->length > 0 ? static_cast<std::size_t>(packet->length) : 0;
    const auto* data = packet && packet->data
        ? reinterpret_cast<const unsigned char*>(packet->data) : nullptr;
    const auto decoded = intercept::decoder::decode_packet(data, e.bytes);
    e.name = decoded.name;
    e.details = decoded.details;
    e.id = (data && e.bytes) ? data[0] : -1;
    e.hex = packet_hex(packet);

    publish_gui(e);
    intercept::events::push(std::move(e));
}

void push_rpc(intercept::events::Direction direction, unsigned char id, RakNet::BitStream* bs)
{
    intercept::events::Event e;
    e.kind = intercept::events::Kind::Rpc;
    e.direction = direction;
    e.id = id;
    e.bytes = bs ? bs->GetNumberOfBytesUsed() : 0;
    const auto* data = bs ? bs->GetData() : nullptr;
    const auto decoded = intercept::decoder::decode_rpc(id, data, e.bytes);
    e.name = decoded.name;
    e.details = decoded.details;
    e.hex = packet_hex(bs);

    publish_gui(e);
    intercept::events::push(std::move(e));
}

bool parse_hex(const std::string& input, std::vector<unsigned char>& out)
{
    auto value = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    out.clear();
    int high = -1;

    for (char c : input)
    {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            continue;

        const int nibble = value(c);
        if (nibble < 0)
            return false;

        if (high < 0)
            high = nibble;
        else
        {
            out.push_back(static_cast<unsigned char>((high << 4) | nibble));
            high = -1;
        }
    }

    return high < 0 && !out.empty();
}

void register_callbacks()
{
    intercept::log::info("[HOOK] Registering on_send_packet callback.");
    rakhook::on_send_packet +=
        [](RakNet::BitStream* bs, PacketPriority&, PacketReliability&, char&) -> bool {
            intercept::log::info("[HOOK] on_send_packet fired.");
            push_packet(intercept::events::Direction::Outgoing, bs);
            return true;
        };

    intercept::log::info("[HOOK] Registering on_receive_packet callback.");
    rakhook::on_receive_packet +=
        [](Packet* packet) -> bool {
            intercept::log::info("[HOOK] on_receive_packet fired.");
            push_packet(intercept::events::Direction::Incoming, packet);
            return true;
        };

    intercept::log::info("[HOOK] Registering on_send_rpc callback.");
    rakhook::on_send_rpc +=
        [](int& id, RakNet::BitStream* bs, PacketPriority&, PacketReliability&, char&, bool&) -> bool {
            intercept::log::info("[HOOK] on_send_rpc fired. id=" + std::to_string(id));
            push_rpc(intercept::events::Direction::Outgoing, static_cast<unsigned char>(id), bs);
            return true;
        };

    intercept::log::info("[HOOK] Registering on_receive_rpc callback.");
    rakhook::on_receive_rpc +=
        [](unsigned char& id, RakNet::BitStream* bs) -> bool {
            intercept::log::info("[HOOK] on_receive_rpc fired. id=" + std::to_string(id));
            push_rpc(intercept::events::Direction::Incoming, id, bs);
            return true;
        };
}
} // namespace

namespace intercept::monitor {

void install()
{
    if (g_installed.exchange(true)) return;
    register_callbacks();
    log::info("INTERCEPT monitor callbacks registered.");
}

void uninstall()
{
    g_installed = false;
}

bool send_packet_hex(const std::string& hex)
{
    std::vector<unsigned char> bytes;
    if (!parse_hex(hex, bytes))
    {
        log::error("GUI send packet rejected: invalid HEX.");
        return false;
    }

    RakNet::BitStream bs(bytes.data(), static_cast<unsigned int>(bytes.size()), false);
    const bool ok = rakhook::send(
        &bs,
        HIGH_PRIORITY,
        RELIABLE_ORDERED,
        0
    );

    log::info(std::string("GUI send packet: ") + (ok ? "success" : "failed"));
    return ok;
}

bool send_rpc_hex(int id, const std::string& hex)
{
    if (id < 0 || id > 255)
        return false;

    std::vector<unsigned char> bytes;
    if (!parse_hex(hex, bytes))
    {
        log::error("GUI send RPC rejected: invalid HEX.");
        return false;
    }

    RakNet::BitStream bs(bytes.data(), static_cast<unsigned int>(bytes.size()), false);
    const bool ok = rakhook::send_rpc(
        id,
        &bs,
        HIGH_PRIORITY,
        RELIABLE_ORDERED,
        0,
        false
    );

    log::info(std::string("GUI send RPC: ") + (ok ? "success" : "failed"));
    return ok;
}

} // namespace intercept::monitor
