# MahJong RLAI - Backend Development Guide

## 1. Project Overview

This project is an online Riichi Mahjong battle server system that supports both human-vs-AI and online multiplayer games. The backend consists of two parts: a C++ game server and a Python AI engine. The client side supports a Python terminal client and a web client.

### Tech Stack

| Layer | Language | Core Dependencies | Purpose |
|------|------|----------|------|
| Game Server | C++17 | WinSock2 / POSIX socket | TCP communication, player management, game logic |
| AI Engine | Python | PyTorch, NumPy | Neural-network decisions (discard/riichi/furo) |
| Game Core | Python | pickle lookup tables | Agari checks, yaku scoring, tenpai detection |
| Web Frontend | HTML/JS | Phaser.js | Graphical Mahjong client |

### Core Directory Structure

```
MahJong_RLAI/
|- online_game/
|  |- Server/
|  |  |- server.cpp          # C++ game server (main file)
|  |  |- server.exe          # build artifact
|  |  |- README              # this document
|  |- server.py              # Python server (includes AI decision logic)
|  |- client.py              # Python terminal client
|  |- web_client/            # web client
|     |- index.html
|     |- js/src/
|        |- render.js        # Phaser rendering
|        |- communication.js # WebSocket communication
|- mahjong/                  # Mahjong core library
|  |- game.py                # game state machine (MahjongGame)
|  |- agent.py               # player state (Agent)
|  |- yaku.py                # yaku evaluation (Yaku, YakuList)
|  |- check_agari.py         # agari check (precomputed table lookup)
|  |- utils.py               # utility functions
|  |- display.py             # ASCII rendering
|  |- AGARI_TABLE_2.pkl      # agari lookup table (~300MB)
|  |- MACHI_TABLE.pkl        # machi lookup table (~200MB)
|- model/
|  |- models.py              # PyTorch model definitions
|     |- DiscardModel
|     |- RiichiModel
|     |- FuroModel
|- sl_train/                 # supervised-learning scripts
|- rl_train/                 # reinforcement-learning scripts
|- dataset/                  # data loading and preprocessing
```

---

## 2. C++ Server Architecture (server.cpp)

### 2.1 Build and Run

```bash
# Build (MinGW-w64 / GCC)
g++ -std=c++17 -O2 server.cpp -o server.exe -lws2_32

# Run
server.exe -H 0.0.0.0 -P 9999 -A 4 -f -d -ob -m 0
```

### 2.2 Command-Line Arguments

| Option | Long Option | Default | Description |
|------|--------|--------|------|
| -H | --host | 0.0.0.0 | Listen address |
| -P | --port | 9999 | Listen port |
| -A | --AI | 0 | Number of AI players (0-4) |
| -m | --min-score | 0 | Minimum score threshold (x100) |
| -ob | --allow-observe | false | Allow observers |
| -f | --fast | false | Skip AI thinking delay |
| -d | --debug | false | Enable debug logging |

### 2.3 Component Architecture

```
+------------------------------------------------------+
|                      main()                          |
|  WSAStartup -> create_server() -> bind/listen -> run |
+------------------+-----------------------------------+
                   |
     +-------------+------------------------+
     |            Server.run()              |
     |  +------------------------------+    |
     |  | handle_connections() thread  |    |
     |  | accept() -> handshake -> join|    |
     |  +------------------------------+    |
     |  +------------------------------+    |
     |  | game_main_loop() thread      |    |
     |  | start -> game_loop -> update |    |
     |  +------------------------------+    |
     +--------------------------------------+
                   |
     +-------------+------------------------+
     |         GameEnvironment              |
     |  +----------+  +------------------+  |
     |  | Mahjong  |  |  Client[4]       |  |
     |  | Game     |  |  observe_info    |  |
     |  |  +-----+ |  |  observers       |  |
     |  |  |Agent| |  +------------------+  |
     |  |  |[0-3]| |                        |
     |  |  +-----+ |                        |
     |  +----------+                        |
     +--------------------------------------+
```

### 2.4 Core Data Structures

#### 2.4.1 JSON Library (json::Value)

Embedded lightweight JSON parser/serializer with support for all JSON data types:

