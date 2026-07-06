#ifdef _WIN32
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0600
    #endif
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <sys/select.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <signal.h>
    #define SOCKET int
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR (-1)
    #define closesocket close
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cstdarg>
#include <ctime>
#include <cerrno>

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <queue>
#include <deque>
#include <algorithm>
#include <random>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <memory>
#include <chrono>
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <fstream>
#include <regex>

// ============================================================================
// Platform abstractions
// ============================================================================
#ifdef _WIN32
    using ssize_t = int64_t;
    using socklen_t = int;
    #define THREAD_LOCAL __declspec(thread)
#else
    #define THREAD_LOCAL __thread
#endif

// ============================================================================
// Simple JSON library (header-only, minimal)
// ============================================================================
namespace json {

enum class Type { Null, Bool, Number, String, Array, Object };

struct Value;

using Array = std::vector<Value>;
using Object = std::map<std::string, Value>;

struct Value {
    Type type = Type::Null;
    bool bool_val = false;
    double num_val = 0.0;
    std::string str_val;
    Array arr_val;
    Object obj_val;

    Value() = default;
    Value(std::nullptr_t) : type(Type::Null) {}
    Value(bool v) : type(Type::Bool), bool_val(v) {}
    Value(int v) : type(Type::Number), num_val(static_cast<double>(v)) {}
    Value(double v) : type(Type::Number), num_val(v) {}
    Value(const char* v) : type(Type::String), str_val(v) {}
    Value(const std::string& v) : type(Type::String), str_val(v) {}
    Value(const Array& v) : type(Type::Array), arr_val(v) {}
    Value(const Object& v) : type(Type::Object), obj_val(v) {}

    bool is_null() const { return type == Type::Null; }
    bool is_bool() const { return type == Type::Bool; }
    bool is_number() const { return type == Type::Number; }
    bool is_string() const { return type == Type::String; }
    bool is_array() const { return type == Type::Array; }
    bool is_object() const { return type == Type::Object; }

    int as_int() const { return static_cast<int>(num_val); }
    double as_double() const { return num_val; }
    bool as_bool() const { return bool_val; }
    const std::string& as_string() const { return str_val; }

    bool contains(const std::string& key) const {
        return is_object() && obj_val.count(key) > 0;
    }

    Value& operator[](const std::string& key) {
        if (!is_object()) { type = Type::Object; obj_val.clear(); }
        return obj_val[key];
    }
    const Value& operator[](const std::string& key) const {
        static Value null_val;
        if (is_object()) {
            auto it = obj_val.find(key);
            if (it != obj_val.end()) return it->second;
        }
        return null_val;
    }

    Value& operator[](size_t i) {
        if (!is_array()) { type = Type::Array; arr_val.clear(); }
        if (i >= arr_val.size()) arr_val.resize(i + 1);
        return arr_val[i];
    }

    size_t size() const {
        if (is_array()) return arr_val.size();
        if (is_object()) return obj_val.size();
        return 0;
    }

    void push_back(const Value& v) {
        if (!is_array()) { type = Type::Array; arr_val.clear(); }
        arr_val.push_back(v);
    }
};

// Forward declarations for parser
struct Parser {
    const char* p;
    const char* end;

    Parser(const std::string& s) : p(s.c_str()), end(s.c_str() + s.size()) {}

    void skip_ws() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    }

    Value parse_value();
    Value parse_object();
    Value parse_array();
    Value parse_string();
    Value parse_number();
    Value parse_literal();
};

inline Value Parser::parse_value() {
    skip_ws();
    if (p >= end) return Value();
    switch (*p) {
        case '{': return parse_object();
        case '[': return parse_array();
        case '"': return parse_string();
        case 't': case 'f': case 'n': return parse_literal();
        default: return parse_number();
    }
}

inline Value Parser::parse_object() {
    Object obj;
    p++; // skip '{'
    skip_ws();
    if (p < end && *p == '}') { p++; return Value(obj); }
    while (p < end) {
        skip_ws();
        Value key = parse_string();
        skip_ws();
        if (p < end && *p == ':') p++;
        Value val = parse_value();
        obj[key.as_string()] = val;
        skip_ws();
        if (p < end && *p == ',') { p++; continue; }
        if (p < end && *p == '}') { p++; break; }
    }
    return Value(obj);
}

inline Value Parser::parse_array() {
    Array arr;
    p++; // skip '['
    skip_ws();
    if (p < end && *p == ']') { p++; return Value(arr); }
    while (p < end) {
        arr.push_back(parse_value());
        skip_ws();
        if (p < end && *p == ',') { p++; continue; }
        if (p < end && *p == ']') { p++; break; }
    }
    return Value(arr);
}

inline Value Parser::parse_string() {
    p++; // skip '"'
    std::string s;
    while (p < end && *p != '"') {
        if (*p == '\\') {
            p++;
            if (p < end) {
                switch (*p) {
                    case '"': s += '"'; break;
                    case '\\': s += '\\'; break;
                    case '/': s += '/'; break;
                    case 'b': s += '\b'; break;
                    case 'f': s += '\f'; break;
                    case 'n': s += '\n'; break;
                    case 'r': s += '\r'; break;
                    case 't': s += '\t'; break;
                    case 'u': {
                        // Skip unicode escapes for simplicity
                        p += 4;
                        s += '?';
                        continue;
                    }
                    default: s += *p; break;
                }
            }
        } else {
            s += *p;
        }
        p++;
    }
    if (p < end) p++; // skip closing '"'
    return Value(s);
}

inline Value Parser::parse_number() {
    const char* start = p;
    if (p < end && *p == '-') p++;
    while (p < end && *p >= '0' && *p <= '9') p++;
    if (p < end && *p == '.') {
        p++;
        while (p < end && *p >= '0' && *p <= '9') p++;
    }
    if (p < end && (*p == 'e' || *p == 'E')) {
        p++;
        if (p < end && (*p == '+' || *p == '-')) p++;
        while (p < end && *p >= '0' && *p <= '9') p++;
    }
    std::string num_str(start, p - start);
    return Value(std::stod(num_str));
}

inline Value Parser::parse_literal() {
    if (strncmp(p, "true", 4) == 0) { p += 4; return Value(true); }
    if (strncmp(p, "false", 5) == 0) { p += 5; return Value(false); }
    if (strncmp(p, "null", 4) == 0) { p += 4; return Value(); }
    return Value();
}

inline Value parse(const std::string& s) {
    Parser parser(s);
    return parser.parse_value();
}

// JSON Serializer
inline std::string serialize(const Value& v) {
    std::ostringstream oss;
    switch (v.type) {
        case Type::Null: oss << "null"; break;
        case Type::Bool: oss << (v.bool_val ? "true" : "false"); break;
        case Type::Number: {
            double d = v.num_val;
            if (d == static_cast<int64_t>(d)) oss << static_cast<int64_t>(d);
            else oss << d;
            break;
        }
        case Type::String: {
            oss << '"';
            for (char c : v.str_val) {
                switch (c) {
                    case '"': oss << "\\\""; break;
                    case '\\': oss << "\\\\"; break;
                    case '\n': oss << "\\n"; break;
                    case '\r': oss << "\\r"; break;
                    case '\t': oss << "\\t"; break;
                    default: oss << c;
                }
            }
            oss << '"';
            break;
        }
        case Type::Array: {
            oss << '[';
            for (size_t i = 0; i < v.arr_val.size(); i++) {
                if (i > 0) oss << ',';
                oss << serialize(v.arr_val[i]);
            }
            oss << ']';
            break;
        }
        case Type::Object: {
            oss << '{';
            bool first = true;
            for (auto& [key, val] : v.obj_val) {
                if (!first) oss << ',';
                oss << '"' << key << "\":" << serialize(val);
                first = false;
            }
            oss << '}';
            break;
        }
    }
    return oss.str();
}

} // namespace json

// ============================================================================
// Logging
// ============================================================================
enum LogLevel { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };

static LogLevel g_log_level = LOG_INFO;

