// Copyright (C) 1997, 1999-2001, 2008 Nathan Lamont
// Copyright (C) 2008-2026 The Antares Authors
//
// This file is part of Antares, a tactical space combat game.
//
// Antares is free software: you can redistribute it and/or modify it
// under the terms of the Lesser GNU General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Antares is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with Antares.  If not, see http://www.gnu.org/licenses/

#include "net/host.hpp"

#include <enet/enet.h>
#include <stdexcept>

namespace antares {
namespace net {

namespace {

const size_t kChannelCount  = 2;
const size_t kMaxPacketSize = 64 * 1024;

struct Library {
    Library() : ok(enet_initialize() == 0) {}
    ~Library() {
        if (ok) {
            enet_deinitialize();
        }
    }
    const bool ok;
};

bool initialize() {
    static Library library;
    return library.ok;
}

bool resolve(pn::string_view address, uint16_t port, ENetAddress* out) {
    out->host = ENET_HOST_ANY;
    out->port = port;
    return address.empty() || (enet_address_set_host(out, address.copy().c_str()) == 0);
}

ENetPacket* make_packet(Channel channel, const Packet& packet) {
    pn::data bytes = encode(packet);
    return enet_packet_create(
            bytes.data(), bytes.size(), (channel == RELIABLE) ? ENET_PACKET_FLAG_RELIABLE : 0);
}

}  // namespace

void Host::Deleter::operator()(_ENetHost* host) const {
    enet_host_flush(host);
    for (size_t i = 0; i < host->peerCount; ++i) {
        if (host->peers[i].state == ENET_PEER_STATE_CONNECTED) {
            enet_peer_disconnect_now(&host->peers[i], 0);
        }
    }
    enet_host_destroy(host);
}

Host::Host(_ENetHost* host) : _host(host) { _host->maximumPacketSize = kMaxPacketSize; }

Host::~Host() = default;

std::unique_ptr<Host> Host::listen(pn::string_view address, uint16_t port, int max_peers) {
    ENetAddress a;
    if (!initialize() || !resolve(address, port, &a)) {
        return nullptr;
    }
    ENetHost* host = enet_host_create(&a, max_peers, kChannelCount, 0, 0);
    if (!host) {
        return nullptr;
    }
    return std::unique_ptr<Host>(new Host(host));
}

std::unique_ptr<Host> Host::connect(pn::string_view address, uint16_t port) {
    ENetAddress a;
    if (!initialize() || address.empty() || !resolve(address, port, &a)) {
        return nullptr;
    }
    ENetHost* h = enet_host_create(nullptr, 1, kChannelCount, 0, 0);
    if (!h) {
        return nullptr;
    }
    std::unique_ptr<Host> host(new Host(h));
    if (!enet_host_connect(host->_host.get(), &a, kChannelCount, kProtocolVersion)) {
        return nullptr;
    }
    return host;
}

uint16_t Host::port() const { return _host->address.port; }

bool Host::poll(usecs timeout, Event* event) {
    enet_uint32 ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();
    ENetEvent   e;
    while (enet_host_service(_host.get(), &e, ms) > 0) {
        ms          = 0;
        *event      = Event{};
        event->peer = e.peer - _host->peers;
        switch (e.type) {
            case ENET_EVENT_TYPE_CONNECT: event->type = Event::CONNECT; return true;
            case ENET_EVENT_TYPE_DISCONNECT: event->type = Event::DISCONNECT; return true;
            case ENET_EVENT_TYPE_RECEIVE: {
                pn::data_view bytes{
                        e.packet->data,
                        static_cast<pn::data_view::size_type>(e.packet->dataLength)};
                bool ok = decode(bytes, &event->packet);
                enet_packet_destroy(e.packet);
                if (ok) {
                    event->type = Event::RECEIVE;
                    return true;
                }
                break;
            }
            case ENET_EVENT_TYPE_NONE: break;
        }
    }
    return false;
}

void Host::send(int peer, Channel channel, const Packet& packet) {
    if ((peer < 0) || (_host->peerCount <= static_cast<size_t>(peer))) {
        throw std::runtime_error("invalid peer");
    }
    ENetPacket* p = make_packet(channel, packet);
    if (enet_peer_send(&_host->peers[peer], channel, p) < 0) {
        enet_packet_destroy(p);
    }
}

void Host::broadcast(Channel channel, const Packet& packet) {
    enet_host_broadcast(_host.get(), channel, make_packet(channel, packet));
}

void Host::disconnect(int peer) {
    if ((peer < 0) || (_host->peerCount <= static_cast<size_t>(peer))) {
        throw std::runtime_error("invalid peer");
    }
    enet_peer_disconnect(&_host->peers[peer], 0);
}

}  // namespace net
}  // namespace antares
