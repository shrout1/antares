// Copyright (C) 1997, 1999-2001, 2008 Nathan Lamont
// Copyright (C) 2008-2017 The Antares Authors
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

#include "game/main.hpp"

#include <fcntl.h>
#include <math.h>

#include <algorithm>
#include <cmath>
#include <pn/output>
#include <set>

#include "config/gamepad.hpp"
#include "config/keys.hpp"
#include "config/preferences.hpp"
#include "data/condition.hpp"
#include "data/plugin.hpp"
#include "data/replay.hpp"
#include "data/resource.hpp"
#include "drawing/color.hpp"
#include "drawing/shapes.hpp"
#include "drawing/sprite-handling.hpp"
#include "drawing/text.hpp"
#include "game/action.hpp"
#include "game/admiral.hpp"
#include "game/condition.hpp"
#include "game/cursor.hpp"
#include "game/globals.hpp"
#include "game/input-source.hpp"
#include "game/instruments.hpp"
#include "game/labels.hpp"
#include "game/level.hpp"
#include "game/messages.hpp"
#include "game/minicomputer.hpp"
#include "game/motion.hpp"
#include "game/non-player-ship.hpp"
#include "game/player-ship.hpp"
#include "game/starfield.hpp"
#include "game/sys.hpp"
#include "game/time.hpp"
#include "game/vector.hpp"
#include "lang/defines.hpp"
#include "math/units.hpp"
#include "net/session.hpp"
#include "sound/driver.hpp"
#include "sound/fx.hpp"
#include "sound/music.hpp"
#include "ui/card.hpp"
#include "ui/flows/master.hpp"
#include "ui/interface-handling.hpp"
#include "ui/screens/briefing.hpp"
#include "ui/screens/debriefing.hpp"
#include "ui/screens/help.hpp"
#include "ui/screens/loading.hpp"
#include "ui/screens/play-again.hpp"
#include "video/driver.hpp"

using std::max;
using std::min;
using std::set;
using std::unique_ptr;

namespace antares {

Rect world() { return Rect({0, 0}, sys.video->screen_size()); }

Rect play_screen() {
    const Size size = sys.video->screen_size();
    return Rect(kLeftPanelWidth, 0, size.width - kRightPanelWidth, size.height);
}

namespace {

struct RollbackTest {
    sfz::optional<pn::output> log;
    int                       lookahead = 0;
    uint32_t                  last_turn = 0;
};
ANTARES_GLOBAL RollbackTest rollback_test;

uint32_t world_hash() {
    uint32_t h    = g.sync;
    auto     post = [&h](int32_t x) { h = (h * 16777619) ^ uint32_t(x); };
    for (auto o : SpaceObject::all()) {
        if (!o->active) {
            continue;
        }
        post(o.number());
        post(o->id);
        post(o->location.h);
        post(o->location.v);
        post(o->velocity.h.val());
        post(o->velocity.v.val());
        post(o->direction);
        post(o->health());
        post(o->energy());
        post(o->owner.number());
        post(o->targetObject.number());
        post(o->destObject.number());
    }
    for (auto a : Admiral::all()) {
        post(a->active());
        post(a->cash().amount.val());
        post(a->saveGoal().amount.val());
        post(a->kills());
        post(a->losses());
        post(a->shipsLeft());
        for (int i = 0; i < kAdmiralScoreNum; ++i) {
            post(a->score()[i]);
        }
        post(a->blitzkrieg());
        post(a->flagship().number());
        post(a->considerShip().number());
        post(a->destinationObject().number());
        post(a->hopeToBuild().has_value());
        post(a->totalBuildChance().val());
    }
    for (const auto& p : g.pilots) {
        post(p.keys);
        post(p.gamepad_keys);
        post(p.steer);
        for (const auto& e : p.events) {
            post(static_cast<uint32_t>(e.type));
            post(e.data);
        }
    }
    post(g.key_mask);
    post(g.game_over);
    return h;
}

}  // namespace

void set_rollback_test(pn::string_view log, int lookahead) {
    rollback_test.log.emplace(log, pn::text);
    rollback_test.lookahead = lookahead;
}

void set_net_test(pn::string_view log, uint32_t last_turn) {
    rollback_test.log.emplace(log, pn::text);
    rollback_test.last_turn = last_turn;
}

bool uses_local_conditions(const Level& level) {
    for (const auto& c : level.base.conditions) {
        switch (c.when.type()) {
            case ConditionType::COMPUTER:
            case ConditionType::MESSAGE:
            case ConditionType::ZOOM: return true;
            default: break;
        }
    }
    return false;
}

Rect viewport() {
    const Size size = sys.video->screen_size();
    return Rect(kLeftPanelWidth, 0, size.width - kRightPanelWidth, size.height - g.bottom_border);
}

class GamePlay : public Card {
  public:
    GamePlay(bool replay, InputSource* input, GameResult* game_result, net::Session* session);