```cpp
// Parse
json::Value msg = json::parse("{\"event\":\"join\",\"status\":1}");

// Access
std::string event = msg["event"].as_string();
int status = msg["status"].as_int();

// Build
json::Value resp;
resp["event"] = "draw";
resp["who"] = 0;
resp["tile_id"] = 52;

// Serialize
std::string wire = json::serialize(resp);
```

#### 2.4.2 ControlledQueue<T>

Thread-safe queue with single-producer semantics controlled by an atomic allow_put flag:

```cpp
template<typename T>
class ControlledQueue {
    std::queue<T> q;
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<bool> allow_put{false};
};
```

Python semantic mapping: in the Python server, ControlledQueue allows only one put each time, then auto-locks until allow_put() is called again. This guarantees that the game loop consumes exactly one client message per decision step.

#### 2.4.3 Agent (Player State)

| Field | Type | Description |
|------|------|------|
| score | int | score / 100 |
| tiles | set<int> | hand tile IDs (Tenhou 0-135) |
| hand_tile_counter | vector<int> | count of 34 tile types |
| discard_tiles | vector<int> | discard history (including called tiles) |
| river | vector<int> | visible river (excluding called tiles) |
| furo | map<FuroKey, vector<int>> | chi/pon/kan |
| riichi_status | int | in riichi or not |
| ippatsu_status | int | ippatsu flag |
| machi | set<int> | waiting tiles |
| *_furiten | bool | furiten states |

FuroKey encoding: (furo_type, pattern, nth)

| furo_type | Meaning | pattern |
|-----------|------|---------|
| 0 | chi | min_tile_type |
| 1 | pon | tile_type |
| 2 | ankan | tile_type |
| 3 | minkan/kakan | tile_type |

#### 2.4.4 MahjongGame (Game State Machine)

| Field | Type | Description |
|------|------|------|
| yama | vector<int> | wall (136 tiles) |
| left_num | int | remaining draws before exhaustive draw |
| round | int | hand index (0 = East 1) |
| round_wind | int | prevailing wind (27=E,28=S,29=W,30=N) |
| honba | int | honba count |
| riichi_ba | int | riichi stick count |
| dora_indicator | vector<int> | dora indicators |
| dora | vector<int> | dora tiles |
| ura_dora_indicator | vector<int> | ura indicators |
| oya | int | dealer index (0-3) |
| kang_num | vector<int>[4] | kan counts per player |
| agents | vector<Agent>[4] | four players |
| first_round | bool | first go-around flag |

Key methods:

| Method | Description |
|------|------|
| new_game(round, honba, riichi_ba) | initialize wall, hands, dora |
| draw_tile(who) | draw tile and update left_num |
| discard_tile(who, tile_id) | discard tile |
| pon(who, tiles, kui_tile, from_who) | pon |
| chi(who, tiles, kui_tile, from_who) | chi |
| kan(who, tiles, mode) | kan (0=ankan, 1=minkan, 2=kakan) |
| riichi(who) | riichi declaration |
| new_dora() | reveal new dora |
| check_pon/chi/kan(who, tile, mode) | action availability checks |

### 2.5 Network Protocol

#### 2.5.1 Transport Format

All messages use newline-delimited JSON (NDJSON):

```
JSON_STRING\n
```

#### 2.5.2 Client -> Server

| Event | Payload | Description |
|-------|------|------|
| handshake | {"username":"...", "observe":bool} | join room / observe |
| discard | {"event":"discard", "tile_id":int} | choose discard |
| decision | {"event":"decision", "action":{...}} | choose action (agari/chi/pon/kan/riichi/pass) |
| ready | {"event":"ready"} | continue between rounds |
| quit | {"event":"quit"} | leave room |
| change_ob | {"event":"change_ob", "username":"..."} | switch observe target |

#### 2.5.3 Server -> Client

