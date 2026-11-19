// Reads and writes host save files containing game and extension state.
#include "game/save_file.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "game/save_slots.h"
#include "game/text.h"
#include "game/text_render.h"

namespace sawgod::game {

namespace {

// The payload is the game state block at SS:0xD000 (symbols/ram.toml names its fields); offsets here are
// from its start. Four party-member records of 31 bytes come first.
constexpr size_t kMemberStride = 0x1F, kQueues = 0x105, kQueueStride = 0xCC;
constexpr int kMembers = 4;
constexpr size_t kMagicAt = 0x435; // the two "valid save" bytes (0x1B, 0xE4), set when a file loads

enum class Kind {
    U8,    // a number
    Hex8,  // a number written in hex (bit fields)
    U16,   // little-endian
    U32,   // little-endian
    Name,  // 4 characters in the game's charset
    List,  // `count` numbers
    Pairs, // `count` [item id, uses] pairs (255 = empty)
    Quads, // `count` [map, x, y, state] records
    Hex,   // `count` bytes as a hex string (spans not decoded yet)
};

struct Field {
    const char *section; // "" = top level, "member" = per member (offset within its record)
    const char *key;
    size_t at;
    Kind kind;
    size_t count = 1;
    const char *comment = nullptr;
};

constexpr size_t width(const Field &f) {
    switch (f.kind) {
    case Kind::U8:
    case Kind::Hex8:
        return 1;
    case Kind::U16:
        return 2;
    case Kind::U32:
    case Kind::Name:
        return 4;
    case Kind::Pairs:
        return 2 * f.count;
    case Kind::Quads:
        return 4 * f.count;
    case Kind::List:
    case Kind::Hex:
        return f.count;
    }
    return 0;
}

// Every payload byte is in exactly one field (checked by tests/test_save_file.cpp), except the magic bytes.
// A section's fields are written in this order. Member `queues` sit apart from the records, 0xCC apart.
const Field kFields[] = {
    {"party", "gold", 0xA2, Kind::U32},
    {"party", "inventory", 0x7C, Kind::Pairs, 8, "[item id, uses]; 255 = empty"},
    {"party", "key_items", 0x8C, Kind::Pairs, 11, "[item id, uses]"},
    {"location", "map", 0xDC, Kind::U8, 1, "tables/maps row"},
    {"location", "x", 0xDD, Kind::U8},
    {"location", "y", 0xDE, Kind::U8},
    {"location", "travel_mode", 0xD6, Kind::U8},
    {"location", "previous_map", 0xD7, Kind::U8},
    {"location", "previous_x", 0xD8, Kind::U8},
    {"location", "previous_y", 0xD9, Kind::U8},
    {"location", "vehicles", 0xAF, Kind::Quads, 3, "parked vehicles: [map, x, y, state]"},
    {"progress", "play_time_frames", 0xDF, Kind::U32, 1, "75.47 per second"},
    {"progress", "tower_counter", 0xDA, Kind::U8},
    {"progress", "ending_seen", 0xE5, Kind::U8},
    {"progress", "event_flags", 0xEC, Kind::Hex, 19, "story flag bits, flag n = byte n/8, bit 0x80 >> n%8"},
    {"progress", "bestiary_flags", 0xBB, Kind::Hex, 27},
    {"progress", "packed_progress", 0xA6, Kind::Hex, 8},
    {"progress", "progression_bits", 0xAE, Kind::Hex8},
    {"config", "dialogue_speed", 0xE6, Kind::U8, 1, "the game's own Config menu"},
    {"config", "field_options", 0xE7, Kind::Hex8, 1, "bit 0 dash, 1 auto-target, 2 dash button inverted"},
    {"dialogue", "leader", 0xE3, Kind::U8},
    {"dialogue", "party_order", 0xE8, Kind::List, 4},
    {"unknown", "byte_db", 0xDB, Kind::Hex8},
    {"unknown", "byte_e4", 0xE4, Kind::Hex8},
    {"unknown", "bytes_ff", 0xFF, Kind::Hex, 6},
    {"unknown", "bytes_437", 0x437, Kind::Hex, 2},
    {"member", "name", 0x01, Kind::Name},
    {"member", "race", 0x05, Kind::U8, 1, "character type: tables/monsters row"},
    {"member", "hp", 0x06, Kind::U16},
    {"member", "hp_max", 0x08, Kind::U16},
    {"member", "strength", 0x0B, Kind::U8},
    {"member", "defense", 0x0C, Kind::U8},
    {"member", "agility", 0x0D, Kind::U8},
    {"member", "magic", 0x0E, Kind::U8},
    {"member", "equipment", 0x0F, Kind::Pairs, 8, "[item id, uses]; 255 = empty"},
    {"member", "equipment_flags", 0x0A, Kind::Hex8},
    {"member", "status", 0x00, Kind::Hex8},
    {"member", "queues", 0, Kind::Hex, kQueueStride, "icon/token queues"},
};

const char *const kSections[] = {"party", "location", "progress", "config", "dialogue", "unknown"};

size_t member_offset(const Field &f, int member) {
    return std::strcmp(f.key, "queues") == 0 ? kQueues + kQueueStride * size_t(member)
                                             : kMemberStride * size_t(member) + f.at;
}

std::string hex_string(const uint8_t *p, size_t n) {
    static const char kHex[] = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        s += kHex[p[i] >> 4];
        s += kHex[p[i] & 15];
    }
    return s;
}

std::string toml_string(const std::string &s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out + "\"";
}

// One field's value, in TOML.
std::string value_text(const Field &f, const uint8_t *p) {
    char buf[32];
    switch (f.kind) {
    case Kind::U8:
        return std::to_string(p[0]);
    case Kind::Hex8:
        std::snprintf(buf, sizeof buf, "0x%02X", p[0]);
        return buf;
    case Kind::U16:
        return std::to_string(p[0] | p[1] << 8);
    case Kind::U32:
        return std::to_string(uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
                              uint32_t(p[3]) << 24);
    case Kind::Name: {
        static const Charset charset = Charset::system();
        std::string s;
        for (int i = 0; i < 4; ++i)
            s += charset.decode(p[i]);
        return toml_string(s);
    }
    case Kind::Hex:
        return toml_string(hex_string(p, f.count));
    case Kind::List:
    case Kind::Pairs:
    case Kind::Quads: {
        const size_t per = f.kind == Kind::List ? 1 : f.kind == Kind::Pairs ? 2 : 4;
        std::string s = "[";
        for (size_t i = 0; i < f.count; ++i) {
            if (i)
                s += ", ";
            if (per > 1)
                s += "[";
            for (size_t j = 0; j < per; ++j)
                s += (j ? ", " : "") + std::to_string(p[i * per + j]);
            if (per > 1)
                s += "]";
        }
        return s + "]";
    }
    }
    return {};
}

void write_field(std::string &out, const Field &f, const uint8_t *p) {
    out += f.key;
    out += " = ";
    out += value_text(f, p);
    if (f.comment) {
        out += "  # ";
        out += f.comment;
    }
    out += '\n';
}

// --- reading: a TOML subset (what save_to_text writes, plus comments, blank lines and arrays across lines)

struct Value {
    bool is_int = false, is_string = false;
    int64_t n = 0;
    std::string s;
    std::vector<Value> items; // an array
};

struct Parser {
    std::string_view t;
    size_t i = 0;
    int line = 1;
    std::string error;