    virtual void become_front();
    virtual void resign_front();

    virtual void draw() const;

    virtual bool next_timer(wall_time& time);
    virtual void fire_timer();

    virtual void key_down(const KeyDownEvent& event);
    virtual void key_up(const KeyUpEvent& event);

    virtual void mouse_down(const MouseDownEvent& event);
    virtual void mouse_up(const MouseUpEvent& event);
    virtual void mouse_move(const MouseMoveEvent& event);

    virtual void gamepad_button_down(const GamepadButtonDownEvent& event);
    virtual void gamepad_button_up(const GamepadButtonUpEvent& event);
    virtual void gamepad_stick(const GamepadStickEvent& event);

    // Runs the world alone for `turns`, with input but no effects.
    void run(uint32_t turns);

  private:
    void play_turn();
    void play_net(ticks units);
    void end_net_game(pn::string_view message);

    enum State {
        PLAYING,
        PAUSED,
        PLAY_AGAIN,
        DEBRIEFING,
        HELP,
    };
    State _state;

    // Runs `units` of the game, and updates the display to match.
    void step(ticks units, bool input);
    // Runs `units` of the world only. With `input`, executes the turn's commands; without, each
    // player keeps the controls they hold.
    void simulate(ticks units, bool input);
    void simulate_to(game_ticks at);
    // Updates local display state for `units` of elapsed time.
    void update_display(ticks units);
    void move_stars(ticks units);
    void look_ahead();

    const bool            _replay;
    GameResult* const     _game_result;
    wall_time             _next_timer;
    const Rect            _play_area;
    const bool            _command_and_q;
    bool                  _fast_motion;
    bool                  _player_paused;
    PlayAgainScreen::Item _play_again;
    PlayerShip            _player_ship;
    bool                  _should_draw_sector_lines;
    bool                  _should_draw_site;

    // The wall_time that g.time corresponds to. Under normal operation,
    // this increases in lockstep with g.time, but during fast motion or
    // paused games, it tracks now() without regard for the in-game
    // clock.
    wall_time _real_time;