void log_set_level(LogLevel level) { g_log_level = level; }

void log_msg(LogLevel level, const char* fmt, ...) {
    if (level < g_log_level) return;
    va_list args;
    va_start(args, fmt);
    const char* prefix = "";
    switch (level) {
        case LOG_DEBUG: prefix = "[DEBUG] "; break;
        case LOG_INFO:  prefix = "[INFO]  "; break;
        case LOG_WARN:  prefix = "[WARN]  "; break;
        case LOG_ERROR: prefix = "[ERROR] "; break;
    }
    fprintf(stderr, "%s", prefix);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

#define LOG_DEBUG(fmt, ...) log_msg(LOG_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  log_msg(LOG_INFO,  fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  log_msg(LOG_WARN,  fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) log_msg(LOG_ERROR, fmt, ##__VA_ARGS__)

// ============================================================================
// Mahjong Type Constants
// ============================================================================
constexpr int TOTAL_TILES = 136;
constexpr int TILES_PER_SUIT = 36;  // 9 * 4
constexpr int NUM_TILE_TYPES = 34;  // man(0-8) pin(9-17) sou(18-26) wind(27-30) dragon(31-33)
constexpr int AKA_MAN5 = 16;
constexpr int AKA_PIN5 = 52;
constexpr int AKA_SOU5 = 88;

inline int tile_type(int tile_id) { return tile_id / 4; }
inline int tile_number(int tile_id) { return (tile_id / 4) % 9; }

// Suit: 0=man, 1=pin, 2=sou, 3=wind, 4=dragon
inline int tile_suit(int t) { return t / 9; }

// ============================================================================
// Forward declarations
// ============================================================================
struct Agent;
struct MahjongGame;
struct GameEnvironment;
struct Client;
struct Server;

// ============================================================================
// Thread-safe queue (mirrors Python ControlledQueue)
// ============================================================================
template<typename T>
class ControlledQueue {
    std::queue<T> q;
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<bool> allow_put{false};
public:
    void set_allow_put() { allow_put = true; }

    void put(T item) {
        if (allow_put.exchange(false)) {
            std::lock_guard<std::mutex> lk(mtx);
            q.push(std::move(item));
            cv.notify_one();
        }
    }

    T get() {
        std::unique_lock<std::mutex> lk(mtx);
        cv.wait(lk, [this] { return !q.empty(); });
        T item = std::move(q.front());
        q.pop();
        return item;
    }

    void clear() {
        std::lock_guard<std::mutex> lk(mtx);
        while (!q.empty()) q.pop();
    }
};

// ============================================================================
// Agent (mirrors mahjong.agent.Agent)
// ============================================================================
struct Agent {
    int score = 250;          // score / 100
    std::set<int> tiles;      // tile IDs (0-135)
    std::vector<int> hand_tile_counter;  // 34-element count per tile type
    std::vector<int> hand_tile_counter_bak;
    std::vector<int> discard_tiles;
    std::vector<int> river;

    // furo: key = (type, pattern_tuple), value = tile IDs
    struct FuroKey {
        int furo_type; // 0=chi, 1=pon, 2=ankan, 3=minkan
        int pattern;   // tile type for pon/kan, or (min_type, nth) for chi
        int nth = -1;
        bool operator<(const FuroKey& o) const {
            if (furo_type != o.furo_type) return furo_type < o.furo_type;
            if (pattern != o.pattern) return pattern < o.pattern;
            return nth < o.nth;
        }
    };
    std::map<FuroKey, std::vector<int>> furo;
    std::vector<std::tuple<int, int, int>> kui_info; // (type, from_who, kui_tile)

    int declare_riichi = 0;
    int riichi_status = 0;
    int riichi_round = 100;
    int riichi_tile = -1;
    int ippatsu_status = 0;
    bool kui = false;
    std::set<int> machi;
    int menfon = 27;  // wind tile type

    int nagashimangan = 1;

    bool discard_furiten = false;
    bool riichi_furiten = false;
    bool round_furiten = false;

    Agent() : hand_tile_counter(34, 0), hand_tile_counter_bak(34, 0) {}
    Agent(int s, const std::set<int>& t, int seat)
        : score(s), tiles(t), hand_tile_counter(34, 0), hand_tile_counter_bak(34, 0) {
        for (int tid : tiles) hand_tile_counter[tid / 4]++;
        hand_tile_counter_bak = hand_tile_counter;
        menfon = 27 + seat;
    }

    bool is_furiten() const { return discard_furiten || riichi_furiten || round_furiten; }

    void draw(int tile_id) {
        tiles.insert(tile_id);
        hand_tile_counter[tile_id / 4]++;
    }

    void discard(int tile_id) {
        int t = tile_id / 4;
        tiles.erase(tile_id);
        discard_tiles.push_back(tile_id);
        river.push_back(tile_id);
        hand_tile_counter[t]--;
    }

    void pon(const std::vector<int>& tile_ids, int kui_tile, int from_who) {
        kui = true;
        int t = tile_ids[0] / 4;
        FuroKey key{1, t};
        furo[key] = tile_ids;
        kui_info.push_back({1, from_who, kui_tile});
        for (int tid : tile_ids) {
            if (tid != kui_tile) tiles.erase(tid);
        }
        for (int tid : tile_ids) hand_tile_counter[tid / 4]--;
        discard_tiles.pop_back();
        river.pop_back();
    }

    void chi(const std::vector<int>& tile_ids, int kui_tile, int from_who) {
        kui = true;
        int min_t = (*std::min_element(tile_ids.begin(), tile_ids.end(),
            [](int a, int b) { return tile_type(a) < tile_type(b); })) / 4;
        FuroKey key{0, min_t, static_cast<int>(furo.size())};
        furo[key] = tile_ids;
        kui_info.push_back({0, from_who, kui_tile});
        for (int tid : tile_ids) {
            if (tid != kui_tile) tiles.erase(tid);
        }
        for (int tid : tile_ids) hand_tile_counter[tid / 4]--;
        discard_tiles.pop_back();
        river.pop_back();
    }

    void kan(const std::vector<int>& tile_ids, int add, int kui_tile, int from_who, int mode) {
        // mode: 0=ankan, 1=minkan, 2=addkan
        int t = tile_ids[0] / 4;
        if (mode == 0) { // closed kan
            FuroKey key{2, t};
            furo[key] = tile_ids;
            for (int tid : tile_ids) tiles.erase(tid);
            kui_info.push_back({2, -1, -1});
        } else if (mode == 1) { // open kan
            kui = true;
            FuroKey key{3, t};
            furo[key] = tile_ids;
            for (int tid : tile_ids) {
                if (tid != kui_tile) tiles.erase(tid);
            }
            kui_info.push_back({3, from_who, kui_tile});
            discard_tiles.pop_back();
            river.pop_back();
        } else { // add kan
            // Remove pon, add kan
            furo.erase({1, t});
            FuroKey key{3, t};
            std::vector<int> kan_tiles = {add};
            // Add 3 same tiles from hand
            for (int tid : {t*4, t*4+1, t*4+2, t*4+3}) {
                if (tid != add && tiles.count(tid)) {
                    kan_tiles.push_back(tid);
                }
            }
            furo[key] = kan_tiles;
            for (int tid : kan_tiles) {
                if (tid != add) tiles.erase(tid);
            }
            kui_info.push_back({2, -1, add});
        }
        // Update hand counter
        hand_tile_counter.assign(34, 0);
        for (int tid : tiles) hand_tile_counter[tid / 4]++;
    }

    // Check if can pon
    bool can_pon(int tile_id) {
        int t = tile_type(tile_id);
        return hand_tile_counter[t] >= 2;
    }

    // Check if can chi
    std::pair<std::vector<int>, bool> check_chi_helper(int t, int kui_t) {
        // Returns (pattern_types, possible)
        std::vector<int> patterns;
        int s = tile_suit(t);
        int n = tile_number(t);
        if (s >= 3) return {patterns, false}; // winds and dragons can't chi

        int kui_n = tile_number(kui_t);

        // pattern: (lower, middle, upper) relative to kui
        if (n >= 2 && hand_tile_counter[t - 2] > 0 && hand_tile_counter[t - 1] > 0)
            patterns.push_back(t - 2);
        if (n >= 1 && n <= 7 && hand_tile_counter[t - 1] > 0 && hand_tile_counter[t + 1] > 0)
            patterns.push_back(t - 1);
        if (n <= 6 && hand_tile_counter[t + 1] > 0 && hand_tile_counter[t + 2] > 0)
            patterns.push_back(t);

        return {patterns, !patterns.empty()};
    }

    // Search furo tiles with aka handling
    std::vector<int> search_furo_pon(int t, int kui_tile) {
        std::vector<int> result = {kui_tile};
        int count = 0;
        for (int tid : {t*4, t*4+1, t*4+2, t*4+3}) {
            if (tid != kui_tile && tiles.count(tid) && count < 2) {
                result.push_back(tid);
                count++;
            }
        }
        return result;
    }

    std::vector<int> search_furo_chi(int min_t, int kui_tile) {
        std::vector<int> result = {kui_tile};
        for (int i = 0; i < 3; i++) {
            int ct = min_t + i;
            for (int tid : {ct*4, ct*4+1, ct*4+2, ct*4+3}) {
                if (tid != kui_tile && tiles.count(tid)) {
                    result.push_back(tid);
                    break;
                }
            }
        }
        return result;
    }
};

// ============================================================================
// MahjongGame (mirrors mahjong.game.MahjongGame)
// ============================================================================
struct MahjongGame {
    std::vector<int> yama;
    int left_num = 0;
    bool has_aka = true;
    int round = 0;
    int round_wind = 27;
    int honba = 0;
    int riichi_ba = 0;
    std::vector<int> dora_indicator;
    std::vector<int> dora;
    std::vector<int> ura_dora_indicator;
    std::vector<int> ura_dora;
    int oya = 0;
    std::vector<int> kang_num;
    std::vector<Agent> agents;
    std::vector<int> ranks;
    std::vector<int> public_visible_tiles;  // count per tile type (34)
    bool first_round = true;

    std::mt19937 rng;

    MahjongGame(bool _has_aka = true)
        : has_aka(_has_aka), kang_num(4, 0), public_visible_tiles(34, 0) {
        std::random_device rd;
        rng.seed(rd());
    }

    static int get_dora_indicator(int tile_id) {
        int t = tile_type(tile_id);
        int s = tile_suit(t);
        int n = tile_number(t);
        if (s == 3) return (s * 9 + (n + 1) % 4) * 4; // winds cycle
        if (s == 4) return (s * 9 + (n + 1) % 3) * 4; // dragons cycle
        return (s * 9 + (n + 1) % 9) * 4;
    }

    static int get_dora(int indicator) {
        return get_dora_indicator(indicator);
    }

    void new_game(int game_round, int _honba, int _riichi_ba) {
        round = game_round;
        honba = _honba;
        riichi_ba = _riichi_ba;
        round_wind = 27 + (game_round / 4) * 1;
        oya = game_round % 4;
        kang_num.assign(4, 0);
        first_round = true;

        // Initialize yama (0-135)
        yama.resize(TOTAL_TILES);
        for (int i = 0; i < TOTAL_TILES; i++) yama[i] = i;
        std::shuffle(yama.begin(), yama.end(), rng);

        // Roll dice
        int dice1 = (rng() % 6) + 1;
        int dice2 = (rng() % 6) + 1;
        int start_side = (dice1 + dice2) % 4;
        int start_pos = -start_side * 34 + (dice1 + dice2) * 2;
        if (start_pos < 0) start_pos += TOTAL_TILES;
        std::rotate(yama.begin(), yama.begin() + start_pos, yama.end());

        left_num = TOTAL_TILES - 13 * 4 - 14;

        int dora_ind = yama[TOTAL_TILES - 6];
        int ura_ind = yama[TOTAL_TILES - 5];
        dora_indicator = {dora_ind};
        dora = {get_dora(dora_ind)};
        ura_dora_indicator = {ura_ind};
        ura_dora = {get_dora(ura_ind)};

        // Deal tiles
        for (int i = 0; i < 4; i++) {
            int seat = (oya + i) % 4;
            std::set<int> hand;
            // 4 tiles * 3 rows + 1 tile
            for (int j = 0; j < 4; j++) {
                hand.insert(yama[i * 4 + j]);
                hand.insert(yama[i * 4 + 16 + j]);
                hand.insert(yama[i * 4 + 32 + j]);
            }
            hand.insert(yama[i + 48]);
            agents[seat] = Agent(agents[seat].score, hand, seat);
        }

        // Remove dealt tiles from yama
        yama.erase(yama.begin(), yama.begin() + 52);

        // Ranks
        ranks.resize(4);
        std::vector<std::pair<int, int>> index_scores;
        for (int i = 0; i < 4; i++)
            index_scores.push_back({i, agents[i].score});
        std::sort(index_scores.begin(), index_scores.end(),
            [](auto& a, auto& b) { return a.second > b.second; });
        for (int rank = 0; rank < 4; rank++)
            ranks[index_scores[rank].first] = rank;

        public_visible_tiles.assign(34, 0);
        public_visible_tiles[dora_ind / 4]++;
    }

    void new_dora(int dora_val = -1) {
        if (dora_val < 0) {
            dora_val = yama[TOTAL_TILES - 6 - 2 * static_cast<int>(dora.size()) +
                (kang_num[0] + kang_num[1] + kang_num[2] + kang_num[3])];
        }
        int ura_val = yama[TOTAL_TILES - 5 - 2 * static_cast<int>(dora.size()) +
            (kang_num[0] + kang_num[1] + kang_num[2] + kang_num[3]) - 1];
        ura_dora_indicator.push_back(ura_val);
        ura_dora.push_back(get_dora(ura_val));
        dora_indicator.push_back(dora_val);
        dora.push_back(get_dora(dora_val));
        public_visible_tiles[dora_val / 4]++;
    }

    int draw_tile(int who, int tile_id = -1, int where = 0) {
        if (tile_id < 0) {
            if (where == -1) {
                int total_kang = kang_num[0] + kang_num[1] + kang_num[2] + kang_num[3];
                where = (total_kang % 2 == 0) ? -1 : -2;
            }
            // Pop from yama
            if (where < 0) {
                tile_id = yama[yama.size() + where];
                yama.erase(yama.end() + where);
            } else {
                tile_id = yama[0];
                yama.erase(yama.begin());
            }
        }
        agents[who].draw(tile_id);
        left_num--;
        return tile_id;
    }

    void discard_tile(int who, int tile_id) {
        agents[who].discard(tile_id);
        public_visible_tiles[tile_id / 4]++;
    }

    bool can_declare_riichi(int who) {
        if (left_num < 4) return false;
        // Check if hand is closed (no furo) and can riichi
        if (!agents[who].furo.empty()) return false;
        // Simplified: check if any discard leads to tenpai
        return true;  // Full implementation needs machi checking
    }

    void riichi(int who, bool double_riichi = false) {
        agents[who].riichi_status = 1;
        agents[who].riichi_round = static_cast<int>(agents[who].discard_tiles.size()) + 1;
        agents[who].ippatsu_status = 1;
        riichi_ba++;
    }

    void chi(int who, const std::vector<int>& tile_ids, int kui_tile, int from_who) {
        for (int tid : tile_ids)
            if (tid != kui_tile)
                public_visible_tiles[tid / 4]++;
        agents[who].chi(tile_ids, kui_tile, from_who);
    }

    void pon(int who, const std::vector<int>& tile_ids, int kui_tile, int from_who) {
        agents[who].pon(tile_ids, kui_tile, from_who);
        public_visible_tiles[kui_tile / 4] += 2;
    }

    void kan(int who, const std::vector<int>& tile_ids, int add = -1, int kui_tile = -1,
             int from_who = -1, int mode = 0) {
        agents[who].kan(tile_ids, add, kui_tile, from_who, mode);
        kang_num[who]++;
        public_visible_tiles[tile_ids[0] / 4] = 4;
    }

    std::vector<std::pair<int, int>> get_rank() {
        std::vector<std::pair<int, int>> scores;
        for (int i = 0; i < 4; i++)
            scores.push_back({i, agents[i].score});
        std::sort(scores.begin(), scores.end(),
            [](auto& a, auto& b) { return a.second > b.second; });
        return scores;
    }

    // Check pon/chi/kan availability
    bool check_pon(int who, int tile_id) {
        if (left_num == 0) return false;
        return agents[who].can_pon(tile_id);
    }

    std::pair<bool, std::vector<int>> check_chi(int who, int tile_id) {
        if (left_num == 0) return {false, {}};
        auto [patterns, ok] = agents[who].check_chi_helper(tile_type(tile_id), tile_type(tile_id));
        return {ok, patterns};
    }

    std::pair<bool, std::vector<int>> check_kan(int who, int tile_id, int mode) {
        if (left_num == 0) return {false, {}};
        int t = tile_type(tile_id);
        if (mode == 0) {
            // Check ankan
            if (agents[who].hand_tile_counter[t] == 4)
                return {true, {t}};
            return {false, {}};
        }
        if (mode == 2) {
            // Check add kan (kakan)
            for (auto& [key, tiles] : agents[who].furo) {
                if (key.furo_type == 1 && key.pattern == t) {
                    return {true, {t}};
                }
            }
            return {false, {}};
        }
        // mode 1: minkan
        if (agents[who].hand_tile_counter[t] >= 3)
            return {true, {t}};
        return {false, {}};
    }

    // Search furo for given type
    std::vector<int> search_furo_pon(int who, int t, int kui_tile) {
        return agents[who].search_furo_pon(t, kui_tile);
    }

    std::vector<int> search_furo_chi(int who, int min_t, int kui_tile) {
        return agents[who].search_furo_chi(min_t, kui_tile);
    }

    std::vector<int> search_furo_kan_ankan(int who, int t) {
        return {t*4, t*4+1, t*4+2, t*4+3};
    }

    std::vector<int> search_furo_kan_kakan(int who, int t, int add) {
        std::vector<int> result = {add};
        for (int tid : {t*4, t*4+1, t*4+2, t*4+3}) {
            if (tid != add) result.push_back(tid);
        }
        return result;
    }
};

// ============================================================================
// Client (mirrors Python Client)
// ============================================================================
struct Client {
    SOCKET sock = INVALID_SOCKET;
    std::string username;
    std::unique_ptr<ControlledQueue<json::Value>> message_queue;
    bool disconnected = false;

    Client() : message_queue(std::make_unique<ControlledQueue<json::Value>>()) {}
    Client(SOCKET s, const std::string& u)
        : sock(s), username(u), message_queue(std::make_unique<ControlledQueue<json::Value>>()) {}

    Client(Client&&) = default;
    Client& operator=(Client&&) = default;
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    bool is_human() const { return sock != INVALID_SOCKET && !disconnected; }
    bool is_ai() const { return sock == INVALID_SOCKET && !disconnected; }

    void send_msg(const json::Value& msg);

    void close() {
        if (sock != INVALID_SOCKET) {
            closesocket(sock);
            sock = INVALID_SOCKET;
        }
    }

    json::Value fetch_message() {
        message_queue->set_allow_put();
        return message_queue->get();
    }

    void clear_queue() {
        message_queue->clear();
    }
};

// ============================================================================
// JSON protocol helpers
// ============================================================================
json::Value make_response(const std::string& event) {
    json::Value v;
    v["event"] = event;
    return v;
}

// ============================================================================
// Forward-declare global send functions that need Client
// ============================================================================
void send_personal(Client& client, const json::Value& msg);
void send_multiply(GameEnvironment& env, const json::Value& msg, int except = -1, int except_ob = -1);
void send_observers(GameEnvironment& env, int who, const json::Value& msg);
void send_all_game_info(GameEnvironment& env, Client* client = nullptr);

void Client::send_msg(const json::Value& msg) {
    if (is_human()) {
        std::string data = json::serialize(msg) + "\n";
        ::send(sock, data.c_str(), static_cast<int>(data.size()), 0);
    }
}

// ============================================================================
// GameEnvironment (mirrors Python GameEnvironment)
// ============================================================================
struct GameEnvironment {
    MahjongGame game;
    int round = 0;
    int honba = 0;
    int riichi_ba = 0;
    bool has_aka = true;

    std::vector<Client> clients;
    std::map<int, std::vector<Client*>> observe_info;  // who -> observers
    std::map<std::string, std::pair<int, Client*>> observers;  // username -> (who, client)

    int current_player = 0;
    bool game_start = false;
    int AI_count = 0;
    int min_score = 0;
    bool fast = false;
    bool allow_observe = true;

    GameEnvironment(bool _has_aka = true, int _ai_count = 0, int _min_score = 0,
                    bool _fast = false, bool _allow_observe = true)
        : game(_has_aka), has_aka(_has_aka), AI_count(_ai_count),
          min_score(_min_score), fast(_fast), allow_observe(_allow_observe) {
        for (int i = 0; i < AI_count; i++) {
            std::string name = "一姬" + std::to_string(i + 1) + "(简单)";
            clients.push_back(Client(-1, name));
        }
    }

    void start() {
        for (auto& c : clients) {
            if (c.is_human()) {
                c.message_queue->clear();
            }
        }
        game.new_game(round, honba, riichi_ba);
    }

    void reset() {
        LOG_INFO("Game is reset");
        game = MahjongGame(has_aka);
        round = 0;
        honba = 0;
        riichi_ba = 0;
        clients.clear();
        observe_info.clear();
        observers.clear();
        current_player = 0;
        game_start = false;
        for (int i = 0; i < AI_count; i++) {
            std::string name = "一姬" + std::to_string(i + 1) + "(简单)";
            clients.push_back(Client(-1, name));
        }
    }

    json::Value get_game_info() {
        json::Value info;
        info["round"] = game.round;
        info["honba"] = game.honba;
        info["riichi_ba"] = game.riichi_ba;
        info["oya"] = game.oya;
        info["left_num"] = game.left_num;

        json::Value dora_arr;
        for (int d : game.dora_indicator) dora_arr.push_back(d);
        info["dora_indicator"] = dora_arr;

        json::Value agents_arr;
        for (int i = 0; i < 4; i++) {
            auto& p = game.agents[i];
            json::Value ag;
            ag["username"] = clients[i].username;
            ag["score"] = p.score;
            ag["tile_count"] = static_cast<int>(p.tiles.size());

            // furo as object
            json::Value furo_obj;
            for (auto& [key, tiles] : p.furo) {
                std::string key_str;
                if (key.furo_type == 0)  // chi
                    key_str = "(0, (" + std::to_string(key.pattern) + ", " + std::to_string(key.nth) + "))";
                else if (key.furo_type == 1) // pon
                    key_str = "(1, " + std::to_string(key.pattern) + ")";
                else if (key.furo_type == 2) // ankan
                    key_str = "(2, " + std::to_string(key.pattern) + ")";
                else // minkan
                    key_str = "(3, " + std::to_string(key.pattern) + ")";

                json::Value tiles_arr;
                for (int t : tiles) tiles_arr.push_back(t);
                furo_obj[key_str] = tiles_arr;
            }
            ag["furo"] = furo_obj;

            // kui_info
            json::Value kui_arr;
            for (auto& [t, fw, kt] : p.kui_info) {
                json::Value ktup;
                ktup.push_back(t);
                ktup.push_back(fw);
                ktup.push_back(kt);
                kui_arr.push_back(ktup);
            }
            ag["kui_info"] = kui_arr;

            ag["riichi"] = p.riichi_status ? 1 : 0;
            ag["riichi_round"] = p.riichi_round;

            json::Value disc_arr;
            for (int d : p.discard_tiles) disc_arr.push_back(d);
            ag["discard"] = disc_arr;

            json::Value riv_arr;
            for (int r : p.river) riv_arr.push_back(r);
            ag["river"] = riv_arr;

            ag["riichi_tile"] = p.riichi_tile;
            ag["is_ai"] = clients[i].is_ai();
            agents_arr.push_back(ag);
        }
        info["agents"] = agents_arr;
        return info;
    }

    json::Value get_player_info(int who) {
        auto& p = game.agents[who];
        json::Value info;
        info["username"] = clients[who].username;
        info["seat"] = who;

        json::Value tiles_arr;
        for (int t : p.tiles) tiles_arr.push_back(t);
        info["tiles"] = tiles_arr;

        json::Value furo_obj;
        for (auto& [key, tiles] : p.furo) {
            std::string key_str;
            if (key.furo_type == 0)
                key_str = "(0, (" + std::to_string(key.pattern) + ", " + std::to_string(key.nth) + "))";
            else if (key.furo_type == 1)
                key_str = "(1, " + std::to_string(key.pattern) + ")";
            else if (key.furo_type == 2)
                key_str = "(2, " + std::to_string(key.pattern) + ")";
            else
                key_str = "(3, " + std::to_string(key.pattern) + ")";
            json::Value tiles_arr;
            for (int t : tiles) tiles_arr.push_back(t);
            furo_obj[key_str] = tiles_arr;
        }
        info["furo"] = furo_obj;

        json::Value machi_arr;
        for (int m : p.machi) machi_arr.push_back(m);
        info["machi"] = machi_arr;

        return info;
    }

    // Update a key for a specific client
    void update(const std::string& key, const json::Value& value, Client* client_ptr = nullptr) {
        json::Value msg;
        msg["event"] = "update";
        msg["key"] = key;
        msg["value"] = value;

        if (!client_ptr) {
            send_multiply(*this, msg);
        } else {
            if (client_ptr->is_human())
                send_personal(*client_ptr, msg);
            auto it = std::find_if(clients.begin(), clients.end(),
                [client_ptr](const Client& c) { return &c == client_ptr; });
            if (it != clients.end()) {
                int who = static_cast<int>(it - clients.begin());
                send_observers(*this, who, msg);
            }
        }
    }

    // AI decision stubs (actual AI needs Python model)
    int ai_discard(int who, const std::vector<int>& tiles, const std::vector<int>& banned) {
        // Simple: discard a random tile not in banned
        std::vector<int> candidates;
        for (int t : tiles) {
            if (std::find(banned.begin(), banned.end(), t / 4) == banned.end())
                candidates.push_back(t);
        }
        if (candidates.empty()) candidates = tiles;
        return candidates[rand() % candidates.size()];
    }

    json::Value ai_decision(int who, const std::vector<json::Value>& actions) {
        // Simple AI: prefer pass, randomly pick otherwise
        // actions[0] is always pass
        if (actions.size() == 1) return actions[0];

        // Very basic: agari if possible, otherwise pass
        for (size_t i = 1; i < actions.size(); i++) {
            if (actions[i]["type"].as_string() == "agari") {
                return actions[i];
            }
        }
        return actions[0];  // pass
    }

    // Fetch decision from client or AI
    json::Value fetch_decision(Client& client, const std::vector<json::Value>& actions, bool after_tsumo) {
        int who = static_cast<int>(std::find_if(clients.begin(), clients.end(),
            [&client](const Client& c) { return &c == &client; }) - clients.begin());

        if (client.is_human()) {
            json::Value msg = client.fetch_message();
            if (msg.contains("action")) {
                return msg["action"];
            }
        }
        return ai_decision(who, actions);
    }

    // Fetch discard from client or AI
    int fetch_discard(int who, Client& client, const std::vector<int>& tiles, const std::vector<int>& banned) {
        if (client.is_human()) {
            json::Value msg = client.fetch_message();
            if (msg.contains("tile_id")) {
                return msg["tile_id"].as_int();
            }
        }
        return ai_discard(who, tiles, banned);
    }

    int select_tile(Client& client, const json::Value& tiles_data,
                    const std::vector<int>& banned, int tsumo,
                    bool riichi_flag, bool is_riichi_tile) {
        auto it = std::find_if(clients.begin(), clients.end(),
            [&client](const Client& c) { return &c == &client; });
        int who = static_cast<int>(it - clients.begin());

        json::Value msg;
        msg["event"] = "select_tile";
        msg["tiles"] = tiles_data;
        json::Value banned_arr;
        for (int b : banned) banned_arr.push_back(b);
        msg["banned"] = banned_arr;
        msg["tsumo"] = tsumo;
        msg["riichi"] = riichi_flag;
        msg["is_riichi_tile"] = is_riichi_tile;

        send_observers(*this, who, msg);

        std::vector<int> tiles_vec;
        if (tiles_data.is_string() && tiles_data.as_string() == "all") {
            for (int t : game.agents[who].tiles) tiles_vec.push_back(t);
        } else if (tiles_data.is_array()) {
            for (auto& v : tiles_data.arr_val) tiles_vec.push_back(v.as_int());
        }

        if (client.is_human()) {
            send_personal(client, msg);
            return fetch_discard(who, client, tiles_vec, banned);
        }
        return ai_discard(who, tiles_vec, banned);
    }

    // Game update (scoring after round ends)
    bool game_update(const json::Value& res, std::vector<int>& score_delta) {
        score_delta.assign(4, 0);
        bool change_oya = true;
        honba = game.honba;
        riichi_ba = game.riichi_ba;
        int oya = game.oya;

        if (res.is_array()) {
            // Agari
            int first_winner = res[0]["who"].as_int();
            for (auto& action : res.arr_val) {
                int who = action["who"].as_int();
                int from_who = action["from_who"].as_int();
                int score = action["score"].as_int();

                if (who == from_who) {  // tsumo
                    if (who == oya) {
                        score = (score * 2 + 90) / 100;
                        for (int i = 0; i < 4; i++) {
                            if (i != oya) {
                                game.agents[i].score -= score + honba;
                                score_delta[i] -= score + honba;
                            }
                        }
                        game.agents[who].score += score * 3 + honba * 3;
                        score_delta[who] += score * 3 + honba * 3;
                        change_oya = false;
                    } else {
                        int score_oya = (score * 2 + 90) / 100;
                        score = (score + 90) / 100;
                        for (int i = 0; i < 4; i++) {
                            if (i == who) {
                                game.agents[i].score += score_oya + score * 2 + honba * 3;
                                score_delta[i] += score_oya + score * 2 + honba * 3;
                            } else if (i == oya) {
                                game.agents[i].score -= score_oya + honba;
                                score_delta[i] -= score_oya + honba;
                            } else {
                                game.agents[i].score -= score + honba;
                                score_delta[i] -= score + honba;
                            }
                        }
                    }
                } else {  // ron
                    if (who == oya) {
                        score = (score * 6 + 90) / 100 + honba * 3;
                        change_oya = false;
                    } else {
                        score = (score * 4 + 90) / 100 + honba * 3;
                    }
                    game.agents[from_who].score -= score;
                    game.agents[who].score += score;
                    score_delta[from_who] -= score;
                    score_delta[who] += score;
                }
            }
            if (riichi_ba) {
                game.agents[first_winner].score += riichi_ba * 10;
                score_delta[first_winner] += riichi_ba * 10;
            }
            riichi_ba = 0;
            if (!change_oya) honba++; else honba = 0;
        } else {
            // Ryuukyoku
            std::string why = res["why"].as_string();
            if (why == "yama_end") {
                change_oya = true; // simplified
            } else {
                change_oya = false;
            }
            honba++;
        }

        if (change_oya) round++;
        game.honba = honba;
        game.riichi_ba = riichi_ba;

        // Check game end conditions
        int min_score_val = game.agents[0].score;
        for (int i = 1; i < 4; i++) min_score_val = std::min(min_score_val, game.agents[i].score);
        if (min_score_val * 100 < min_score) return true;
        if (round > 11) return true;
        if (round > 7 || (round == 7 && !change_oya)) {
            int max_score = game.agents[0].score;
            for (int i = 1; i < 4; i++) max_score = std::max(max_score, game.agents[i].score);
            if (max_score < 300) return false;
            if (change_oya) {
                if (riichi_ba) {
                    int winner = 0;
                    for (int i = 0; i < 4; i++)
                        if (game.agents[i].score > game.agents[winner].score) winner = i;
                    game.agents[winner].score += riichi_ba * 10;
                    game.riichi_ba = 0;
                }
                return true;
            }
            auto ranks = game.get_rank();
            return ranks[0].first == oya;
        }
        return false;
    }
};

// ============================================================================
// Global send functions (after GameEnvironment is defined)
// ============================================================================
void send_personal(Client& client, const json::Value& msg) {
    client.send_msg(msg);
}

void send_multiply(GameEnvironment& env, const json::Value& msg, int except, int except_ob) {
    std::string data = json::serialize(msg) + "\n";
    for (int i = 0; i < 4; i++) {
        if (i == except || !env.clients[i].is_human()) continue;
        env.clients[i].send_msg(msg);
    }
    for (auto& [username, pair] : env.observers) {
        if (pair.first == except_ob) continue;
        pair.second->send_msg(msg);
    }
}

void send_observers(GameEnvironment& env, int who, const json::Value& msg) {
    auto it = env.observe_info.find(who);
    if (it != env.observe_info.end()) {
        for (auto* observer : it->second) {
            observer->send_msg(msg);
        }
    }
}

void send_all_game_info(GameEnvironment& env, Client* client_ptr) {
    json::Value game_info = env.get_game_info();

    if (!client_ptr) {
        for (int i = 0; i < 4; i++) {
            json::Value player_info = env.get_player_info(i);
            auto& player_client = env.clients[i];
            if (player_client.is_human()) {
                json::Value msg;
                msg["event"] = "start";
                msg["game"] = game_info;
                msg["self"] = player_info;
                send_personal(player_client, msg);
            }
            auto it = env.observe_info.find(i);
            if (it != env.observe_info.end()) {
                for (auto* obs : it->second) {
                    json::Value obs_msg;
                    obs_msg["event"] = "start";
                    obs_msg["game"] = game_info;
                    obs_msg["self"] = player_info;
                    send_personal(*obs, obs_msg);
                }
            }
        }
    } else {
        auto it = std::find_if(env.clients.begin(), env.clients.end(),
            [client_ptr](const Client& c) { return &c == client_ptr; });
        if (it != env.clients.end()) {
            int who = static_cast<int>(it - env.clients.begin());
            json::Value player_info = env.get_player_info(who);
            json::Value msg;
            msg["event"] = "start";
            msg["game"] = game_info;
            msg["self"] = player_info;
            send_personal(*client_ptr, msg);
        } else {
            auto obs_it = env.observers.find(client_ptr->username);
            if (obs_it != env.observers.end()) {
                json::Value player_info = env.get_player_info(obs_it->second.first);
                json::Value msg;
                msg["event"] = "start";
                msg["game"] = game_info;
                msg["self"] = player_info;
                send_personal(*obs_it->second.second, msg);
            }
        }
    }
}

// ============================================================================
// Player join/leave logic
// ============================================================================
std::pair<bool, Client*> player_join(GameEnvironment& env, SOCKET sock,
                                      const std::string& username, bool observe) {
    static int uuid_counter = 0;

    if (observe) {
        if (!env.allow_observe) {
            json::Value resp;
            resp["event"] = "join";
            resp["status"] = 0;
            resp["message"] = "服务端未开启观战";
            std::string data = json::serialize(resp) + "\n";
            ::send(sock, data.c_str(), static_cast<int>(data.size()), 0);
            return {false, nullptr};
        }
        std::string obs_user = username;
        if (obs_user.empty() && !env.clients.empty()) {
            obs_user = env.clients[rand() % env.clients.size()].username;
        }
        auto client_it = std::find_if(env.clients.begin(), env.clients.end(),
            [&obs_user](const Client& c) { return c.username == obs_user; });
        if (client_it != env.clients.end()) {
            int idx = static_cast<int>(client_it - env.clients.begin());
            std::string uid = "obs_" + std::to_string(++uuid_counter);
            Client* c = new Client(sock, uid);
            env.observers[uid] = {idx, c};
            env.observe_info[idx].push_back(c);

            json::Value resp;
            resp["event"] = "join";
            resp["status"] = -1;
            resp["message"] = "成功加入观战位";
            std::string data = json::serialize(resp) + "\n";
            ::send(sock, data.c_str(), static_cast<int>(data.size()), 0);

            if (env.game_start)
                send_all_game_info(env, c);
            return {true, c};
        }
        return {false, nullptr};
    }

    // Player join
    if (username.size() > 8) {
        json::Value resp;
        resp["event"] = "join";
        resp["status"] = 0;
        resp["message"] = "用户名长度不能超过8";
        std::string data = json::serialize(resp) + "\n";
        ::send(sock, data.c_str(), static_cast<int>(data.size()), 0);
        return {false, nullptr};
    }

    auto client_it = std::find_if(env.clients.begin(), env.clients.end(),
        [&username](const Client& c) { return c.username == username; });
    if (client_it != env.clients.end()) {
        if (client_it->is_human()) {
            if (!env.game_start) {
                json::Value resp;
                resp["event"] = "join";
                resp["status"] = 0;
                resp["message"] = "用户名已被占用";
                std::string data = json::serialize(resp) + "\n";
                ::send(sock, data.c_str(), static_cast<int>(data.size()), 0);
                return {false, nullptr};
            }
            if (client_it->disconnected) {
                client_it->sock = sock;
                client_it->disconnected = false;
                client_it->message_queue->clear();

                json::Value resp;
                resp["event"] = "join";
                resp["status"] = 1;
                resp["message"] = "欢迎重新加入游戏！";
                std::string data = json::serialize(resp) + "\n";
                ::send(sock, data.c_str(), static_cast<int>(data.size()), 0);
                send_all_game_info(env, &(*client_it));
                return {true, &(*client_it)};
            }
        }
    }

    if (env.clients.size() >= 4) {
        if (env.allow_observe) {
            std::string uid = "obs_" + std::to_string(++uuid_counter);
            Client* c = new Client(sock, uid);
            int idx = rand() % 4;
            env.observers[uid] = {idx, c};
            env.observe_info[idx].push_back(c);

            json::Value resp;
            resp["event"] = "join";
            resp["status"] = -1;
            resp["message"] = "房间人数已满，已加入观战位";
            std::string data = json::serialize(resp) + "\n";
            ::send(sock, data.c_str(), static_cast<int>(data.size()), 0);
            send_all_game_info(env, c);
            return {true, c};
        }
        json::Value resp;
        resp["event"] = "join";
        resp["status"] = 0;
        resp["message"] = "房间人数已满";
        std::string data = json::serialize(resp) + "\n";
        ::send(sock, data.c_str(), static_cast<int>(data.size()), 0);
        return {false, nullptr};
    }

    std::string name = username;
    if (name.empty()) {
        for (int i = 1; ; i++) {
            name = "匿名玩家" + std::to_string(i);
            bool found = false;
            for (auto& c : env.clients) {
                if (c.username == name) { found = true; break; }
            }
            if (!found) break;
        }
    }

    LOG_INFO("username: %s join", name.c_str());
    env.clients.push_back(Client(sock, name));

    json::Value resp;
    resp["event"] = "join";
    resp["status"] = 1;
    resp["message"] = "成功加入房间, 您的用户名为「" + name + "」";
    std::string data = json::serialize(resp) + "\n";
    ::send(sock, data.c_str(), static_cast<int>(data.size()), 0);

    if (env.clients.size() < 4) {
        json::Value broadcast;
        broadcast["event"] = "join";
        broadcast["message"] = "当前人数:" + std::to_string(env.clients.size()) + ", 等待其他玩家加入...";
        send_multiply(env, broadcast);
    }

    return {true, &env.clients.back()};
}

void player_disconnect(GameEnvironment& env, Client& client) {
    LOG_INFO("%s leave", client.username.c_str());
    auto it = std::find_if(env.clients.begin(), env.clients.end(),
        [&client](const Client& c) { return &c == &client; });
    if (it != env.clients.end()) {
        if (!env.game_start) {
            client.close();
            env.clients.erase(it);
            json::Value msg;
            msg["event"] = "quit";
            msg["message"] = "当前人数:" + std::to_string(env.clients.size()) + ",等待其他玩家加入...";
            send_multiply(env, msg);
        } else {
            int idx = static_cast<int>(it - env.clients.begin());
            client.close();
            env.clients[idx].disconnected = true;
        }
    } else {
        auto obs_it = env.observers.find(client.username);
        if (obs_it != env.observers.end()) {
            client.close();
            int who = obs_it->second.first;
            auto& obs_list = env.observe_info[who];
            obs_list.erase(std::remove(obs_list.begin(), obs_list.end(), &client), obs_list.end());
            env.observers.erase(obs_it);
        }
    }
}

// ============================================================================
// Game loop (simplified C++ version of the async Python game_loop)
// ============================================================================
struct GameAction {
    std::string type;
    int who = -1;
    int from_who = -1;
    int tile_id = -1;
    std::vector<int> pattern;
    int machi = -1;
    json::Value as_json() const {
        json::Value v;
        v["type"] = type;
        v["who"] = who;
        v["from_who"] = from_who;
        if (!pattern.empty()) {
            json::Value p;
            for (int x : pattern) p.push_back(x);
            v["pattern"] = p;
        }
        if (machi >= 0) v["machi"] = machi;
        return v;
    }
};

// Game loop - runs in its own thread
void game_main_loop(GameEnvironment& env) {
    env.game_start = true;
    // Shuffle clients
    std::random_device rd;
    std::mt19937 g_rng(rd());
    std::shuffle(env.clients.begin(), env.clients.end(), g_rng);

    while (env.game_start) {
        env.start();
        send_all_game_info(env);

        // Simple round: just play through
        int wind_idx = env.game.round / 4;
        int wind_round = env.game.round % 4 + 1;
        LOG_INFO("%c%d局 - %d本场------场供: %d",
            "东南西北"[wind_idx], wind_round, env.game.honba, env.game.riichi_ba * 1000);

        // --- Basic game round simulation ---
        // Deal initial draws, process turns
        env.current_player = env.game.oya;

        // Simplified game loop: each player draws and discards
        std::vector<int> score_delta;
        bool game_over = false;

        for (int turn = 0; turn < 52 && env.game.left_num > 0; turn++) {
            int who = env.current_player;
            auto& agent = env.game.agents[who];
            auto& client = env.clients[who];

            // Draw
            int tile_id = env.game.draw_tile(who);

            json::Value draw_msg;
            draw_msg["event"] = "draw";
            draw_msg["who"] = who;
            draw_msg["tile_id"] = tile_id;
            draw_msg["where"] = 0;

            if (client.is_human())
                send_personal(client, draw_msg);
            send_observers(env, who, draw_msg);

            json::Value draw_broadcast;
            draw_broadcast["event"] = "draw";
            draw_broadcast["who"] = who;
            draw_broadcast["where"] = 0;
            send_multiply(env, draw_broadcast, who, who);

            json::Value left_update;
            left_update["event"] = "update";
            left_update["key"] = "left_num";
            left_update["value"] = env.game.left_num;
            send_multiply(env, left_update);

            // Select tile to discard
            json::Value tiles_val("all");
            int discard_tile = env.select_tile(client, tiles_val, {}, tile_id, false, false);
            bool mode = (discard_tile == tile_id);

            // Discard
            env.game.discard_tile(who, discard_tile);

            json::Value furiten_update;
            furiten_update["event"] = "update";
            furiten_update["key"] = "furiten";
            furiten_update["value"] = agent.is_furiten();
            send_personal(client, furiten_update);

            json::Value machi_update;
            machi_update["event"] = "update";
            machi_update["key"] = "machi";
            json::Value machi_arr;
            for (int m : agent.machi) machi_arr.push_back(m);
            machi_update["value"] = machi_arr;
            send_personal(client, machi_update);

            json::Value disc_msg;
            disc_msg["event"] = "discard";
            disc_msg["who"] = who;
            disc_msg["tile_id"] = discard_tile;
            disc_msg["mode"] = mode ? 1 : 0;
            disc_msg["after_tsumo"] = true;
            disc_msg["is_riichi"] = false;
            send_multiply(env, disc_msg, who);

            // Broadcast wait
            json::Value wait_msg;
            wait_msg["event"] = "wait";
            wait_msg["message"] = "等待他人响应...";
            send_multiply(env, wait_msg);

            // Sleep briefly (not fast mode)
            if (!env.fast) {
                std::this_thread::sleep_for(std::chrono::milliseconds(400));
            }

            // Advance player
            env.current_player = (env.current_player + 1) % 4;

            // Check if end of game
            if (env.game.left_num <= 0) break;
        }

        // Simplified end-of-round
        json::Value ryuukyoku;
        ryuukyoku["event"] = "ryuukyoku";
        ryuukyoku["why"] = "yama_end";
        ryuukyoku["nagashimangan"] = json::Array{};
        json::Value machi_state;
        send_multiply(env, ryuukyoku);

        game_over = env.game_update(ryuukyoku, score_delta);

        // Settlement
        if (!env.fast) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }

        json::Value settlement;
        settlement["event"] = "settlement";
        settlement["res"] = ryuukyoku;
        json::Value sd_arr;
        for (int d : score_delta) sd_arr.push_back(d);
        settlement["score"] = sd_arr;
        json::Value ura_arr;
        for (int d : env.game.ura_dora_indicator) ura_arr.push_back(d);
        settlement["ura_dora"] = ura_arr;
        send_multiply(env, settlement);

        if (game_over) {
            LOG_INFO("游戏结束！");
            for (auto& [i, score] : env.game.get_rank()) {
                LOG_INFO("「%s」积分「%d」", env.clients[i].username.c_str(), score * 100);
            }
            env.game_start = false;

            json::Value score_msg;
            score_msg["event"] = "score";
            json::Value rank_arr;
            for (auto& [i, score] : env.game.get_rank()) {
                json::Value pair;
                pair.push_back(i);
                pair.push_back(score);
                rank_arr.push_back(pair);
            }
            score_msg["score"] = rank_arr;
            send_multiply(env, score_msg);

            json::Value end_msg;
            end_msg["event"] = "end";
            end_msg["message"] = "游戏结束！请重新加入房间～";
            send_multiply(env, end_msg);

            for (auto& c : env.clients) c.close();
            env.reset();
            break;
        }
    }
}

