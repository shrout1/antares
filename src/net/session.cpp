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

#include "net/session.hpp"

#include <pn/output>

namespace antares {
namespace net {

namespace {

const usecs kPollInterval   = std::chrono::milliseconds(5);
const usecs kResendInterval = std::chrono::milliseconds(30);

usecs elapsed_since(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration_cast<usecs>(std::chrono::steady_clock::now() - t);
}

pn::string_view reject_reason(Reject::Reason reason) {
    switch (reason) {
        case Reject::PROTOCOL: return "The host uses a different version of the net protocol.";
        case Reject::VERSION: return "The host runs a different version of Antares.";
        case Reject::PLUGIN: return "The host is playing a different plugin or plugin version.";
        case Reject::FULL: return "The game is full.";
    }
    return "The host refused to let you join.";
}

}  // namespace

std::unique_ptr<Session> Session::host(
        pn::string_view address, uint16_t port, Hello hello, Setup setup) {
    std::unique_ptr<Host> h = Host::listen(address, port, 1);
    if (!h) {
        return nullptr;
    }
    std::unique_ptr<Session>    s(new Session(std::move(h), true, std::move(hello)));
    std::lock_guard<std::mutex> lock(s->_mutex);
    s->_state = LOBBY;
    s->_setup = std::move(setup);
    return s;
}

std::unique_ptr<Session> Session::join(pn::string_view address, uint16_t port, Hello hello) {
    std::unique_ptr<Host> h = Host::connect(address, port);
    if (!h) {
        return nullptr;
    }
    return std::unique_ptr<Session>(new Session(std::move(h), false, std::move(hello)));
}

Session::Session(std::unique_ptr<Host> host, bool is_host, Hello hello)
        : _host(std::move(host)), _is_host(is_host), _hello(std::move(hello)), _stop(false) {
    _thread = std::thread([this] { run(); });
}

Session::~Session() {
    _stop = true;
    _thread.join();
}

Session::State Session::state() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _state;
}

pn::string Session::error() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _error.copy();
}

int Session::local() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _local;
}

Setup Session::setup() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _setup.copy();
}

bool Session::has_peer() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _setup.players.size() == kSessionPlayers;
}

void Session::set_setup(Setup setup) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_is_host || (_state != LOBBY)) {
        return;
    }
    setup.players = std::move(_setup.players);
    _setup        = std::move(setup);
    send_setup();
}

void Session::choose(pn::string_view race, int32_t hue) {
    std::lock_guard<std::mutex> lock(_mutex);
    if ((_state != LOBBY) || (_setup.players.size() <= _local)) {
        return;
    }
    _setup.players[_local].race = race.copy();
    _setup.players[_local].hue  = hue;
    if (_is_host) {
        send_setup();
    } else {
        Packet p;
        p.type        = Packet::CHOOSE;
        p.choose.race = race.copy();
        p.choose.hue  = hue;
        send(RELIABLE, std::move(p));
    }
}

void Session::start() {
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_is_host || (_state != LOBBY) || (_setup.players.size() != kSessionPlayers)) {
        return;
    }
    send_setup();
    Packet p;
    p.type = Packet::START;
    send(RELIABLE, std::move(p));
    _state = STARTED;
}

void Session::ready(uint32_t first) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_state != STARTED) {
        return;
    }
    _rollback.reset(new Rollback(kSessionPlayers, _local, first, _setup.delay));
    Packet p;
    p.type = Packet::READY;
    send(RELIABLE, std::move(p));
    if (_peer_ready) {
        _state = PLAYING;
    }
}

void Session::quit() {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_state == CLOSED) {
        return;
    }
    Packet p;
    p.type        = Packet::QUIT;
    p.quit.player = _local;
    send(RELIABLE, std::move(p));
    _state = CLOSED;
}

void Session::submit(uint32_t at, Rollback::Turn turn) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_rollback) {
        _rollback->submit(at, std::move(turn));
        _turns_dirty = true;
    }
}

uint32_t Session::confirmed() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return (_state == PLAYING) ? _rollback->confirmed() : 0;
}

void Session::set_lead(int32_t lead) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_state == PLAYING) {
        _rollback->set_lead(lead);
    }
}

int32_t Session::peer_lead() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return (_state == PLAYING) ? _rollback->lead(1 - _local) : 0;
}

uint32_t Session::peer_synced() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return (_state == PLAYING) ? _rollback->synced(1 - _local) : 0;
}

sfz::optional<Rollback::Turn> Session::known(int player, uint32_t at) const {
    std::lock_guard<std::mutex> lock(_mutex);
    const Rollback::Turn*       turn = _rollback ? _rollback->known(player, at) : nullptr;
    if (!turn) {
        return sfz::nullopt;
    }
    return sfz::make_optional(Rollback::Turn{*turn});
}

void Session::confirm(uint32_t at) {
    std::lock_guard<std::mutex> lock(_mutex);
    _rollback->confirm(at);
}

void Session::sync(uint32_t at, uint32_t value) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_rollback) {
        _rollback->sync(at, value);
    }
}

bool Session::desynced() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _rollback && _rollback->desynced();
}

void Session::chat(pn::string_view text) {
    std::lock_guard<std::mutex> lock(_mutex);
    Packet                      p;
    p.type        = Packet::CHAT;
    p.chat.player = _local;
    p.chat.text   = text.copy();
    send(RELIABLE, std::move(p));
}

void Session::set_delay(
        std::chrono::milliseconds delay, std::chrono::milliseconds jitter, uint32_t seed) {
    std::lock_guard<std::mutex> lock(_mutex);
    _delay      = delay;
    _jitter     = jitter;
    _delay_seed = seed;
    _delay_set  = true;
}