    InputSource*             _input_source;
    net::Session* const      _session;
    sfz::optional<wall_time> _stalled_since;
    game_ticks               _frontier;             // In net games, the time head has reached.
    uint32_t                 _sampled_through = 0;  // In net games, the last turn of local input.
    double                   _lead            = 0;  // In net games, turns head predicts, smoothed.
    ticks                    _since_slowed    = ticks(0);
};

uint32_t sim_fingerprint() {
    const uint32_t kTurns = 200;
    const int32_t  kSeed  = 1;

    uint32_t h = 0;
    for (const auto& kv : plug.levels) {
        const Level& level = kv.second;
        if ((level.type() != Level::Type::NET) ||
            (level.net.players.size() != net::kSessionPlayers)) {
            continue;
        }
        std::vector<NetPlayer> players;
        for (int i = 0; i < level.net.players.size(); ++i) {
            NetPlayer player;
            player.race = level.net.players[i].races[0].copy();
            player.hue  = (i == 0) ? Hue::RED : Hue::BLUE;
            players.push_back(std::move(player));
        }
        set_net_players(std::move(players), 0);
        g.random.seed = kSeed;
        RemoveAllSpaceObjects();
        g.game_over = false;
        LoadState s = start_construct_level(level);
        while (!s.done) {
            construct_level(&s);
        }
        set_up_instruments();

        ScriptedInputSource input(kSeed);
        GameResult          result = NO_GAME;
        GamePlay(true, &input, &result, nullptr).run(kTurns);
        h = (h * 16777619) ^ world_hash();
    }
    g.game_over = true;
    return h;
}

MainPlay::MainPlay(
        const Level& level, bool replay, InputSource* input, bool show_loading_screen,
        GameResult* game_result, net::Session* session)
        : _state(NEW),
          _level(level),
          _replay(replay),
          _show_loading_screen(show_loading_screen),
          _cancelled(false),
          _game_result(game_result),
          _input_source(input),
          _session(session) {}

void MainPlay::become_front() {
    switch (_state) {
        case NEW: {
            _state = LOADING;
            RemoveAllSpaceObjects();
            g.game_over = false;

            // _replay_builder.init(
            //         sys.prefs->scenario_identifier(),
            //         String(u32_to_version(plug.meta.version)),
            //         *_level->chapter,
            //         g.random.seed);

            sys.music.play(Music::IDLE, Music::briefing_song);

            if (_show_loading_screen) {
                stack()->push(new LoadingScreen(_level, &_cancelled));
                break;
            } else {
                LoadState s = start_construct_level(_level);
                while (!s.done) {
                    construct_level(&s);
                }
            }
        }
            // fall through.

        case LOADING: {
            if (_cancelled) {
                *_game_result = QUIT_GAME;
                stack()->pop(this);
                return;
            }
            if (!_replay) {
                _state = BRIEFING;
                stack()->push(new BriefingScreen(_level, &_cancelled));
                break;
            }
        }
            // fall through

        case BRIEFING: {
            sys.music.stop();

            if (_cancelled) {
                *_game_result = QUIT_GAME;
                stack()->pop(this);
                break;
            }

            _state = PLAYING;

            set_up_instruments();

            if (g.level->base.song.has_value()) {
                sys.music.play(Music::IN_GAME, *g.level->base.song);
            }

            stack()->push(new GamePlay(_replay, _input_source, _game_result, _session));
        } break;

        case PLAYING:
            globals()->transitions.reset();
            sys.sound.stop();
            sys.music.stop();
            stack()->pop(this);
            break;
    }
}

static uint32_t   turn_at(game_ticks at) { return at.time_since_epoch() / kMajorTick; }
static game_ticks turn_start(uint32_t turn) { return game_ticks(turn * kMajorTick); }

// How many turns head may run ahead of the input every player has sent.
const uint32_t kRollbackTurns = 20;

// A node that predicts more turns than the other player's does is running ahead of it, so it drops
// one tick in every kSlowEvery until the two are even.  Leads are smoothed over kLeadWindow.
const ticks kSlowEvery  = ticks(100);
const ticks kLeadWindow = ticks(60);

static net::Rollback::Turn to_turn(const std::vector<PlayerEvent>& events) {
    net::Rollback::Turn turn;
    for (const PlayerEvent& e : events) {
        turn.emplace_back();
        turn.back().type = static_cast<uint32_t>(e.type);
        turn.back().data = e.data;
    }
    return turn;
}

static std::vector<PlayerEvent> from_turn(const net::Rollback::Turn& turn) {
    std::vector<PlayerEvent> events;
    for (const net::Turns::Event& e : turn) {
        events.push_back(PlayerEvent{static_cast<PlayerEventType>(e.type), e.data});
    }
    return events;
}

GamePlay::GamePlay(bool replay, InputSource* input, GameResult* game_result, net::Session* session)
        : _state(PLAYING),
          _replay(replay),
          _game_result(game_result),
          _next_timer(now() + kMinorTick),
          _play_area(viewport().left, viewport().top, viewport().right, viewport().bottom),
          _command_and_q(BothCommandAndQ()),
          _fast_motion(false),
          _player_paused(false),
          _real_time(now()),
          _input_source(input),
          _session(session) {}

static const usecs kSwitchAfter = usecs(1000000 / 3);  // TODO(sfiera): ticks(20)
static const usecs kSleepAfter  = secs(60);

class PauseScreen : public Card {
  public:
    PauseScreen() {
        _pause_string = Messages::pause_string().copy();
        int32_t width = sys.fonts.title.string_width(_pause_string);
        Rect    bounds(0, 0, width, sys.fonts.title.height);
        bounds.center_in(play_screen());
        _text_origin = Point(bounds.left, bounds.top + sys.fonts.title.ascent);

        bounds.inset(-4, -4);
        _bracket_bounds = bounds;
    }