// ============================================================================
// Server (mirrors Python Server)
// ============================================================================
struct Server {
    SOCKET server_socket = INVALID_SOCKET;
    std::string host;
    int port;
    int AI_count;
    int min_score;
    bool fast;
    bool allow_observe;
    int handshake_timeout = 5;
    int max_message_size = 64 * 1024;
    GameEnvironment game_env;
    std::atomic<bool> running{true};
    std::mutex game_mutex;
    std::unique_ptr<std::thread> game_thread_ptr;

    Server(const std::string& h, int p, int ai, int ms, bool f, bool ao)
        : host(h), port(p), AI_count(ai), min_score(ms), fast(f), allow_observe(ao),
          game_env(true, ai, ms, f, ao) {
        LOG_INFO("Server running at %s:%d with %d AI...", host.c_str(), port, ai);
    }

    ~Server() {
        running = false;
        if (server_socket != INVALID_SOCKET) closesocket(server_socket);
    }

    std::string recv_socket(SOCKET sock, int max_bytes = -1) {
        if (max_bytes < 0) max_bytes = max_message_size;
        std::string buffer;
        buffer.reserve(max_bytes);
        while (static_cast<int>(buffer.size()) < max_bytes) {
            char ch;
            int n = recv(sock, &ch, 1, 0);
            if (n <= 0) break;
            if (ch == '\n') break;
            buffer += ch;
        }
        if (static_cast<int>(buffer.size()) >= max_bytes)
            throw std::runtime_error("Message exceeds size limit");
        return buffer;
    }