| Event | Payload | Description |
|-------|------|------|
| join | {"event":"join", "status":int, "message":"..."} | join result (1=ok, 0=fail, -1=observer) |
| start | {"event":"start", "game":{...}, "self":{...}} | game start info |
| draw | {"event":"draw", "who":int, "tile_id":int, "where":int} | draw notification |
| select_tile | {"event":"select_tile", "tiles":"all"/[...]} | request discard choice |
| discard | {"event":"discard", "who":int, "tile_id":int, "mode":int, "after_tsumo":bool} | discard broadcast |
| decision | {"event":"decision", "actions":[{...}]} | request decision |
| chi/pon/kan | {"event":"chi", "action":{...}} | meld broadcast |
| riichi | {"event":"riichi", "action":{...}} | riichi broadcast |
| agari | {"event":"agari", "action":[{...}], "ura_dora_indicator":[...]} | win broadcast |
| ryuukyoku | {"event":"ryuukyoku", "why":"..."} | exhaustive draw broadcast |
| settlement | {"event":"settlement", "res":{...}, "score":[...], "ura_dora":[...]} | hand settlement |
| score | {"event":"score", "score":[[who,score],...]} | ranking broadcast |
| end | {"event":"end", "message":"..."} | game end |
| update | {"event":"update", "key":str, "value":any} | state update (furiten/machi/left_num) |

### 2.6 Game Loop

```
game_main_loop()
|
|- shuffle player order
|
|- while game_start:
|  |- env.start()             # start hand
|  |- send_all_game_info()
|  |
|  |- for each turn:
|  |  |- draw_tile(who)
|  |  |  |- send draw to current player
|  |  |  |- broadcast draw to others
|  |  |
|  |  |- select_tile(client)
|  |  |  |- human: fetch_message() (blocking)
|  |  |  |- AI: ai_discard()
|  |  |
|  |  |- discard_tile(who, tile)
|  |  |- broadcast discard
|  |  |- broadcast wait
|  |  |- current_player++
|  |
|  |- ryuukyoku/agari judgment
|  |- game_update(res)        # score settlement
|  |- send settlement
|  |
|  |- check game over:
|     |- score threshold
|     |- round >= 12
|     |- east/south end conditions
|
|- reset() and wait for next game
```

### 2.7 AI Decision (Current C++ Placeholder)

The current C++ version uses a placeholder AI. Full intelligent decision logic exists in the Python server.

```cpp
// Discard: random among legal candidates
int ai_discard(int who, const vector<int>& tiles, const vector<int>& banned) {
    vector<int> candidates;
    for (int t : tiles)
        if (find(banned.begin(), banned.end(), t / 4) == banned.end())
            candidates.push_back(t);
    return candidates[rand() % candidates.size()];
}

// Action choice: agari if possible, otherwise pass
json::Value ai_decision(int who, const vector<json::Value>& actions) {
    for (auto& act : actions)
        if (act["type"].as_string() == "agari") return act;
    return actions[0];
}
```

---

## 3. Python Server (server.py)

The Python version contains full AI decision logic using PyTorch models.

### 3.1 Run

```bash
# basic mode (human players)
python server.py -H 0.0.0.0 -P 9999

# training mode with 4 AIs
python server.py -A 4 -f -t

# observer support
python server.py -A 2 -ob
```

### 3.2 AI Decision Flow

```
decide_by_ai(who, actions)
|
|- score each action:
|  |- agari  -> AiAgent.agari_decision()
|  |- riichi -> AiAgent.riichi_decision()
|  |- pon    -> AiAgent.pon_decision()
|  |- chi    -> AiAgent.chi_decision()
|  |- kan    -> AiAgent.kan_decision()
|  |- ryuukyoku check
|
|- pick highest score action
|- if score < 0.5 -> pass
|- return selected action

discard_by_ai(who, tiles, banned)
|
|- build state features
|- training mode: sample from DiscardModel distribution
|- inference mode: take max-confidence discard
```

### 3.3 Feature Encoding

State is encoded as a multi-channel feature tensor for CNN input:

| Channel Group | Count | Description |
|--------|--------|------|
| Hand | 4 | per-tile ownership 0-4 |
| Melds | 16 | 4 meld slots x 4 channels |
| Rivers | ~20+ | discard history of all players |
| Dora/winds/meta | ~10 | dora and context features |

---

## 4. Neural Network Models (model/models.py)

### 4.1 Model Architecture