    virtual void become_front() {
        // TODO(sfiera): cancel any active transition.
        sys.sound.pause();
        _visible     = true;
        _asleep      = false;
        _next_switch = now() + kSwitchAfter;
        _sleep_at    = now() + kSleepAfter;
    }

    virtual void mouse_up(const MouseUpEvent& event) { wake(); }
    virtual void mouse_down(const MouseDownEvent& event) { wake(); }
    virtual void mouse_move(const MouseMoveEvent& event) { wake(); }
    virtual void key_down(const KeyDownEvent& event) { wake(); }

    virtual void key_up(const KeyUpEvent& event) {
        wake();
        if (event.key() == Key::CAPS_LOCK) {
            stack()->pop(this);
        }
    }

    virtual bool next_timer(wall_time& time) {
        if (!_asleep) {
            time = std::min(_next_switch, _sleep_at);
            return true;
        }
        return false;
    }

    virtual void fire_timer() { show_hide(); }

    virtual void draw() const {
        next()->draw();
        if (_visible) {
            const RgbColor& light_green = GetRGBTranslateColorShade(Hue::GREEN, LIGHTER);
            const RgbColor& dark_green  = GetRGBTranslateColorShade(Hue::GREEN, DARKER);

            {
                Rects rects;
                for (int32_t y = _bracket_bounds.top + 2; y < _bracket_bounds.bottom; y += 2) {
                    rects.fill(
                            {_bracket_bounds.left, y, _bracket_bounds.right, y + 1}, dark_green);
                }
                draw_vbracket(rects, _bracket_bounds, light_green);
            }

            sys.fonts.title.draw(_text_origin, _pause_string, light_green);
        }
        if (_asleep) {
            Rects().fill(world(), rgba(0, 0, 0, 63));
        }
    }

  private:
    void show_hide() {
        const wall_time now = antares::now();
        if (_sleep_at < now) {
            _asleep = _visible = true;
        } else {
            while (_next_switch < now) {
                _visible = !_visible;
                _next_switch += kSwitchAfter;
            }
        }
    }

    void wake() {
        _asleep   = false;
        _sleep_at = now() + kSleepAfter;
    }

