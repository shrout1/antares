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

#include <algorithm>
#include <chrono>
#include <pn/output>
#include <sfz/sfz.hpp>
#include <thread>

#include "build/defs.hpp"
#include "config/ledger.hpp"
#include "config/preferences.hpp"
#include "data/level.hpp"
#include "data/plugin.hpp"
#include "game/admiral.hpp"
#include "game/globals.hpp"
#include "game/input-source.hpp"
#include "game/instruments.hpp"
#include "game/labels.hpp"
#include "game/level.hpp"
#include "game/main.hpp"
#include "game/messages.hpp"
#include "game/space-object.hpp"
#include "game/sys.hpp"
#include "game/vector.hpp"
#include "lang/exception.hpp"
#include "math/random.hpp"
#include "net/session.hpp"
#include "sound/driver.hpp"
#include "ui/card.hpp"
#include "video/text-driver.hpp"

using std::unique_ptr;

namespace args = sfz::args;

namespace antares {
namespace {

// Presses keys chosen from `seed` and the turn alone, so that a player's input is the same however
// their frames fall.
class ScriptedInputSource : public InputSource {
  public:
    explicit ScriptedInputSource(uint32_t seed) : _seed(seed) {}

    virtual void start() {}

    virtual bool get(Handle<Admiral> admiral, game_ticks at, EventReceiver& receiver) {
        static const KeyNum kHeld[]    = {kUpKeyNum,  kLeftKeyNum, kRightKeyNum,
                                          kOneKeyNum, kTwoKeyNum,  kEnterKeyNum};
        static const KeyNum kPressed[] = {kSelectFoeKeyNum, kSelectFriendKeyNum, kOrderKeyNum,
                                          kCompDownKeyNum,  kCompAcceptKeyNum,   kTransferKeyNum};

        uint32_t turn = at.time_since_epoch() / kMajorTick;
        Random   r{static_cast<int32_t>(_seed ^ (turn * 2654435761u))};
        if (r.next(4) == 0) {
            int i    = r.next(sizeof(kHeld) / sizeof(kHeld[0]));
            Key key  = sys.prefs->key(kHeld[i]);
            _held[i] = !_held[i];
            if (_held[i]) {
                KeyDownEvent(wall_time(), key).send(&receiver);
            } else {
                KeyUpEvent(wall_time(), key).send(&receiver);
            }
        }
        if (r.next(40) == 0) {
            Key key = sys.prefs->key(kPressed[r.next(sizeof(kPressed) / sizeof(kPressed[0]))]);
            KeyDownEvent(wall_time(), key).send(&receiver);
            KeyUpEvent(wall_time(), key).send(&receiver);
        }
        return true;
    }