```
INPUT (in_channels x 34)
    |
    v
Conv1d(in->256, k=3) + BN + LeakyReLU
    |
    v
ResBlock x N
|   Conv1d(256->256, k=3) + BN + LeakyReLU
|   Conv1d(256->256, k=3) + BN + LeakyReLU
|   skip connection
    |
    v
Conv1d(256->1, k=1) + BN
    |
    v
OUTPUT (34)
    Softmax -> probability distribution
```

### 4.2 Model Variants

| Model | Input Channels | Output | Purpose |
|------|----------|------|------|
| DiscardModel | state channels | 34 logits | discard selection |
| RiichiModel | state channels | 1 scalar | riichi willingness |
| FuroModel | state + furo channels | 1 scalar | furo willingness |
| RewardPredictor | 74 channels | 1 scalar | reward prediction (for PPO) |

### 4.3 Training Stages

| Stage | Method | Script |
|------|------|------|
| Discard pretrain | supervised learning | sl_train/train_discard_model.py |
| Furo pretrain | supervised learning | sl_train/train_furo_model.py |
| Riichi pretrain | supervised learning | sl_train/train_riichi_model.py |
| RL fine-tune | PPO | rl_train/train_discard_ppo.py |

---

## 5. Rules Engine (mahjong/)

### 5.1 Agari Check (check_agari.py)

Uses precomputed lookup tables:

- AGARI_TABLE_2.pkl: all valid agari patterns -> encoded result
- MACHI_TABLE.pkl: all tenpai patterns -> waiting tiles

```python
def is_agari(counter):
    """O(1) table-based agari check"""
    pattern = counter_to_pattern(counter)
    key = calculate_pattern_key(pattern)
    return agari_table[0].get(key)
```

### 5.2 Yaku Evaluation (yaku.py)

Yaku handles full scoring logic:

- Yakuman checks: Kokushi Musou, Suuankou, Daisangen, Chuuren Poutou, Ryuuiisou, etc.
- Standard yaku: Riichi, Pinfu, Tanyao, Ittsuu, Sanshoku Doujun, etc.
- Fu calculation
- Dora/aka/ura handling

### 5.3 Tenhou Tile ID System

Tiles use Tenhou IDs 0-135:

```
manzu: 0-35     (types 0-8, aka 5m = 16)
pinzu: 36-71    (types 9-17, aka 5p = 52)
souzu: 72-107   (types 18-26, aka 5s = 88)
winds: 108-123  (types 27-30)
dragons: 124-135 (types 31-33)
```

Formulas:

- type = tile_id / 4
- copy = tile_id % 4

---

## 6. Client Integration

### 6.1 Python Terminal Client

```bash
python client.py -H localhost -P 9999 -U "player_name"
python client.py -ob
```

### 6.2 Web Client

Open online_game/web_client/index.html in a browser and fill in server address/port.

### 6.3 Custom Client Workflow

```
1. Connect TCP to server:port
2. Send handshake: {"username":"name", "observe":false}\n
3. Receive join response (status=1)
4. Wait for start event
5. Game loop:
   - draw event -> render draw
   - select_tile/decision -> send choice
   - discard -> update river
   - chi/pon/kan/riichi -> update board
   - settlement/end -> round/game finalization
```

---

## 7. Deployment Notes

### Development Environment

```bash
# Python dependencies
pip install torch numpy

# C++ build (Windows MinGW)
g++ -std=c++17 -O2 server.cpp -o server.exe -lws2_32

# C++ build (Linux)
g++ -std=c++17 -O2 server.cpp -o server -lpthread
```

### Production Considerations

1. Lookup tables AGARI_TABLE_2.pkl and MACHI_TABLE.pkl consume about 500MB in memory.
2. Trained .pt models should be placed under model/saved/.
3. Default port is 9999; ensure firewall allows inbound traffic.
4. Each client uses an independent thread; observer mode reuses state references.
5. During game, disconnected players can reconnect with the same username.

### Game-End Conditions

- Any player drops below minimum threshold (-m, x100)
- East/South match reaches end and top score is still below 30000
- Top-ranked player is dealer and round-end condition is satisfied