    bool fail(const std::string &why) {
        if (error.empty())
            error = "line " + std::to_string(line) + ": " + why;
        return false;
    }
    void skip_space(bool newlines) {
        for (;;) {
            while (i < t.size() &&
                   (t[i] == ' ' || t[i] == '\t' || t[i] == '\r' || (newlines && t[i] == '\n'))) {
                if (t[i] == '\n')
                    ++line;
                ++i;
            }
            if (newlines && i < t.size() && t[i] == '#') {
                while (i < t.size() && t[i] != '\n')
                    ++i;
                continue;
            }
            return;
        }
    }
    bool value(Value &v) {
        skip_space(false);
        if (i >= t.size())
            return fail("missing value");
        if (t[i] == '"') {
            ++i;
            v.is_string = true;
            while (i < t.size() && t[i] != '"' && t[i] != '\n') {
                if (t[i] == '\\' && i + 1 < t.size())
                    ++i;
                v.s += t[i++];
            }
            if (i >= t.size() || t[i] != '"')
                return fail("unterminated string");
            ++i;
            return true;
        }
        if (t[i] == '[') {
            ++i;
            for (;;) {
                skip_space(true);
                if (i < t.size() && t[i] == ']') {
                    ++i;
                    return true;
                }
                Value item;
                if (!value(item))
                    return false;
                v.items.push_back(std::move(item));
                skip_space(true);
                if (i < t.size() && t[i] == ',') {
                    ++i;
                    continue;
                }
                if (i < t.size() && t[i] == ']') {
                    ++i;
                    return true;
                }
                return fail("expected , or ] in an array");
            }
        }
        size_t j = i;
        while (j < t.size() && (std::isalnum(uint8_t(t[j])) || t[j] == '_' || t[j] == '-' || t[j] == '+'))
            ++j;
        std::string word(t.substr(i, j - i));
        std::erase(word, '_');
        if (word.empty())
            return fail("expected a number, string or array");
        try {
            size_t used = 0;
            const bool hex = word.size() > 2 && word[0] == '0' && (word[1] == 'x' || word[1] == 'X');
            v.n = std::stoll(hex ? word.substr(2) : word, &used, hex ? 16 : 10);
            if (used != (hex ? word.size() - 2 : word.size()))
                return fail("not a number: " + word);
        } catch (...) {
            return fail("not a number: " + word);
        }
        v.is_int = true;
        i = j;
        return true;
    }
};

// Parsed file: "section.key" -> value; members: one map per [[member]].
struct Document {
    std::map<std::string, Value> top;
    std::vector<std::map<std::string, Value>> members;
};

bool parse(std::string_view text, Document &doc, std::string *error) {
    Parser p{text, 0, 1, {}};
    std::string section;
    std::map<std::string, Value> *into = &doc.top;
    for (;;) {
        p.skip_space(true);
        if (p.i >= text.size())
            break;
        if (text[p.i] == '[') {
            const bool array = text.substr(p.i, 2) == "[[";
            const size_t end = text.find(array ? "]]" : "]", p.i);
            if (end == std::string_view::npos)
                return *error = "line " + std::to_string(p.line) + ": unterminated section header", false;
            section = std::string(text.substr(p.i + (array ? 2 : 1), end - p.i - (array ? 2 : 1)));
            p.i = end + (array ? 2 : 1);
            if (array) {
                if (section != "member")
                    return *error = "line " + std::to_string(p.line) + ": unknown [[" + section + "]]", false;
                doc.members.emplace_back();
                into = &doc.members.back();
            } else {
                into = &doc.top;
            }
            continue;
        }
        size_t j = p.i;
        while (j < text.size() && (std::isalnum(uint8_t(text[j])) || text[j] == '_'))
            ++j;
        const std::string key(text.substr(p.i, j - p.i));
        p.i = j;
        p.skip_space(false);
        if (key.empty() || p.i >= text.size() || text[p.i] != '=')
            return *error = "line " + std::to_string(p.line) + ": expected key = value", false;
        ++p.i;
        Value v;
        if (!p.value(v))
            return *error = p.error, false;
        const std::string name = into == &doc.top && !section.empty() ? section + "." + key : key;
        if (into->count(name))
            return *error = "line " + std::to_string(p.line) + ": " + name + " given twice", false;
        (*into)[name] = std::move(v);
        p.skip_space(false);
        if (p.i < text.size() && text[p.i] == '#')
            while (p.i < text.size() && text[p.i] != '\n')
                ++p.i;
        if (p.i < text.size() && text[p.i] != '\n')
            return *error = "line " + std::to_string(p.line) + ": unexpected text after the value", false;
    }
    return true;
}

bool number(const Value &v, int64_t max, uint32_t *out) {
    if (!v.is_int || v.n < 0 || v.n > max)
        return false;
    *out = uint32_t(v.n);
    return true;
}

// Stores one field's value into the payload at p.
bool read_field(const Field &f, const Value &v, uint8_t *p, std::string &why) {
    uint32_t n = 0;
    switch (f.kind) {
    case Kind::U8:
    case Kind::Hex8:
        if (!number(v, 0xFF, &n))
            return why = "a number 0-255", false;
        p[0] = uint8_t(n);
        return true;
    case Kind::U16:
        if (!number(v, 0xFFFF, &n))
            return why = "a number 0-65535", false;
        p[0] = uint8_t(n), p[1] = uint8_t(n >> 8);
        return true;
    case Kind::U32:
        if (!number(v, 0xFFFFFFFFll, &n))
            return why = "a number 0-4294967295", false;
        for (int i = 0; i < 4; ++i)
            p[i] = uint8_t(n >> (8 * i));
        return true;
    case Kind::Name: {
        static const Charset charset = Charset::system();
        std::vector<uint8_t> bytes;
        std::string err;
        if (!v.is_string || !charset.encode(v.s, bytes, &err))
            return why = "4 characters the game's font has" + (err.empty() ? "" : " (" + err + ")"), false;
        if (bytes.size() > 4)
            return why = "at most 4 characters", false;
        std::vector<uint8_t> space;
        charset.encode("\u3000", space, nullptr); // shorter names: padded with spaces, as name entry does
        bytes.resize(4, space.empty() ? 0xFF : space[0]);
        std::memcpy(p, bytes.data(), 4);
        return true;
    }
    case Kind::Hex: {
        if (!v.is_string || v.s.size() != 2 * f.count)
            return why = "a string of " + std::to_string(2 * f.count) + " hex digits", false;
        for (size_t i = 0; i < f.count; ++i) {
            const std::string pair = v.s.substr(2 * i, 2);
            if (!std::isxdigit(uint8_t(pair[0])) || !std::isxdigit(uint8_t(pair[1])))
                return why = "hex digits only", false;
            p[i] = uint8_t(std::stoi(pair, nullptr, 16));
        }
        return true;
    }
    case Kind::List:
    case Kind::Pairs:
    case Kind::Quads: {
        const size_t per = f.kind == Kind::List ? 1 : f.kind == Kind::Pairs ? 2 : 4;
        const std::string shape =
            per == 1 ? std::to_string(f.count) + " numbers"
                     : std::to_string(f.count) + " lists of " + std::to_string(per) + " numbers";
        if (v.items.size() != f.count)
            return why = shape, false;
        for (size_t i = 0; i < f.count; ++i) {
            const Value &item = v.items[i];
            for (size_t j = 0; j < per; ++j) {
                const Value &x = per == 1 ? item : (item.items.size() == per ? item.items[j] : Value{});
                if (!number(x, 0xFF, &n))
                    return why = shape + " (0-255)", false;
                p[i * per + j] = uint8_t(n);
            }
        }
        return true;
    }
    }
    return false;
}

std::string slot_path(const std::string &dir, int slot) {
    if (slot == SaveSlots::kAuto)
        return dir + "/auto.toml";
    return dir + "/slot" + std::to_string(slot + 1) + ".toml";
}
std::string store_path(const std::string &dir) { return dir + "/store.toml"; }

std::string read_text(const std::string &path, bool *exists) {
    std::ifstream in(path, std::ios::binary);
    *exists = bool(in);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

} // namespace

std::string save_to_text(const std::vector<uint8_t> &payload, const PartyNameList *names) {
    std::string out;
    if (payload.size() != SaveSlots::kPayload)
        return out;
    const uint8_t *p = payload.data();
    out +=
        "# SawGod save slot (Makai Toushi SaGa, WSC). Edit freely: the game checks nothing but the format.\n";
    out += "# Fields: docs/SAVES.md. Numbers may be decimal or 0x hex.\n";
    out += "format = " + std::to_string(kSaveFileFormat) + "\n";
    for (const char *section : kSections) {
        out += std::string("\n[") + section + "]\n";
        for (const Field &f : kFields)
            if (std::strcmp(f.section, section) == 0)
                write_field(out, f, p + f.at);
    }
    for (int m = 0; m < kMembers; ++m) {
        out += "\n[[member]]\n";
        for (const Field &f : kFields)
            if (std::strcmp(f.section, "member") == 0) {
                if (f.kind == Kind::Name) {
                    std::string full = names ? (*names)[size_t(m)] : std::string();
                    const auto *projection = p + member_offset(f, m);
                    if (full.empty()) {
                        for (int n = 0; n < 4; ++n) {
                            auto glyph = Charset::system().decode(projection[n]);
                            full += glyph.size() == 4 && glyph.front() == '['
                                        ? name_utf8(std::u32string(1, 0xE000 + projection[n]))
                                        : glyph;
                        }
                    }
                    out += std::string(f.key) + " = " + toml_string(full) + "\n";
                    // An empty host name means use the RAM projection, not a new four-glyph host name.
                    // Keep that distinction without changing the bytes restored by older slot files.
                    if (names && (*names)[size_t(m)].empty())
                        out += "host_name = \"\"\n";
                    continue;
                }
                write_field(out, f, p + member_offset(f, m));
            }
    }
    return out;
}

bool save_from_text(std::string_view text, std::vector<uint8_t> *payload, std::string *error,
                    PartyNameList *names) {
    Document doc;
    std::string err;
    if (!parse(text, doc, &err)) {
        if (error)
            *error = err;
        return false;
    }
    auto fail = [&](const std::string &why) {
        if (error)
            *error = why;
        return false;
    };
    const auto format = doc.top.find("format");
    if (format == doc.top.end() || !format->second.is_int)
        return fail("no format = ... line");
    if (format->second.n != kSaveFileFormat)
        return fail("format " + std::to_string(format->second.n) + " (this build reads format " +
                    std::to_string(kSaveFileFormat) + ")");
    if (doc.members.size() != kMembers)
        return fail("needs exactly " + std::to_string(kMembers) + " [[member]] sections");
    std::vector<uint8_t> out(SaveSlots::kPayload, 0);
    PartyNameList full_names{};
    size_t known = 1; // "format"
    for (const Field &f : kFields) {
        if (std::strcmp(f.section, "member") == 0)
            continue;
        const std::string name = std::string(f.section) + "." + f.key;
        const auto it = doc.top.find(name);
        if (it == doc.top.end())
            return fail("missing " + name);
        std::string why;
        if (!read_field(f, it->second, out.data() + f.at, why))
            return fail(name + ": expected " + why);
        ++known;
    }
    if (known != doc.top.size())
        for (const auto &[name, v] : doc.top) {
            bool found = name == "format";
            for (const Field &f : kFields)
                found |=
                    std::strcmp(f.section, "member") != 0 && name == std::string(f.section) + "." + f.key;
            if (!found)
                return fail("unknown field " + name);
        }
    for (int m = 0; m < kMembers; ++m) {
        const auto &fields = doc.members[size_t(m)];
        size_t used = 0;
        for (const Field &f : kFields) {
            if (std::strcmp(f.section, "member") != 0)
                continue;
            const std::string where = "member " + std::to_string(m + 1) + " " + f.key;
            const auto it = fields.find(f.key);
            if (it == fields.end())
                return fail("missing " + where);
            std::string why;
            Value projected = it->second;
            if (f.kind == Kind::Name) {
                if (!projected.is_string || !valid_player_name(projected.s))
                    return fail(where + ": expected at most 10 valid UTF-8 glyphs");
                full_names[size_t(m)] = projected.s;
                const auto glyphs = utf8_to_u32(projected.s);
                auto *prefix = out.data() + member_offset(f, m);
                for (size_t n = 0; n < 4; ++n) {
                    prefix[n] = 0xD9;
                    if (n >= glyphs.size())
                        continue;
                    const auto cp = glyphs[n];
                    if (cp >= 0xE000 && cp <= 0xE0FF) {
                        prefix[n] = uint8_t(cp - 0xE000);
                        continue;
                    }
                    std::vector<uint8_t> code;
                    if (Charset::system().encode(name_utf8(std::u32string(1, cp)), code, nullptr) &&
                        code.size() == 1)
                        prefix[n] = code[0];
                }
                ++used;
                continue;
            }
            if (!read_field(f, projected, out.data() + member_offset(f, m), why))
                return fail(where + ": expected " + why);
            ++used;
        }
        if (const auto it = fields.find("host_name"); it != fields.end()) {
            if (!it->second.is_string || !it->second.s.empty())
                return fail("member " + std::to_string(m + 1) + " host_name: expected an empty string");
            full_names[size_t(m)].clear();
            ++used;
        }
        if (used != fields.size())
            return fail("member " + std::to_string(m + 1) + ": unknown field");
    }
    out[kMagicAt] = 0x1B;
    out[kMagicAt + 1] = 0xE4;
    *payload = std::move(out);
    if (names)
        *names = std::move(full_names);
    return true;
}

bool read_save_store(const std::string &dir, SaveSlots &slots, std::vector<std::string> *warnings) {
    slots.attach_store();
    bool exists = false;
    const std::string store = read_text(store_path(dir), &exists);
    if (!exists)
        return false;
    Document doc;
    std::string error;
    if (parse(store, doc, &error)) {
        const auto last = doc.top.find("last_slot");
        if (last != doc.top.end() && last->second.is_int)
            slots.load_last(int(last->second.n) - 1);
    } else if (warnings) {
        warnings->push_back(store_path(dir) + ": " + error);
    }
    for (int slot = 0; slot < SaveSlots::kSlots; ++slot) {
        const std::string text = read_text(slot_path(dir, slot), &exists);
        if (!exists)
            continue;
        std::vector<uint8_t> payload;
        PartyNameList names;
        if (save_from_text(text, &payload, &error, &names)) {
            slots.load_payload(slot, payload, names);
        } else {
            slots.lock(slot); // never overwrite a file we couldn't read
            if (warnings)
                warnings->push_back(slot_path(dir, slot) + ": " + error + " (slot " +
                                    std::to_string(slot + 1) + " is empty and won't be saved this session)");
        }
    }
    slots.clear_dirty();
    return true;
}

bool write_save_store(const std::string &dir, SaveSlots &slots, const SaveFileWriter &write,
                      std::string *error) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    bool ok = true;
    for (int slot = 0; slot < SaveSlots::kSlots; ++slot) {
        if (!slots.slot_dirty(slot))
            continue;
        if (write(slot_path(dir, slot), save_to_text(slots.payload(slot), &slots.names(slot)), error))
            slots.clear_dirty(slot);
        else
            ok = false;
    }
    if (slots.last_dirty() || !std::filesystem::exists(store_path(dir))) {
        const std::string text =
            "# SawGod save store (docs/SAVES.md)\nformat = " + std::to_string(kSaveFileFormat) +
            "\nlast_slot = " + std::to_string(slots.last() + 1) + "  # 0 = none\n";
        if (write(store_path(dir), text, error))
            slots.clear_last_dirty();
        else
            ok = false;
    }
    return ok;
}