std::vector<pn::string> Session::take_chats() {
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<pn::string>     chats;
    chats.swap(_chats);
    return chats;
}

void Session::run() {
    auto last_turns = std::chrono::steady_clock::now();
    while (true) {
        const bool  stopping = _stop;
        Host::Event e;
        const bool  got = !stopping && _host->poll(kPollInterval, &e);

        std::lock_guard<std::mutex> lock(_mutex);
        if (_delay_set) {
            _host->set_delay(_delay, _jitter, _delay_seed);
            _delay_set = false;
        }
        if (got) {
            handle(e);
        }
        if (_peer >= 0) {
            for (const auto& out : _outbox) {
                _host->send(_peer, out.first, out.second);
            }
            if (_rollback && (_state == PLAYING) &&
                (_turns_dirty || (elapsed_since(last_turns) >= kResendInterval))) {
                Packet p;
                p.type  = Packet::TURNS;
                p.turns = _rollback->outgoing(1 - _local);
                _host->send(_peer, UNRELIABLE, p);
                _turns_dirty = false;
                last_turns   = std::chrono::steady_clock::now();
            }
        }
        _outbox.clear();
        if (stopping) {
            return;
        }
    }
}

void Session::handle(const Host::Event& e) {
    switch (e.type) {
        case Host::Event::CONNECT:
            if (_peer < 0) {
                _peer = e.peer;
                if (!_is_host) {
                    Packet p;
                    p.type  = Packet::HELLO;
                    p.hello = _hello.copy();
                    _host->send(_peer, RELIABLE, p);
                }
            }
            break;

        case Host::Event::DISCONNECT:
            if (e.peer != _peer) {
                break;
            }
            _peer = -1;
            if (_is_host && (_state == LOBBY)) {
                _setup.players.resize(1);
            } else if (_state == CONNECTING) {
                close("Couldn’t connect to the host.");
            } else {
                close("The other player disconnected.");
            }
            break;

        case Host::Event::RECEIVE:
            if (e.peer != _peer) {
                break;
            } else if (_is_host && (e.packet.type == Packet::HELLO)) {
                handle_hello(e.peer, e.packet.hello);
            } else {
                receive(e.packet);
            }
            break;

        case Host::Event::NONE: break;
    }
}

void Session::handle_hello(int peer, const Hello& hello) {
    if (_state != LOBBY) {
        return;
    }
    Packet p;
    p.type = Packet::REJECT;
    if (hello.protocol != kProtocolVersion) {
        p.reject.reason = Reject::PROTOCOL;
    } else if (hello.antares_version != _hello.antares_version) {
        p.reject.reason = Reject::VERSION;
    } else if (
            (hello.plugin_identifier != _hello.plugin_identifier) ||
            (hello.plugin_version != _hello.plugin_version)) {
        p.reject.reason = Reject::PLUGIN;
    } else if (_setup.players.size() != 1) {
        p.reject.reason = Reject::FULL;
    } else {
        _setup.players.emplace_back();
        Setup::Player& guest = _setup.players.back();
        guest.name           = hello.name.copy();
        guest.race           = _setup.players[0].race.copy();
        guest.hue            = _setup.players[0].hue;

        p                = Packet{};
        p.type           = Packet::WELCOME;
        p.welcome.player = 1;
        _host->send(peer, RELIABLE, p);
        send_setup();
        return;
    }
    _host->send(peer, RELIABLE, p);
}

void Session::receive(const Packet& p) {
    switch (p.type) {
        case Packet::WELCOME:
            if (!_is_host && (_state == CONNECTING) && (p.welcome.player == 1)) {
                _local = p.welcome.player;
                _state = LOBBY;
            }
            break;

        case Packet::REJECT:
            if (!_is_host) {
                close(reject_reason(p.reject.reason));
            }
            break;

        case Packet::SETUP:
            if (!_is_host && (_state == LOBBY) && (p.setup.players.size() == kSessionPlayers)) {
                _setup = p.setup.copy();
            }
            break;

        case Packet::CHOOSE:
            if (_is_host && (_state == LOBBY) && (_setup.players.size() == kSessionPlayers)) {
                _setup.players[1].race = p.choose.race.copy();
                _setup.players[1].hue  = p.choose.hue;
                send_setup();
            }
            break;

        case Packet::START:
            if (!_is_host && (_state == LOBBY)) {
                _state = STARTED;
            }
            break;

        case Packet::READY:
            _peer_ready = true;
            if (_rollback && (_state == STARTED)) {
                _state = PLAYING;
            }
            break;

        case Packet::TURNS:
            if (_rollback) {
                _rollback->receive(p.turns);
            }
            break;

        case Packet::CHAT:
            if ((0 <= p.chat.player) && (p.chat.player < _setup.players.size())) {
                _chats.push_back(
                        pn::format("{0}: {1}", _setup.players[p.chat.player].name, p.chat.text));
            }
            break;

        case Packet::QUIT: close("The other player quit."); break;

        case Packet::NONE:
        case Packet::HELLO: break;
    }
}

void Session::close(pn::string_view error) {
    if (_state != CLOSED) {
        _state = CLOSED;
        _error = error.copy();
    }
}

void Session::send(Channel channel, Packet p) { _outbox.emplace_back(channel, std::move(p)); }

void Session::send_setup() {
    Packet p;
    p.type  = Packet::SETUP;
    p.setup = _setup.copy();
    send(RELIABLE, std::move(p));
}

}  // namespace net
}  // namespace antares