    bool       _visible;
    bool       _asleep;
    wall_time  _next_switch;
    wall_time  _sleep_at;
    pn::string _pause_string;
    Point      _text_origin;
    Rect       _bracket_bounds;
};

void GamePlay::become_front() {
    switch (_state) {
        case PLAYING:
            _input_source->start();
            if (_session) {
                _session->ready(turn_at(g.time) + 1);
                copy_world(&tail, g);
                _frontier        = g.time;
                _sampled_through = turn_at(g.time);
            }
            if (_replay) {
                _player_ship.cursor().show = false;
            } else {
                _player_ship.cursor().show = true;
            }
            HintLine::reset();

            CheckLevelConditions();
            break;

        case PAUSED:
        case HELP: _state = PLAYING; break;

        case PLAY_AGAIN:
            switch (_play_again) {
                case PlayAgainScreen::QUIT:
                    if (_session) {
                        _session->quit();
                    }
                    *_game_result  = QUIT_GAME;
                    g.game_over    = true;
                    g.next_level   = nullptr;
                    g.victory_text = sfz::nullopt;
                    stack()->pop(this);
                    break;

                case PlayAgainScreen::RESTART:
                    *_game_result  = RESTART_GAME;
                    g.game_over    = true;
                    g.next_level   = nullptr;
                    g.victory_text = sfz::nullopt;
                    stack()->pop(this);
                    break;

                case PlayAgainScreen::RESUME: _state = PLAYING; break;

                case PlayAgainScreen::SKIP:
                    *_game_result  = WIN_GAME;
                    g.game_over    = true;
                    g.victor       = g.admiral;
                    g.next_level   = g.level->solo.skip->get();
                    g.victory_text = sfz::nullopt;
                    stack()->pop(this);
                    break;

                default:
                    throw std::runtime_error(
                            pn::format("invalid play again result {0}", stringify(_play_again))
                                    .c_str());
            }
            break;

        case DEBRIEFING:
            if (_session || (*_game_result == WIN_GAME)) {
                stack()->pop(this);
            } else {  // LOSE_GAME
                _state = PLAY_AGAIN;
                stack()->push(new PlayAgainScreen(false, false, &_play_again));
            }
            break;
    }
}

void GamePlay::resign_front() { minicomputer_cancel(); }

void GamePlay::draw() const {
    globals()->starfield.draw();
    if (_should_draw_sector_lines) {
        draw_sector_lines();
    }
    Vectors::draw();
    draw_sprites();
    Label::draw();

    Messages::draw_message();
    if (_should_draw_site) {
        draw_site(_player_ship);
    }
    draw_instruments();
    if (stack()->top() == this) {
        _player_ship.cursor().draw();
    }
    HintLine::draw();
    globals()->transitions.draw();
    if (_stalled_since.has_value() && (now() >= (*_stalled_since + secs(1)))) {
        pn::string_view text = "Waiting for the other player";
        Point           at(
                play_screen().center().h - (sys.fonts.title.string_width(text) / 2),
                play_screen().center().v);
        sys.fonts.title.draw(at, text, GetRGBTranslateColorShade(Hue::GREEN, LIGHTER));
    }
}

bool GamePlay::next_timer(wall_time& time) {
    if (_state == PLAYING) {
        time = _next_timer;
        return true;
    }
    return false;
}

void GamePlay::fire_timer() {
    while (_next_timer < now()) {
        _next_timer = _next_timer + kMinorTick;
    }

    ticks     unitsPassed = ticks(0);
    wall_time new_now     = now();
    while (_real_time <= (new_now - kMinorTick)) {
        unitsPassed += kMinorTick;
        _real_time += kMinorTick;
    }

    if (_fast_motion && !_player_ship.entering_message()) {
        unitsPassed *= 12;
        _real_time = now();
    }

    if (unitsPassed <= ticks(0)) {
        return;
    }

    EraseSite();

    if (_player_paused) {
        _player_paused = false;
        unitsPassed    = ticks(0);
        _real_time     = now();
    }

    if (_session) {
        for (const pn::string& message : _player_ship.take_messages()) {
            _session->chat(message);
            Messages::add(pn::format("{0}: {1}", g.admiral->name(), message));
        }
        for (const pn::string& message : _session->take_chats()) {
            Messages::add(message);
        }
        if (_session->desynced()) {
            _session->quit();
            end_net_game("The game is out of sync.");
            return;
        } else if (_session->state() == net::Session::CLOSED) {
            if (!tail.game_over) {
                end_net_game(_session->error());
                return;
            }
            tail.game_over_at = tail.time;
        }
        play_net(unitsPassed);
    }

    while (!_session && (unitsPassed > ticks(0))) {
        ticks unitsToDo   = unitsPassed;
        ticks minor_ticks = g.time.time_since_epoch() % kMajorTick;
        if (minor_ticks + unitsToDo > kMajorTick) {
            unitsToDo = kMajorTick - minor_ticks;
        }

        if ((minor_ticks == ticks(0)) && rollback_test.lookahead) {
            look_ahead();
        }
        step(unitsToDo, true);
        if (rollback_test.log.has_value() &&
            ((g.time.time_since_epoch() % kMajorTick) == ticks(0))) {
            rollback_test.log->format(
                    "{0}\t{1}\n", g.time.time_since_epoch().count(), world_hash());
        }
        unitsPassed -= unitsToDo;
    }

    // In net games, only what every player's input confirms can end the game.
    const World& world = _session ? static_cast<const World&>(tail) : g;
    if (world.game_over && (world.time >= world.game_over_at)) {
        if (*_game_result == NO_GAME) {
            if (world.victor == g.admiral) {
                *_game_result = WIN_GAME;
            } else {
                *_game_result = LOSE_GAME;
            }
        }
    }

    switch (*_game_result) {
        case QUIT_GAME:
        case RESTART_GAME: stack()->pop(this); break;

        case WIN_GAME:
            if (_replay || !world.victory_text.has_value()) {
                stack()->pop(this);
            } else {
                _state        = DEBRIEFING;
                const auto& a = g.admiral;
                switch (g.level->type()) {
                    case Level::Type::SOLO:
                        stack()->push(new DebriefingScreen(
                                *g.victory_text, g.time, g.level->solo.par.time, GetAdmiralLoss(a),
                                g.level->solo.par.losses, GetAdmiralKill(a),
                                g.level->solo.par.kills));
                        break;

                    default: stack()->push(new DebriefingScreen(*world.victory_text)); break;
                }
            }
            break;

        case LOSE_GAME:
            if (_replay) {
                *_game_result = QUIT_GAME;
                stack()->pop(this);
            } else if (_session && !world.victory_text.has_value()) {
                stack()->pop(this);
            } else if (!world.victory_text.has_value()) {
                _state = PLAY_AGAIN;
                stack()->push(new PlayAgainScreen(false, false, &_play_again));
            } else {
                _state = DEBRIEFING;
                stack()->push(new DebriefingScreen(*world.victory_text));
            }
            break;

        case NO_GAME:
            // Continue playing.
            break;
    }
}

void GamePlay::step(ticks units, bool input) {
    move_stars(units);
    simulate(units, input);
    update_display(units);
}

void GamePlay::simulate(ticks units, bool input) {
    globals()->show_effects = (g.time + units) > globals()->shown_through;

    MoveSpaceObjects(units);

    g.time += units;

    if ((g.time.time_since_epoch() % kMajorTick) == ticks(0)) {
        // everything in here gets executed once every major tick
        _player_paused = false;

        NonplayerShipThink();
        AdmiralThink();
        execute_action_queue();

        if (input) {
            play_turn();
        } else {
            _player_ship.execute(g.admiral, {});
        }

        CollideSpaceObjects();
        if ((g.time.time_since_epoch() % kConditionTick) == ticks(0)) {
            CheckLevelConditions();
        }
    }

    CullSprites();
    Vectors::cull();
    globals()->shown_through = std::max(globals()->shown_through, g.time);
}

void GamePlay::move_stars(ticks units) {
    globals()->starfield.prepare_to_move();
    globals()->starfield.move(units);
}

void GamePlay::update_display(ticks units) {
    UpdateMiniScreenLines();

    if (confirmed()) {
        Messages::clip();
        Messages::draw_long_message(units);
    }

    _should_draw_sector_lines = update_sector_lines();
    Vectors::update();
    Label::update_positions(units);
    Label::update_contents(units);
    _should_draw_site = update_site();

    Label::show_all();
    globals()->starfield.show();

    if (confirmed()) {
        Messages::draw_message_screen(units);
    }
    UpdateRadar(units);
    globals()->transitions.update_boolean(units);
}

void GamePlay::look_ahead() {
    static World ahead;
    copy_world(&ahead, g);
    // The steps ahead play their own sounds, and the steps that replace them stay silent, so the
    // sound log means nothing with a look-ahead.
    globals()->confirmed = false;
    for (auto& p : g.pilots) {
        p.keys ^= kUpKey | kLeftKey | kPulseKey;  // Look ahead with other controls held.
    }
    for (int i = 0; (i < rollback_test.lookahead) && !g.game_over; ++i) {
        step(kMajorTick, false);
    }
    globals()->confirmed = true;
    copy_world(&g, ahead);
}

void GamePlay::play_turn() {
    uint32_t turn = turn_at(g.time);
    if (!_session || (turn > _sampled_through)) {
        if (!_input_source->get(g.admiral, g.time, _player_ship)) {
            g.game_over    = true;
            g.game_over_at = g.time;
        }
    }
    if (!_session) {
        _player_ship.execute(g.admiral, _player_ship.sample());
        return;
    }

    if (turn > _sampled_through) {
        _session->submit(turn, to_turn(_player_ship.sample()));
        _sampled_through = turn;
    }
    for (int i = 0; i < net::kSessionPlayers; ++i) {
        // A player whose input hasn't arrived keeps holding the same controls.
        sfz::optional<net::Rollback::Turn> commands = _session->known(i, turn);
        _player_ship.execute(
                Handle<Admiral>(i),
                commands.has_value() ? from_turn(*commands) : std::vector<PlayerEvent>{});
    }
}

// Advances tail through each turn that every player's input has reached, then predicts head from
// tail to `units` past where head was.
void GamePlay::play_net(ticks units) {
    double lead = static_cast<double>(turn_at(_frontier)) - _session->confirmed();
    _lead += (lead - _lead) *
             std::min(1.0, static_cast<double>(units.count()) / kLeadWindow.count());
    _session->set_lead(static_cast<int32_t>(std::lround(_lead)));
    if ((_lead - _session->peer_lead()) > 1) {
        _since_slowed += units;
        if (_since_slowed >= kSlowEvery) {
            _since_slowed -= kSlowEvery;
            units -= kMinorTick;
        }
    }

    game_ticks target = _frontier + units;
    game_ticks limit  = turn_start(_session->confirmed() + kRollbackTurns);
    if (target > limit) {
        target = std::max(limit, _frontier);
        if (!_stalled_since.has_value()) {
            _stalled_since.emplace(now());
        }
        _real_time = now();
    } else {
        _stalled_since = sfz::nullopt;
    }
    units = target - _frontier;
    if (units <= ticks(0)) {
        return;
    }

    move_stars(units);

    copy_world(&g, tail);
    while (turn_at(g.time) < _session->confirmed()) {
        game_ticks next = turn_start(turn_at(g.time) + 1);
        if (next > target) {
            break;
        }
        simulate_to(next);
        _session->sync(turn_at(g.time), g.sync);
        _session->confirm(turn_at(g.time));
        if (rollback_test.log.has_value()) {
            rollback_test.log->format(
                    "{0}\t{1}\n", g.time.time_since_epoch().count(), world_hash());
        }
        if (rollback_test.last_turn && (turn_at(g.time) >= rollback_test.last_turn)) {
            g.game_over    = true;
            g.game_over_at = g.time;
            break;
        }
    }
    copy_world(&tail, g);

    globals()->confirmed = false;
    simulate_to(target);
    globals()->confirmed = true;
    _frontier            = target;

    update_display(units);
}

// One minor tick at a time, so that every player's world runs the same steps however their frames
// fall.
void GamePlay::simulate_to(game_ticks at) {
    while (g.time < at) {
        simulate(kMinorTick, true);
    }
}

void GamePlay::run(uint32_t turns) {
    _input_source->start();
    globals()->confirmed     = false;
    globals()->shown_through = game_ticks::max();
    CheckLevelConditions();
    simulate_to(turn_start(turn_at(g.time) + turns));
    globals()->confirmed = true;
}

void GamePlay::end_net_game(pn::string_view message) {
    *_game_result = QUIT_GAME;
    if (rollback_test.last_turn) {
        // No one is there to dismiss the debriefing.
        pn::err.format("{0}\n", message);
        stack()->pop(this);
        return;
    }
    _state = DEBRIEFING;
    stack()->push(new DebriefingScreen(message));
}

void GamePlay::key_down(const KeyDownEvent& event) {
    if (_session &&
        ((event.key() == Key::CAPS_LOCK) || (event.key() == sys.prefs->key(kHelpKeyNum)) ||
         (event.key() == sys.prefs->key(kFastMotionKeyNum)))) {
        return;
    }
    switch (event.key().value()) {
        case Key::CAPS_LOCK:
            _state         = PAUSED;
            _player_paused = true;
            stack()->push(new PauseScreen);
            return;

        case Key::ESCAPE:
            if (_replay) {
                break;
            } else {
                _state         = PLAY_AGAIN;
                _player_paused = true;
                stack()->push(new PlayAgainScreen(
                        true,
                        (g.level->type() == Level::Type::SOLO) && g.level->solo.skip.has_value(),
                        &_play_again));
                return;
            }

        default:
            if (event.key() == sys.prefs->key(kHelpKeyNum)) {
                if (_replay) {
                    break;
                } else {
                    _state         = HELP;
                    _player_paused = true;
                    stack()->push(new HelpScreen);
                    return;
                }
            } else if (event.key() == sys.prefs->key(kVolumeDownKeyNum)) {
                sys.prefs->set_volume(sys.prefs->volume());
                sys.audio->set_global_volume(sys.prefs->volume());
                return;
            } else if (event.key() == sys.prefs->key(kVolumeUpKeyNum)) {
                sys.prefs->set_volume(sys.prefs->volume() + 1);
                sys.audio->set_global_volume(sys.prefs->volume());
                return;
            } else if (event.key() == sys.prefs->key(kActionMusicKeyNum)) {
                if (sys.prefs->play_music_in_game()) {
                    sys.music.toggle();
                }
                return;
            } else if (event.key() == sys.prefs->key(kFastMotionKeyNum)) {
                _fast_motion = true;
                return;
            }
    }

    _input_source->key_down(event);
}

void GamePlay::key_up(const KeyUpEvent& event) {
    if (event.key() == sys.prefs->key(kFastMotionKeyNum)) {
        _fast_motion = false;
        return;
    }

    _input_source->key_up(event);
}

void GamePlay::mouse_down(const MouseDownEvent& event) { _input_source->mouse_down(event); }

void GamePlay::mouse_up(const MouseUpEvent& event) { _input_source->mouse_up(event); }

void GamePlay::mouse_move(const MouseMoveEvent& event) { _input_source->mouse_move(event); }

void GamePlay::gamepad_button_down(const GamepadButtonDownEvent& event) {
    switch (event.button) {
        case Gamepad::Button::START:
            if (_replay) {
                break;
            } else {
                _state         = PLAY_AGAIN;
                _player_paused = true;
                stack()->push(new PlayAgainScreen(
                        true,
                        (g.level->type() == Level::Type::SOLO) && g.level->solo.skip.has_value(),
                        &_play_again));
                return;
            }
        default: break;
    }

    _input_source->gamepad_button_down(event);
}

void GamePlay::gamepad_button_up(const GamepadButtonUpEvent& event) {
    _input_source->gamepad_button_up(event);
}

void GamePlay::gamepad_stick(const GamepadStickEvent& event) {
    _input_source->gamepad_stick(event);
}

}  // namespace antares