int import_wsc_sram(const std::vector<uint8_t> &sram, SaveSlots &slots) {
    constexpr size_t kLast = 0x3300, kFirst = 0x3301, kBackup = 0xCB1;
    int n = 0;
    for (int slot = 0; slot < SaveSlots::kOriginal; ++slot) {
        for (size_t at : {kFirst + SaveSlots::kBlock * size_t(slot),
                          kFirst + SaveSlots::kBlock * size_t(slot) + kBackup}) {
            if (at + SaveSlots::kBlock > sram.size())
                continue;
            const uint8_t *b = sram.data() + at;
            uint16_t sum = 0;
            for (size_t i = 0; i < SaveSlots::kPayload; ++i)
                sum = uint16_t(sum + b[i]);
            if (sum != (b[SaveSlots::kPayload] | b[SaveSlots::kPayload + 1] << 8) || b[kMagicAt] != 0x1B ||
                b[kMagicAt + 1] != 0xE4)
                continue;
            slots.store(slot, std::vector<uint8_t>(b, b + SaveSlots::kPayload));
            ++n;
            break;
        }
    }
    if (sram.size() > kLast && sram[kLast] < SaveSlots::kOriginal && slots.has(sram[kLast]))
        slots.set_last(sram[kLast]);
    return n;
}

bool save_fields_cover_payload(std::string *error) {
    std::vector<int> uses(SaveSlots::kPayload, 0);
    uses[kMagicAt] = uses[kMagicAt + 1] = 1;
    for (const Field &f : kFields) {
        const bool member = std::strcmp(f.section, "member") == 0;
        for (int m = 0; m < (member ? kMembers : 1); ++m) {
            const size_t at = member ? member_offset(f, m) : f.at;
            for (size_t i = 0; i < width(f); ++i)
                if (at + i < uses.size())
                    ++uses[at + i];
        }
    }
    for (size_t i = 0; i < uses.size(); ++i)
        if (uses[i] != 1) {
            if (error)
                *error =
                    "payload byte " + std::to_string(i) + " is in " + std::to_string(uses[i]) + " fields";
            return false;
        }
    return true;
}

} // namespace sawgod::game