  private:
    const uint32_t _seed;
    bool           _held[6] = {};
};

net::Hello hello(pn::string_view name) {
    net::Hello h;
    h.protocol          = net::kProtocolVersion;
    h.antares_version   = kAntaresVersion;
    h.plugin_identifier = plug.info.identifier.hash.copy();
    h.plugin_version    = plug.info.version.copy();
    h.name              = name.copy();
    return h;
}

// Waits up to 10 seconds of real time for `done`.
template <typename F>
void wait_for(pn::string_view what, F done) {
    auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!done()) {
        if (std::chrono::steady_clock::now() > until) {
            throw std::runtime_error(pn::format("timed out waiting for {0}", what).c_str());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

class NetSimMaster : public Card {
  public:
    NetSimMaster(
            bool is_host, uint16_t port, pn::string_view level, uint32_t seed,
            pn::string_view world_log, uint32_t turns, int delay, int jitter)
            : _is_host(is_host),
              _port(port),
              _level(level.copy()),
              _seed(seed),
              _world_log(world_log.copy()),
              _turns(turns),
              _delay(delay),
              _jitter(jitter),
              _input_source(seed) {}

    virtual void become_front() {
        switch (_state) {
            case NEW:
                _state = PLAYING;
                init();
                start();
                break;

            case PLAYING:
                // Let the other player's tail reach the last turn before quitting.
                std::this_thread::sleep_for(std::chrono::seconds(1));
                _session->quit();
                if (_game_result == QUIT_GAME) {
                    throw std::runtime_error("the game ended before the last turn");
                }
                stack()->pop(this);
                break;
        }
    }

  private:
    void init();
    void start();
    void set_delay() {
        _session->set_delay(
                std::chrono::milliseconds(_delay), std::chrono::milliseconds(_jitter), _seed);
    }

    enum State {
        NEW,
        PLAYING,
    };
    State _state = NEW;

    const bool               _is_host;
    const uint16_t           _port;
    const pn::string         _level;
    const uint32_t           _seed;
    const pn::string         _world_log;
    const uint32_t           _turns;
    const int                _delay;
    const int                _jitter;
    ScriptedInputSource      _input_source;
    GameResult               _game_result = NO_GAME;
    unique_ptr<net::Session> _session;
};

void NetSimMaster::init() {
    init_globals();
    sys.audio->set_global_volume(8);
    sys_init();
    Label::init();
    Messages::init();
    InstrumentInit();
    PluginInit(sfz::nullopt);
    SpriteHandlingInit();
    SpaceObjectHandlingInit();  // MUST be after PluginInit()
    Admiral::init();
    Vectors::init();
}

void NetSimMaster::start() {
    if (_is_host) {
        net::Setup setup;
        setup.level       = _level.copy();
        setup.global_seed = static_cast<int32_t>(_seed);
        setup.delay       = net::kDefaultDelay;
        setup.players.emplace_back();
        setup.players[0].name = "host";
        _session = net::Session::host("127.0.0.1", _port, hello("host"), std::move(setup));
        if (!_session) {
            throw std::runtime_error("couldn't host");
        }
        set_delay();
        wait_for("a peer", [this] { return _session->setup().players.size() == 2; });
        _session->start();
    } else {
        _session = net::Session::join("127.0.0.1", _port, hello("join"));
        if (!_session) {
            throw std::runtime_error("couldn't join");
        }
        set_delay();
        wait_for("the host to start", [this] {
            return (_session->state() == net::Session::STARTED) ||
                   (_session->state() == net::Session::CLOSED);
        });
        if (_session->state() == net::Session::CLOSED) {
            throw std::runtime_error(_session->error().c_str());
        }
    }

    net::Setup   setup = _session->setup();
    const Level* level = Level::get(setup.level);
    if (!level || (level->type() != Level::Type::NET) ||
        (level->net.players.size() != setup.players.size())) {
        throw std::runtime_error(pn::format("{0} isn't a two-player level", setup.level).c_str());
    }
    std::vector<NetPlayer> players;
    for (int i = 0; i < setup.players.size(); ++i) {
        NetPlayer player;
        player.name = setup.players[i].name.copy();
        player.race = level->net.players[i].races[0].copy();
        player.hue  = (i == 0) ? Hue::RED : Hue::BLUE;
        players.push_back(std::move(player));
    }
    set_net_players(std::move(players), _session->local());
    set_net_test(_world_log, _turns);
    g.random.seed = setup.global_seed;

    stack()->push(
            new MainPlay(*level, false, &_input_source, false, &_game_result, _session.get()));
}

void usage(pn::output_view out, pn::string_view progname, int retcode) {
    out.format(
            "usage: {0} --host=PORT|--join=PORT [OPTIONS]"
            "\n"
            "\n  Plays one side of a two-player net game over loopback, with scripted input"
            "\n"
            "\n  options:"
            "\n        --host=PORT      host the game on PORT"
            "\n        --join=PORT      join the game on PORT"
            "\n        --level=LEVEL    the net level to play (host only; default: net1)"
            "\n        --seed=SEED      seed for the world and the scripted input (default: 1)"
            "\n        --turns=TURNS    end after this many turns (default: 600)"
            "\n        --delay=MS       hold each packet received this long (default: 0)"
            "\n        --jitter=MS      vary the delay by up to this much (default: 0)"
            "\n        --world-log=FILE write a line per confirmed turn identifying the world"
            "\n        --help           display this help screen"
            "\n",
            progname);
    exit(retcode);
}

void main(int argc, char* const* argv) {
    args::callbacks callbacks;

    sfz::optional<bool>       is_host;
    int                       port   = 0;
    pn::string                level  = "net1";
    int                       seed   = 1;
    int                       turns  = 600;
    int                       delay  = 0;
    int                       jitter = 0;
    sfz::optional<pn::string> world_log;
    callbacks.long_option = [&](pn::string_view                     opt,
                                const args::callbacks::get_value_f& get_value) {
        if ((opt == "host") || (opt == "join")) {
            is_host.emplace(opt == "host");
            sfz::args::integer_option(get_value(), &port);
            return true;
        } else if (opt == "level") {
            level = get_value().copy();
            return true;
        } else if (opt == "seed") {
            sfz::args::integer_option(get_value(), &seed);
            return true;
        } else if (opt == "turns") {
            sfz::args::integer_option(get_value(), &turns);
            return true;
        } else if (opt == "delay") {
            sfz::args::integer_option(get_value(), &delay);
            return true;
        } else if (opt == "jitter") {
            sfz::args::integer_option(get_value(), &jitter);
            return true;
        } else if (opt == "world-log") {
            world_log.emplace(get_value().copy());
            return true;
        } else if (opt == "help") {
            usage(pn::out, sfz::path::basename(argv[0]), 0);
            return true;
        } else {
            return false;
        }
    };

    args::parse(argc - 1, argv + 1, callbacks);
    if (!is_host.has_value() || (port <= 0) || (port > 65535)) {
        throw std::runtime_error("missing --host=PORT or --join=PORT");
    }
    if (!world_log.has_value()) {
        throw std::runtime_error("missing --world-log=FILE");
    }

    Preferences     preferences;
    NullPrefsDriver prefs(preferences.copy());
    EventScheduler  scheduler;
    NullSoundDriver sound;
    NullLedger      ledger;

    TextVideoDriver video({640, 480}, sfz::optional<pn::string>());
    video.loop(
            new NetSimMaster(
                    *is_host, port, level, seed + (*is_host ? 0 : 1), *world_log, turns, delay,
                    jitter),
            scheduler);
}

}  // namespace
}  // namespace antares

int main(int argc, char* const* argv) { return antares::wrap_main(antares::main, argc, argv); }
