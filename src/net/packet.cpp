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

#include "net/packet.hpp"

#include <stdexcept>

namespace antares {
namespace net {

namespace {

enum {
    VARINT           = 0,
    FIXED64          = 1,
    LENGTH_DELIMITED = 2,
    FIXED32          = 5,
};

void put_varint(pn::data* out, uint64_t value) {
    do {
        uint8_t byte = value & 0x7f;
        value >>= 7;
        if (value) {
            byte |= 0x80;
        }
        *out += pn::data_view{&byte, 1};
    } while (value);
}

void put_uint(pn::data* out, int field, uint64_t value) {
    put_varint(out, (field << 3) | VARINT);
    put_varint(out, value);
}

void put_int(pn::data* out, int field, int32_t value) {
    put_uint(out, field, static_cast<uint64_t>(static_cast<int64_t>(value)));
}

void put_bytes(pn::data* out, int field, pn::data_view bytes) {
    put_varint(out, (field << 3) | LENGTH_DELIMITED);
    put_varint(out, bytes.size());
    *out += bytes;
}

void put_string(pn::data* out, int field, pn::string_view s) {
    put_bytes(out, field, s.as_data());
}

// Rejects overlong forms, surrogates, and code points past U+10FFFF.
bool valid_utf8(pn::data_view d) {
    size_t i = 0;
    while (i < d.size()) {
        uint8_t  c = d[i];
        size_t   n;
        uint32_t rune, min;
        if (c < 0x80) {
            ++i;
            continue;
        } else if ((c & 0xe0) == 0xc0) {
            n = 1, rune = c & 0x1f, min = 0x80;
        } else if ((c & 0xf0) == 0xe0) {
            n = 2, rune = c & 0x0f, min = 0x800;
        } else if ((c & 0xf8) == 0xf0) {
            n = 3, rune = c & 0x07, min = 0x10000;
        } else {
            return false;
        }
        if (d.size() - i <= n) {
            return false;
        }
        for (size_t j = 1; j <= n; ++j) {
            if ((d[i + j] & 0xc0) != 0x80) {
                return false;
            }
            rune = (rune << 6) | (d[i + j] & 0x3f);
        }
        if ((rune < min) || (rune > 0x10ffff) || ((0xd800 <= rune) && (rune <= 0xdfff))) {
            return false;
        }
        i += n + 1;
    }
    return true;
}

class Reader {
  public:
    explicit Reader(pn::data_view d) : _p(d.data()), _end(d.data() + d.size()) {}

    bool done() const { return _p == _end; }

    bool varint(uint64_t* value) {
        *value = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (_p == _end) {
                return false;
            }
            uint8_t byte = *(_p++);
            *value |= static_cast<uint64_t>(byte & 0x7f) << shift;
            if (!(byte & 0x80)) {
                return true;
            }
        }
        return false;
    }

    bool bytes(pn::data_view* d) {
        uint64_t size;
        if (!varint(&size) || (size > static_cast<uint64_t>(_end - _p))) {
            return false;
        }
        *d = pn::data_view{_p, static_cast<pn::data_view::size_type>(size)};
        _p += size;
        return true;
    }

    bool skip(int wire) {
        uint64_t       value;
        pn::data_view  d;
        const uint8_t* p = _p;
        switch (wire) {
            case VARINT: return varint(&value);
            case LENGTH_DELIMITED: return bytes(&d);
            case FIXED64: p += 8; break;
            case FIXED32: p += 4; break;
            default: return false;
        }
        if (p > _end) {
            return false;
        }
        _p = p;
        return true;
    }

