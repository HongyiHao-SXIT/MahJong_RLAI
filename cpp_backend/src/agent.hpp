#pragma once
// Agent — player state (adapted from server.cpp)

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

// Tile type constants
constexpr int TOTAL_TILES = 136;
constexpr int NUM_TILE_TYPES = 34;  // man(0-8) pin(9-17) sou(18-26) wind(27-30) dragon(31-33)
constexpr int AKA_MAN5 = 16;
constexpr int AKA_PIN5 = 52;
constexpr int AKA_SOU5 = 88;

inline int tile_type(int tile_id) { return tile_id / 4; }
inline int tile_number(int tile_id) { return (tile_id / 4) % 9; }
inline int tile_suit(int t) { return t / 9; }
inline bool is_aka(int tile_id) {
    return tile_id == AKA_MAN5 || tile_id == AKA_PIN5 || tile_id == AKA_SOU5;
}

struct FuroKey {
    int furo_type; // 0=chi, 1=pon, 2=ankan, 3=minkan
    int pattern;   // tile type
    int nth = -1;  // for chi ordering
    bool operator<(const FuroKey& o) const {
        if (furo_type != o.furo_type) return furo_type < o.furo_type;
        if (pattern != o.pattern) return pattern < o.pattern;
        return nth < o.nth;
    }
};

struct Agent {
    int score = 250;          // score / 100
    std::set<int> tiles;      // tile IDs (0-135)
    std::vector<int> hand_tile_counter;  // 34-element count per tile type
    std::vector<int> discard_tiles;
    std::vector<int> river;

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

    Agent() : hand_tile_counter(34, 0) {}
    Agent(int s, const std::set<int>& t, int seat)
        : score(s), tiles(t), hand_tile_counter(34, 0) {
        for (int tid : tiles) hand_tile_counter[tid / 4]++;
        menfon = 27 + seat;
    }

    bool is_furiten() const { return discard_furiten || riichi_furiten || round_furiten; }

    void draw(int tile_id) {
        tiles.insert(tile_id);
        hand_tile_counter[tile_id / 4]++;
    }

    void discard(int tile_id) {
        int t = tile_type(tile_id);
        tiles.erase(tile_id);
        discard_tiles.push_back(tile_id);
        river.push_back(tile_id);
        hand_tile_counter[t]--;
    }

    void pon(const std::vector<int>& tile_ids, int kui_tile, int from_who) {
        kui = true;
        int t = tile_type(tile_ids[0]);
        FuroKey key{1, t};
        furo[key] = tile_ids;
        kui_info.push_back({1, from_who, kui_tile});
        for (int tid : tile_ids) {
            if (tid != kui_tile) tiles.erase(tid);
        }
        for (int tid : tile_ids) hand_tile_counter[tile_type(tid)]--;
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
        for (int tid : tile_ids) hand_tile_counter[tile_type(tid)]--;
        discard_tiles.pop_back();
        river.pop_back();
    }

    void kan(const std::vector<int>& tile_ids, int add, int kui_tile, int from_who, int mode) {
        int t = tile_type(tile_ids[0]);
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
            furo.erase({1, t});
            FuroKey key{3, t};
            std::vector<int> kan_tiles = {add};
            for (int tid : {t*4, t*4+1, t*4+2, t*4+3}) {
                if (tid != add && tiles.count(tid)) kan_tiles.push_back(tid);
            }
            furo[key] = kan_tiles;
            for (int tid : kan_tiles) {
                if (tid != add) tiles.erase(tid);
            }
            kui_info.push_back({2, -1, add});
        }
        hand_tile_counter.assign(34, 0);
        for (int tid : tiles) hand_tile_counter[tile_type(tid)]++;
    }

    bool can_pon(int tile_id) {
        int t = tile_type(tile_id);
        return hand_tile_counter[t] >= 2;
    }

    auto check_chi_helper(int t, int kui_t) -> std::pair<std::vector<int>, bool> {
        std::vector<int> patterns;
        int s = tile_suit(t);
        if (s >= 3) return {patterns, false};
        int n = tile_number(kui_t);
        if (n >= 2 && hand_tile_counter[t - 2] > 0 && hand_tile_counter[t - 1] > 0)
            patterns.push_back(t - 2);
        if (n >= 1 && n <= 7 && hand_tile_counter[t - 1] > 0 && hand_tile_counter[t + 1] > 0)
            patterns.push_back(t - 1);
        if (n <= 6 && hand_tile_counter[t + 1] > 0 && hand_tile_counter[t + 2] > 0)
            patterns.push_back(t);
        return {patterns, !patterns.empty()};
    }

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
