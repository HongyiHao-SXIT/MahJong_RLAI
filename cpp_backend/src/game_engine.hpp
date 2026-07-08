#pragma once
// Game Engine — MahjongGame, GameEnvironment, game loop (adapted from server.cpp)

#include "agent.hpp"
#include "json.hpp"
#include "log.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

// ----------------------------------------------------------------
// Thread-safe queue (mirrors Python ControlledQueue)
// ----------------------------------------------------------------
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

// ----------------------------------------------------------------
// Forward declarations
// ----------------------------------------------------------------
struct GameEnvironment;

// ----------------------------------------------------------------
// Action helpers
// ----------------------------------------------------------------
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

// ----------------------------------------------------------------
// Forward declare the callback type used by GameEnvironment
// ----------------------------------------------------------------
class GameClient {
public:
    virtual ~GameClient() = default;
    virtual void send_json(const json::Value& msg) = 0;
    virtual json::Value fetch_message() = 0;
    virtual void set_queue_allow_put() = 0;
    virtual void clear_queue() = 0;
    virtual bool is_connected() const = 0;
    virtual bool is_ai() const = 0;
    virtual std::string username() const = 0;
    virtual int seat() const = 0;
    virtual void set_seat(int s) = 0;
};

// ----------------------------------------------------------------
// MahjongGame — core game state machine
// ----------------------------------------------------------------
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
    std::vector<int> public_visible_tiles;
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
        if (s == 3) return (s * 9 + (n + 1) % 4) * 4;
        if (s == 4) return (s * 9 + (n + 1) % 3) * 4;
        return (s * 9 + (n + 1) % 9) * 4;
    }

    static int get_dora(int indicator) { return get_dora_indicator(indicator); }

    void new_game(int game_round, int _honba, int _riichi_ba) {
        round = game_round;
        honba = _honba;
        riichi_ba = _riichi_ba;
        round_wind = 27 + (game_round / 4);
        oya = game_round % 4;
        kang_num.assign(4, 0);
        first_round = true;

        yama.resize(TOTAL_TILES);
        for (int i = 0; i < TOTAL_TILES; i++) yama[i] = i;
        std::shuffle(yama.begin(), yama.end(), rng);

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

        for (int i = 0; i < 4; i++) {
            int seat = (oya + i) % 4;
            std::set<int> hand;
            for (int j = 0; j < 4; j++) {
                hand.insert(yama[i * 4 + j]);
                hand.insert(yama[i * 4 + 16 + j]);
                hand.insert(yama[i * 4 + 32 + j]);
            }
            hand.insert(yama[i + 48]);
            agents[seat] = Agent(agents[seat].score, hand, seat);
        }

        yama.erase(yama.begin(), yama.begin() + 52);

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
            int total_kang = kang_num[0] + kang_num[1] + kang_num[2] + kang_num[3];
            dora_val = yama[TOTAL_TILES - 6 - 2 * static_cast<int>(dora.size()) + total_kang];
        }
        int total_kang = kang_num[0] + kang_num[1] + kang_num[2] + kang_num[3];
        int ura_val = yama[TOTAL_TILES - 5 - 2 * static_cast<int>(dora.size()) + total_kang - 1];
        ura_dora_indicator.push_back(ura_val);
        ura_dora.push_back(get_dora(ura_val));
        dora_indicator.push_back(dora_val);
        dora.push_back(get_dora(dora_val));
        public_visible_tiles[dora_val / 4]++;
    }

    int draw_tile(int who, int tile_id = -1, int where = 0) {
        if (tile_id < 0) {
            int total_kang = kang_num[0] + kang_num[1] + kang_num[2] + kang_num[3];
            if (where == -1) {
                where = (total_kang % 2 == 0) ? -1 : -2;
            }
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
        if (!agents[who].furo.empty()) return false;
        return true;
    }

    void riichi(int who, bool double_riichi = false) {
        agents[who].riichi_status = 1;
        agents[who].riichi_round = static_cast<int>(agents[who].discard_tiles.size()) + 1;
        agents[who].ippatsu_status = 1;
        riichi_ba++;
    }

    void chi(int who, const std::vector<int>& tile_ids, int kui_tile, int from_who) {
        for (int tid : tile_ids)
            if (tid != kui_tile) public_visible_tiles[tid / 4]++;
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

    bool check_pon(int who, int tile_id) {
        if (left_num == 0) return false;
        return agents[who].can_pon(tile_id);
    }

    auto check_chi(int who, int tile_id) -> std::pair<bool, std::vector<int>> {
        if (left_num == 0) return {false, {}};
        auto [patterns, ok] = agents[who].check_chi_helper(tile_type(tile_id), tile_type(tile_id));
        return {ok, patterns};
    }

    auto check_kan(int who, int tile_id, int mode) -> std::pair<bool, std::vector<int>> {
        if (left_num == 0) return {false, {}};
        int t = tile_type(tile_id);
        if (mode == 0) {
            if (agents[who].hand_tile_counter[t] == 4) return {true, {t}};
            return {false, {}};
        }
        if (mode == 2) {
            for (auto& [key, tiles] : agents[who].furo) {
                if (key.furo_type == 1 && key.pattern == t) return {true, {t}};
            }
            return {false, {}};
        }
        if (agents[who].hand_tile_counter[t] >= 3) return {true, {t}};
        return {false, {}};
    }

    std::vector<int> search_furo_pon(int who, int t, int kui_tile) {
        return agents[who].search_furo_pon(t, kui_tile);
    }

    std::vector<int> search_furo_chi(int who, int min_t, int kui_tile) {
        return agents[who].search_furo_chi(min_t, kui_tile);
    }
};

// ----------------------------------------------------------------
// GameEnvironment — game management + client interaction
// ----------------------------------------------------------------
struct GameEnvironment {
    MahjongGame game;
    int round = 0;
    int honba = 0;
    int riichi_ba = 0;
    bool has_aka = true;

    std::vector<std::unique_ptr<GameClient>> clients;
    // For observer mode support (simplified)

    int current_player = 0;
    bool game_start = false;
    int AI_count = 0;
    int min_score = 0;
    bool fast = false;
    bool allow_observe = true;

    // Callbacks for sending messages
    std::function<void(GameClient&, const json::Value&)> send_personal;
    std::function<void(GameEnvironment&, const json::Value&, int, int)> send_multiply;

    GameEnvironment(bool _has_aka = true, int _ai_count = 0, int _min_score = 0,
                    bool _fast = false, bool _allow_observe = true)
        : game(_has_aka), has_aka(_has_aka), AI_count(_ai_count),
          min_score(_min_score), fast(_fast), allow_observe(_allow_observe) {}

    void start() {
        for (auto& c : clients) {
            c->clear_queue();
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
        current_player = 0;
        game_start = false;
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
            ag["username"] = clients[i]->username();
            ag["score"] = p.score;
            ag["tile_count"] = static_cast<int>(p.tiles.size());

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
            ag["furo"] = furo_obj;

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
            ag["is_ai"] = clients[i]->is_ai();
            agents_arr.push_back(ag);
        }
        info["agents"] = agents_arr;
        return info;
    }

    json::Value get_player_info(int who) {
        auto& p = game.agents[who];
        json::Value info;
        info["username"] = clients[who]->username();
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
            json::Value tiles_arr2;
            for (int t : tiles) tiles_arr2.push_back(t);
            furo_obj[key_str] = tiles_arr2;
        }
        info["furo"] = furo_obj;

        json::Value machi_arr;
        for (int m : p.machi) machi_arr.push_back(m);
        info["machi"] = machi_arr;
        return info;
    }

    bool game_update(const json::Value& res, std::vector<int>& score_delta) {
        score_delta.assign(4, 0);
        bool change_oya = true;
        honba = game.honba;
        riichi_ba = game.riichi_ba;
        int oya = game.oya;

        if (res.is_array()) {
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
                    if (who == oya)
                        score = (score * 6 + 90) / 100 + honba * 3;
                    else
                        score = (score * 4 + 90) / 100 + honba * 3;
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
            std::string why = res["why"].as_string();
            change_oya = (why == "yama_end");
            honba++;
        }

        if (change_oya) round++;
        game.honba = honba;
        game.riichi_ba = riichi_ba;

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

// ----------------------------------------------------------------
// Helper: build actions for a player after someone discards
// ----------------------------------------------------------------
inline json::Value build_decision_actions(MahjongGame& game, int who, int discarder, int tile_id) {
    json::Value actions;
    // Always include "pass" as action 0
    json::Value pa;
    pa["type"] = "pass";
    actions.push_back(pa);

    int t = tile_type(tile_id);

    // Check ron (agari) — simplified: just offer if player has any machi matching
    // In full implementation this would check yaku. For now we offer ron
    // to non-riichi players whose hand_counter[t] allows completing a pair or set
    // (This is a very rough approximation; real agari checking uses the pkl tables.)

    // Check pon
    if (game.check_pon(who, tile_id)) {
        json::Value act;
        act["type"] = "pon";
        act["who"] = who;
        act["from_who"] = discarder;
        auto pattern = game.search_furo_pon(who, t, tile_id);
        json::Value pat;
        for (int x : pattern) pat.push_back(x);
        act["pattern"] = pat;
        actions.push_back(act);
    }

    // Check kan (minkan)
    auto [kan_ok, kan_pat] = game.check_kan(who, tile_id, 1);
    if (kan_ok) {
        json::Value act;
        act["type"] = "kan";
        act["who"] = who;
        act["from_who"] = discarder;
        json::Value kan_arr;
        kan_arr.push_back(1); // minkan
        kan_arr.push_back(kan_pat[0]);
        kan_arr.push_back(0);
        act["pattern"] = kan_arr;
        actions.push_back(act);
    }

    // Check chi (only for the next player after discarder)
    if (who == (discarder + 1) % 4) {
        auto [chi_ok, chi_pats] = game.check_chi(who, tile_id);
        for (int p : chi_pats) {
            auto furo_tiles = game.search_furo_chi(who, p, tile_id);
            json::Value act;
            act["type"] = "chi";
            act["who"] = who;
            act["from_who"] = discarder;
            json::Value pat;
            for (int x : furo_tiles) pat.push_back(x);
            act["pattern"] = pat;
            actions.push_back(act);
        }
    }

    return actions;
}

// ----------------------------------------------------------------
// AI fallback helpers
// ----------------------------------------------------------------
inline json::Value ai_decision_fallback(int who, const json::Value& actions) {
    // If actions has agari, take it. Otherwise pass.
    for (size_t i = 0; i < actions.size(); i++) {
        if (actions[i]["type"].as_string() == "agari")
            return actions[i];
    }
    return actions[0]; // pass
}

inline int ai_discard_fallback(int who, const std::vector<int>& tiles,
                                const std::vector<int>& banned) {
    std::vector<int> candidates;
    for (int t : tiles) {
        if (std::find(banned.begin(), banned.end(), t / 4) == banned.end())
            candidates.push_back(t);
    }
    if (candidates.empty()) candidates = tiles;
    return candidates[rand() % candidates.size()];
}

// ----------------------------------------------------------------
// Game main loop — full version with decision phase
// ----------------------------------------------------------------
void game_main_loop(GameEnvironment& env) {
    env.game_start = true;

    // Shuffle client order & set seats
    {
        std::random_device rd;
        std::mt19937 g_rng(rd());
        std::shuffle(env.clients.begin(), env.clients.end(), g_rng);
    }
    for (int i = 0; i < 4; i++) env.clients[i]->set_seat(i);

    while (env.game_start) {
        env.start();

        // Send game info to all
        json::Value game_info = env.get_game_info();
        for (int i = 0; i < 4; i++) {
            json::Value msg;
            msg["event"] = "start";
            msg["game"] = game_info;
            msg["self"] = env.get_player_info(i);
            env.send_personal(*env.clients[i], msg);
        }

        int wind_idx = env.game.round / 4;
        int wind_round = env.game.round % 4 + 1;
        LOG_INFO("%c%d局 - %d本场 场供: %d",
            "东南西北"[wind_idx], wind_round, env.game.honba, env.game.riichi_ba * 1000);

        env.current_player = env.game.oya;

        std::vector<int> score_delta;
        bool round_over = false;
        bool first_turn = true;

        while (!round_over && env.game.left_num > 0) {
            int who = env.current_player;
            auto& agent = env.game.agents[who];
            auto& client = *env.clients[who];

            // ---- Draw ----
            int tile_id = env.game.draw_tile(who);

            json::Value draw_msg;
            draw_msg["event"] = "draw";
            draw_msg["who"] = who;
            draw_msg["tile_id"] = tile_id;
            draw_msg["where"] = 0;
            env.send_personal(client, draw_msg);

            json::Value draw_broadcast;
            draw_broadcast["event"] = "draw";
            draw_broadcast["who"] = who;
            draw_broadcast["where"] = 0;
            env.send_multiply(env, draw_broadcast, who, who);

            json::Value left_update;
            left_update["event"] = "update";
            left_update["key"] = "left_num";
            left_update["value"] = env.game.left_num;
            env.send_multiply(env, left_update, -1, -1);

            if (env.game.left_num <= 0) { round_over = true; break; }

            // ---- Select tile ----
            std::vector<int> tiles_vec(agent.tiles.begin(), agent.tiles.end());
            std::vector<int> banned;
            bool can_riichi = agent.furo.empty() && !agent.riichi_status &&
                              env.game.left_num >= 4;

            json::Value sel_msg;
            sel_msg["event"] = "select_tile";
            sel_msg["tiles"] = json::Value("all");
            json::Value b_arr;
            for (int b : banned) b_arr.push_back(b);
            sel_msg["banned"] = b_arr;
            sel_msg["tsumo"] = tile_id;
            sel_msg["riichi"] = can_riichi;
            sel_msg["is_riichi_tile"] = false;
            env.send_personal(client, sel_msg);

            client.set_queue_allow_put();
            json::Value discard_decision;
            if (client.is_ai()) {
                int ai_tile = ai_discard_fallback(who, tiles_vec, banned);
                discard_decision["tile_id"] = ai_tile;
                discard_decision["riichi"] = false;
            } else {
                discard_decision = client.fetch_message();
            }
            int discard_tile = discard_decision["tile_id"].as_int();
            bool do_riichi = discard_decision.contains("riichi") &&
                             discard_decision["riichi"].as_bool();
            bool tsumo_cut = (discard_tile == tile_id);

            // ---- Discard ----
            env.game.discard_tile(who, discard_tile);
            if (do_riichi && can_riichi) {
                env.game.riichi(who);
                agent.riichi_tile = discard_tile;
            }

            json::Value disc_msg;
            disc_msg["event"] = "discard";
            disc_msg["who"] = who;
            disc_msg["tile_id"] = discard_tile;
            disc_msg["mode"] = tsumo_cut ? 1 : 0;
            disc_msg["after_tsumo"] = true;
            disc_msg["is_riichi"] = do_riichi;
            env.send_multiply(env, disc_msg, -1, -1);

            // Update furiten/machi for discarder
            json::Value furiten_update;
            furiten_update["event"] = "update";
            furiten_update["key"] = "furiten";
            furiten_update["value"] = agent.is_furiten();
            env.send_personal(client, furiten_update);

            json::Value machi_update;
            machi_update["event"] = "update";
            machi_update["key"] = "machi";
            json::Value machi_arr;
            for (int m : agent.machi) machi_arr.push_back(m);
            machi_update["value"] = machi_arr;
            env.send_personal(client, machi_update);

            // ---- Decision phase: let other players pon/chi/kan ----
            bool meld_taken = false;
            int meld_who = -1;
            std::string meld_type;

            for (int dist = 1; dist <= 3; dist++) {
                int other = (who + dist) % 4;
                if (other == who) continue;
                if (env.game.agents[other].riichi_status) continue; // in riichi, auto-pass

                auto actions = build_decision_actions(env.game, other, who, discard_tile);

                json::Value decision_msg;
                decision_msg["event"] = "decision";
                decision_msg["actions"] = actions;
                env.send_personal(*env.clients[other], decision_msg);

                json::Value chosen;
                auto& oc = *env.clients[other];
                if (oc.is_ai()) {
                    if (!env.fast)
                        std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    chosen = ai_decision_fallback(other, actions);
                } else {
                    oc.set_queue_allow_put();
                    json::Value raw = oc.fetch_message();
                    if (raw.contains("action")) chosen = raw["action"];
                    else chosen = actions[0];  // default pass
                }

                std::string act_type = chosen["type"].as_string();
                if (act_type != "pass") {
                    meld_taken = true;
                    meld_who = other;
                    meld_type = act_type;

                    json::Value meld_broadcast;
                    meld_broadcast["event"] = act_type;
                    json::Value meld_action;
                    meld_action["who"] = other;
                    meld_action["from_who"] = who;
                    meld_action["pattern"] = chosen["pattern"];
                    // Build kui_tile for the messages
                    meld_broadcast["action"] = meld_action;
                    env.send_multiply(env, meld_broadcast, -1, -1);

                    // Execute meld
                    if (act_type == "pon") {
                        auto pat = chosen["pattern"];
                        std::vector<int> tiles;
                        for (auto& v : pat.arr_val) tiles.push_back(v.as_int());
                        env.game.pon(other, tiles, discard_tile, who);
                    } else if (act_type == "chi") {
                        auto pat = chosen["pattern"];
                        std::vector<int> tiles;
                        for (auto& v : pat.arr_val) tiles.push_back(v.as_int());
                        env.game.chi(other, tiles, discard_tile, who);
                    } else if (act_type == "kan") {
                        auto pat = chosen["pattern"];
                        if (pat.is_array() && pat.size() >= 1) {
                            std::vector<int> tiles2;
                            int t_type = tile_type(discard_tile);
                            for (int tid : {t_type*4, t_type*4+1, t_type*4+2, t_type*4+3}) {
                                if (env.game.agents[other].tiles.count(tid))
                                    tiles2.push_back(tid);
                            }
                            tiles2.push_back(discard_tile);
                            env.game.kan(other, tiles2, -1, discard_tile, who, 1);
                            env.game.new_dora();
                            env.game.draw_tile(other, -1, -1); // rinchan draw
                        }
                    }

                    // Current player becomes the meld player
                    env.current_player = other;
                    break; // rest don't get to act
                }
            }

            if (!meld_taken) {
                // No meld — advance to next player
                env.current_player = (who + 1) % 4;
            }

            if (!env.fast)
                std::this_thread::sleep_for(std::chrono::milliseconds(300));

            first_turn = false;
        }

        // ---- End of round ----
        json::Value ryuukyoku;
        ryuukyoku["event"] = "ryuukyoku";
        ryuukyoku["why"] = "yama_end";
        ryuukyoku["nagashimangan"] = json::Array{};
        env.send_multiply(env, ryuukyoku, -1, -1);

        bool game_over = env.game_update(ryuukyoku, score_delta);

        if (!env.fast)
            std::this_thread::sleep_for(std::chrono::seconds(2));

        json::Value settlement;
        settlement["event"] = "settlement";
        settlement["res"] = ryuukyoku;
        json::Value sd_arr;
        for (int d : score_delta) sd_arr.push_back(d);
        settlement["score"] = sd_arr;
        json::Value ura_arr;
        for (int d : env.game.ura_dora_indicator) ura_arr.push_back(d);
        settlement["ura_dora"] = ura_arr;
        env.send_multiply(env, settlement, -1, -1);

        if (game_over) {
            LOG_INFO("游戏结束！");
            for (auto& [i, score] : env.game.get_rank()) {
                LOG_INFO("「%s」积分「%d」", env.clients[i]->username().c_str(), score * 100);
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
            env.send_multiply(env, score_msg, -1, -1);

            json::Value end_msg;
            end_msg["event"] = "end";
            end_msg["message"] = "游戏结束！";
            env.send_multiply(env, end_msg, -1, -1);
            break;
        }
    }
}