  private:
    const uint8_t* _p;
    const uint8_t* _end;
};

// Calls `f(field, wire, reader)` for each field in `d`; `f` returns false if the field is
// malformed.
template <typename F>
bool read_fields(pn::data_view d, F f) {
    Reader r(d);
    while (!r.done()) {
        uint64_t key;
        if (!r.varint(&key) || !f(static_cast<int>(key >> 3), static_cast<int>(key & 7), &r)) {
            return false;
        }
    }
    return true;
}

bool get(Reader* r, int wire, uint64_t* value) { return (wire == VARINT) && r->varint(value); }

bool get(Reader* r, int wire, uint32_t* value) {
    uint64_t u;
    if (!get(r, wire, &u)) {
        return false;
    }
    *value = static_cast<uint32_t>(u);
    return true;
}

bool get(Reader* r, int wire, int32_t* value) {
    uint64_t u;
    if (!get(r, wire, &u)) {
        return false;
    }
    *value = static_cast<int32_t>(u);
    return true;
}

bool get(Reader* r, int wire, pn::string* s) {
    pn::data_view d;
    if ((wire != LENGTH_DELIMITED) || !r->bytes(&d) || !valid_utf8(d)) {
        return false;
    }
    *s = d.as_string().copy();
    return true;
}

pn::data write(const Hello& h) {
    pn::data out;
    put_uint(&out, 1, h.protocol);
    put_string(&out, 2, h.antares_version);
    put_string(&out, 3, h.plugin_identifier);
    put_string(&out, 4, h.plugin_version);
    put_string(&out, 5, h.name);
    return out;
}

bool read(pn::data_view d, Hello* h) {
    return read_fields(d, [h](int field, int wire, Reader* r) {
        switch (field) {
            case 1: return get(r, wire, &h->protocol);
            case 2: return get(r, wire, &h->antares_version);
            case 3: return get(r, wire, &h->plugin_identifier);
            case 4: return get(r, wire, &h->plugin_version);
            case 5: return get(r, wire, &h->name);
            default: return r->skip(wire);
        }
    });
}

pn::data write(const Welcome& w) {
    pn::data out;
    put_int(&out, 1, w.player);
    return out;
}

bool read(pn::data_view d, Welcome* w) {
    return read_fields(d, [w](int field, int wire, Reader* r) {
        switch (field) {
            case 1: return get(r, wire, &w->player);
            default: return r->skip(wire);
        }
    });
}

pn::data write(const Reject& j) {
    pn::data out;
    put_uint(&out, 1, j.reason);
    return out;
}

bool read(pn::data_view d, Reject* j) {
    return read_fields(d, [j](int field, int wire, Reader* r) {
        uint32_t reason;
        switch (field) {
            case 1:
                if (!get(r, wire, &reason)) {
                    return false;
                }
                j->reason = static_cast<Reject::Reason>(reason);
                return true;
            default: return r->skip(wire);
        }
    });
}

pn::data write(const Setup::Player& p) {
    pn::data out;
    put_string(&out, 1, p.name);
    put_string(&out, 2, p.race);
    put_int(&out, 3, p.hue);
    return out;
}

bool read(pn::data_view d, Setup::Player* p) {
    return read_fields(d, [p](int field, int wire, Reader* r) {
        switch (field) {
            case 1: return get(r, wire, &p->name);
            case 2: return get(r, wire, &p->race);
            case 3: return get(r, wire, &p->hue);
            default: return r->skip(wire);
        }
    });
}

pn::data write(const Setup& s) {
    pn::data out;
    put_int(&out, 1, s.chapter);
    put_int(&out, 2, s.global_seed);
    put_uint(&out, 3, s.delay);
    for (const Setup::Player& p : s.players) {
        put_bytes(&out, 4, write(p));
    }
    return out;
}

bool read(pn::data_view d, Setup* s) {
    return read_fields(d, [s](int field, int wire, Reader* r) {
        pn::data_view sub;
        switch (field) {
            case 1: return get(r, wire, &s->chapter);
            case 2: return get(r, wire, &s->global_seed);
            case 3: return get(r, wire, &s->delay);
            case 4:
                s->players.emplace_back();
                return (wire == LENGTH_DELIMITED) && r->bytes(&sub) &&
                       read(sub, &s->players.back());
            default: return r->skip(wire);
        }
    });
}

pn::data write(const Turns::Event& e) {
    pn::data out;
    put_uint(&out, 1, e.type);
    put_int(&out, 2, e.data);
    return out;
}

bool read(pn::data_view d, Turns::Event* e) {
    return read_fields(d, [e](int field, int wire, Reader* r) {
        switch (field) {
            case 1: return get(r, wire, &e->type);
            case 2: return get(r, wire, &e->data);
            default: return r->skip(wire);
        }
    });
}

pn::data write(const std::vector<Turns::Event>& turn) {
    pn::data out;
    for (const Turns::Event& e : turn) {
        put_bytes(&out, 1, write(e));
    }
    return out;
}

bool read(pn::data_view d, std::vector<Turns::Event>* turn) {
    return read_fields(d, [turn](int field, int wire, Reader* r) {
        pn::data_view sub;
        switch (field) {
            case 1:
                turn->emplace_back();
                return (wire == LENGTH_DELIMITED) && r->bytes(&sub) && read(sub, &turn->back());
            default: return r->skip(wire);
        }
    });
}

pn::data write(const Turns& t) {
    pn::data out;
    put_int(&out, 1, t.player);
    put_uint(&out, 2, t.first);
    for (const std::vector<Turns::Event>& turn : t.turns) {
        put_bytes(&out, 3, write(turn));
    }
    put_uint(&out, 4, t.ack);
    put_uint(&out, 5, t.sync_turn);
    put_uint(&out, 6, t.sync);
    return out;
}

bool read(pn::data_view d, Turns* t) {
    return read_fields(d, [t](int field, int wire, Reader* r) {
        pn::data_view sub;
        switch (field) {
            case 1: return get(r, wire, &t->player);
            case 2: return get(r, wire, &t->first);
            case 3:
                t->turns.emplace_back();
                return (wire == LENGTH_DELIMITED) && r->bytes(&sub) && read(sub, &t->turns.back());
            case 4: return get(r, wire, &t->ack);
            case 5: return get(r, wire, &t->sync_turn);
            case 6: return get(r, wire, &t->sync);
            default: return r->skip(wire);
        }
    });
}

pn::data write(const Chat& c) {
    pn::data out;
    put_int(&out, 1, c.player);
    put_string(&out, 2, c.text);
    return out;
}

bool read(pn::data_view d, Chat* c) {
    return read_fields(d, [c](int field, int wire, Reader* r) {
        switch (field) {
            case 1: return get(r, wire, &c->player);
            case 2: return get(r, wire, &c->text);
            default: return r->skip(wire);
        }
    });
}

pn::data write(const Quit& q) {
    pn::data out;
    put_int(&out, 1, q.player);
    return out;
}

bool read(pn::data_view d, Quit* q) {
    return read_fields(d, [q](int field, int wire, Reader* r) {
        switch (field) {
            case 1: return get(r, wire, &q->player);
            default: return r->skip(wire);
        }
    });
}

bool read_empty(pn::data_view d) {
    return read_fields(d, [](int field, int wire, Reader* r) { return r->skip(wire); });
}

}  // namespace

pn::data encode(const Packet& packet) {
    pn::data out;
    switch (packet.type) {
        case Packet::HELLO: put_bytes(&out, packet.type, write(packet.hello)); break;
        case Packet::WELCOME: put_bytes(&out, packet.type, write(packet.welcome)); break;
        case Packet::REJECT: put_bytes(&out, packet.type, write(packet.reject)); break;
        case Packet::SETUP: put_bytes(&out, packet.type, write(packet.setup)); break;
        case Packet::START: put_bytes(&out, packet.type, pn::data{}); break;
        case Packet::TURNS: put_bytes(&out, packet.type, write(packet.turns)); break;
        case Packet::CHAT: put_bytes(&out, packet.type, write(packet.chat)); break;
        case Packet::QUIT: put_bytes(&out, packet.type, write(packet.quit)); break;
        case Packet::NONE: throw std::runtime_error("can't encode a packet with no type");
    }
    return out;
}

bool decode(pn::data_view bytes, Packet* packet) {
    *packet = Packet{};
    bool ok = read_fields(bytes, [packet](int field, int wire, Reader* r) {
        if ((field < Packet::HELLO) || (Packet::QUIT < field)) {
            return r->skip(wire);
        }
        pn::data_view body;
        if ((wire != LENGTH_DELIMITED) || !r->bytes(&body)) {
            return false;
        }
        packet->type = static_cast<Packet::Type>(field);
        switch (packet->type) {
            case Packet::HELLO: return read(body, &packet->hello);
            case Packet::WELCOME: return read(body, &packet->welcome);
            case Packet::REJECT: return read(body, &packet->reject);
            case Packet::SETUP: return read(body, &packet->setup);
            case Packet::START: return read_empty(body);
            case Packet::TURNS: return read(body, &packet->turns);
            case Packet::CHAT: return read(body, &packet->chat);
            case Packet::QUIT: return read(body, &packet->quit);
            case Packet::NONE: return false;
        }
        return false;
    });
    return ok && (packet->type != Packet::NONE);
}

}  // namespace net
}  // namespace antares