    void handle_client(Client& client) {
        fd_set readfds;
        struct timeval tv;
        while (running) {
            FD_ZERO(&readfds);
            FD_SET(client.sock, &readfds);
            tv.tv_sec = 0;
            tv.tv_usec = 100000;

            int ret = select(static_cast<int>(client.sock) + 1, &readfds, nullptr, nullptr, &tv);
            if (ret < 0) break;
            if (ret == 0) continue;

            try {
                std::string data = recv_socket(client.sock);
                if (data.empty()) {
                    json::Value quit_msg;
                    quit_msg["event"] = "quit";
                    client.message_queue->put(quit_msg);
                    break;
                }
                json::Value msg = json::parse(data);
                std::string event;
                if (msg.contains("event")) event = msg["event"].as_string();

                LOG_DEBUG("Recv: %s", data.c_str());

                if (event == "quit" || event == "discard" || event == "decision" || event == "ready") {
                    client.message_queue->put(msg);
                }
                if (event == "quit") break;

                if (event == "change_ob") {
                    std::lock_guard<std::mutex> lk(game_mutex);
                    if (!game_env.observers.count(client.username)) continue;
                    std::string target_user;
                    if (msg.contains("username")) target_user = msg["username"].as_string();
                    auto target_it = std::find_if(game_env.clients.begin(), game_env.clients.end(),
                        [&target_user](const Client& c) { return c.username == target_user; });
                    if (target_it == game_env.clients.end()) continue;
                    int target = static_cast<int>(target_it - game_env.clients.begin());
                    auto& obs_pair = game_env.observers[client.username];
                    int old_who = obs_pair.first;
                    obs_pair = {target, &client};
                    auto& obs_list = game_env.observe_info[old_who];
                    obs_list.erase(std::remove(obs_list.begin(), obs_list.end(), &client), obs_list.end());
                    game_env.observe_info[target].push_back(&client);
                    send_all_game_info(game_env, &client);
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Exception in handle_client: %s", e.what());
                json::Value quit_msg;
                quit_msg["event"] = "quit";
                client.message_queue->put(quit_msg);
                break;
            }
        }
        std::lock_guard<std::mutex> lk(game_mutex);
        player_disconnect(game_env, client);
    }

    void start_game_thread() {
        if (game_thread_ptr && game_thread_ptr->joinable()) {
            game_thread_ptr->join();
        }
        game_thread_ptr = std::make_unique<std::thread>([this]() {
            std::lock_guard<std::mutex> lk(game_mutex);
            game_main_loop(game_env);
        });
    }

    void handle_connections() {
        while (running) {
            SOCKET client_sock = accept(server_socket, nullptr, nullptr);
            if (client_sock == INVALID_SOCKET) {
                if (!running) break;
                continue;
            }

            // Set timeout
            struct timeval tv;
            tv.tv_sec = handshake_timeout;
            tv.tv_usec = 0;
            setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

            try {
                fd_set readfds;
                FD_ZERO(&readfds);
                FD_SET(client_sock, &readfds);
                tv.tv_sec = handshake_timeout;
                tv.tv_usec = 0;
                int ret = select(static_cast<int>(client_sock) + 1, &readfds, nullptr, nullptr, &tv);
                if (ret <= 0) {
                    closesocket(client_sock);
                    continue;
                }

                std::string payload = recv_socket(client_sock);
                if (payload.empty()) {
                    closesocket(client_sock);
                    continue;
                }
                json::Value msg = json::parse(payload);
                std::string username;
                if (msg.contains("username")) username = msg["username"].as_string();
                bool observe = false;
                if (msg.contains("observe")) observe = msg["observe"].as_bool();

                // Set back to blocking with no timeout
                tv.tv_sec = 0;
                tv.tv_usec = 0;
                setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

                // Set TCP_NODELAY
                int flag = 1;
                setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&flag, sizeof(flag));

                std::lock_guard<std::mutex> lk(game_mutex);
                auto [success, client_ptr] = player_join(game_env, client_sock, username, observe);
                if (success && client_ptr && client_ptr->is_human()) {
                    std::thread(&Server::handle_client, this, std::ref(*client_ptr)).detach();
                } else if (!success) {
                    closesocket(client_sock);
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Handshake error: %s", e.what());
                closesocket(client_sock);
            }
        }
    }

    void run() {
        std::thread conn_thread(&Server::handle_connections, this);

        while (running) {
            {
                std::lock_guard<std::mutex> lk(game_mutex);
                if (game_env.clients.size() >= 4 && !game_env.game_start) {
                    start_game_thread();
                    if (game_thread_ptr && game_thread_ptr->joinable()) {
                        game_thread_ptr->join();
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        conn_thread.join();
    }
};

// ============================================================================
// Global server pointer for signal handling
// ============================================================================
static Server* g_server = nullptr;

// ============================================================================
// Main
// ============================================================================
void print_usage(const char* prog) {
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -H, --host HOST       Server host (default: 0.0.0.0)\n");
    printf("  -P, --port PORT       Server port (default: 9999)\n");
    printf("  -A, --AI COUNT        Number of AI players (default: 0)\n");
    printf("  -m, --min-score N     Minimum score (default: 0)\n");
    printf("  -ob, --allow-observe  Allow observing\n");
    printf("  -f, --fast            Cancel AI thinking time\n");
    printf("  -d, --debug           Print debug information\n");
    printf("  -h, --help            Show this help\n");
}

Server* create_server(int argc, char* argv[]) {
    std::string host = "0.0.0.0";
    int port = 9999;
    int ai_count = 0;
    int min_score = 0;
    bool fast = false;
    bool allow_observe = false;
    bool debug = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "-H" || arg == "--host") && i + 1 < argc) {
            host = argv[++i];
        } else if ((arg == "-P" || arg == "--port") && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if ((arg == "-A" || arg == "--AI") && i + 1 < argc) {
            ai_count = std::stoi(argv[++i]);
        } else if ((arg == "-m" || arg == "--min-score") && i + 1 < argc) {
            min_score = std::stoi(argv[++i]);
        } else if (arg == "-ob" || arg == "--allow-observe") {
            allow_observe = true;
        } else if (arg == "-f" || arg == "--fast") {
            fast = true;
        } else if (arg == "-d" || arg == "--debug") {
            debug = true;
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return nullptr;
        }
    }

    if (debug) log_set_level(LOG_DEBUG);

    return new Server(host, port, ai_count, min_score, fast, allow_observe);
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

    srand(static_cast<unsigned>(time(nullptr)));

    auto* server = create_server(argc, argv);
    if (!server) return 0;

    g_server = server;

    // Create listening socket
    server->server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server->server_socket == INVALID_SOCKET) {
        LOG_ERROR("Failed to create socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server->server_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(server->port));
    if (server->host == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        addr.sin_addr.s_addr = inet_addr(server->host.c_str());
    }

    if (bind(server->server_socket, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        LOG_ERROR("Bind failed");
        return 1;
    }

    if (listen(server->server_socket, 5) == SOCKET_ERROR) {
        LOG_ERROR("Listen failed");
        return 1;
    }

    LOG_INFO("Server listening on %s:%d", server->host.c_str(), server->port);

#ifdef _WIN32
    // Windows signal handling via SetConsoleCtrlHandler would be needed for proper cleanup
#endif

    server->run();

    delete server;

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}