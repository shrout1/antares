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

#include "game/globals.hpp"

#include <algorithm>

#include "config/gamepad.hpp"
#include "drawing/color.hpp"
#include "drawing/sprite-handling.hpp"
#include "game/admiral.hpp"
#include "game/input-source.hpp"
#include "game/labels.hpp"
#include "game/minicomputer.hpp"
#include "game/motion.hpp"
#include "game/space-object.hpp"
#include "game/starfield.hpp"
#include "game/vector.hpp"
#include "lang/defines.hpp"
#include "sound/driver.hpp"

namespace antares {

static ANTARES_GLOBAL aresGlobalType* gAresGlobal;

ANTARES_GLOBAL GlobalState& g = head;
ANTARES_GLOBAL GlobalState  head;
ANTARES_GLOBAL GlobalState  tail;

aresGlobalType* globals() { return gAresGlobal; }

bool show_effects() { return gAresGlobal->show_effects; }
bool confirmed() { return gAresGlobal->confirmed; }

template <typename T>
static void copy_array(std::unique_ptr<T[]>* to, const std::unique_ptr<T[]>& from, size_t size) {
    if (from) {
        std::copy(from.get(), from.get() + size, to->get());
    } else {
        to->reset();
    }
}

void copy_world(World* to, const World& from) {
    if (from.admirals && !to->admirals) {
        to->admirals.reset(new Admiral[kMaxPlayerNum]);
    }
    if (from.objects && !to->objects) {
        to->objects.reset(new SpaceObject[kMaxSpaceObject]);
    }
    if (from.vectors && !to->vectors) {
        to->vectors.reset(new Vector[Vector::all().size()]);
    }
    if (from.destinations && !to->destinations) {
        to->destinations.reset(new Destination[kMaxDestObject]);
    }
    if (from.sprites && !to->sprites) {
        to->sprites.reset(new Sprite[Sprite::all().size()]);
    }

    to->sync   = from.sync;
    to->time   = from.time;
    to->random = from.random;
    to->level  = from.level;
    to->angle  = from.angle;
    copy_array(&to->admirals, from.admirals, kMaxPlayerNum);
    to->admiral = from.admiral;
    copy_array(&to->objects, from.objects, kMaxSpaceObject);
    to->ship = from.ship;
    to->root = from.root;
    copy_array(&to->vectors, from.vectors, Vector::all().size());
    copy_array(&to->destinations, from.destinations, kMaxDestObject);
    copy_array(&to->sprites, from.sprites, Sprite::all().size());
    to->initials          = from.initials;
    to->initial_ids       = from.initial_ids;
    to->condition_enabled = from.condition_enabled;
    to->action_queue      = from.action_queue;
    to->game_over         = from.game_over;
    to->game_over_at      = from.game_over_at;
    to->victor            = from.victor;
    to->next_level        = from.next_level;
    if (from.victory_text.has_value()) {
        to->victory_text.emplace(from.victory_text->copy());
    } else {
        to->victory_text.reset();
    }
    to->key_mask = from.key_mask;
    to->pilots   = from.pilots;
}

void init_globals() {
    gAresGlobal = new aresGlobalType;

    g.time     = game_ticks();
    g.ship     = Handle<SpaceObject>(0);
    g.closest  = Handle<SpaceObject>(0);
    g.farthest = Handle<SpaceObject>(0);
}

aresGlobalType::aresGlobalType() {}

aresGlobalType::~aresGlobalType() {}

}  // namespace antares
