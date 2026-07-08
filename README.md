# Mahjong AI based on Suphx

[中文版](./README_CN.md)

A Riichi Mahjong AI reproducing Microsoft Research Asia's [Suphx](https://arxiv.org/abs/2003.13590) model, supporting a complete training pipeline: **Supervised Learning → Reinforcement Learning Fine-tuning → Online Play**.

---
```

┌───────────────┐   WebSocket    ┌─────────────────────┐   TCP   ┌──────────────┐
│  Godot Client │◄──────────────►│  C++ Backend Server │◄───────►│  Python AI   │
│  (Godot 4.6)  │    NDJSON      │  (C++20, WebSocket) │   IPC   │  Engine      │
└───────────────┘                └─────────────────────┘         └──────────────┘
```

---

## Environment Setup

### Python AI Engine

```powershell
conda create -n mahjong python=3.9 -y
conda activate mahjong
pip install -r requirements.txt
```

Primary dependencies: `PyTorch 2.0`, `numpy`.

Training and gameplay require a GPU. Run `python check.py` to verify CUDA availability.

### C++ Backend

**Requirements:** GCC 12+ / MinGW-w64 (C++20 support).

```powershell
cd cpp_backend
g++ -std=c++20 -O2 -Isrc src/main.cpp -o build/server.exe -lws2_32
# or simply: build.bat
```

No external libraries — the backend includes its own WebSocket (RFC 6455), JSON, and SHA-1 implementations.

### Godot Client

Open `godot_client/project.godot` with **Godot 4.6+**. All scripts are in GDScript — no C# setup needed.

---

## Quick Start: Play Against AI

### Option A: C++ Backend + Godot Client (WebSocket, no bridge needed)

**1. Prepare Models**

Place trained weights under `model/saved/`:

```
model/saved/
├── discard-model/best.pt   # Discard model (required)
├── riichi-model/best.pt    # Riichi model
├── furo-model/best.pt      # Furo (meld) model
└── reward-model/best.pt    # Reward predictor
```

**2. Start C++ Server**

```powershell
cd cpp_backend
build/server.exe -P 9999 -A 3 -ob
```

| Flag | Description |
|------|-------------|
| `-A N` | Number of AI players (remaining seats for humans) |
| `-P N` | Port (default: 9999) |
| `-f` | Fast mode, skip thinking/waiting delays |
| `-d` | Debug logging |
| `-ob` | Allow observers |

**3. Open Godot Client**

Open `godot_client/` in Godot 4.6+, run the project, enter server address (`127.0.0.1:9999`) and username, then click **Connect**.

**4. (Optional) Connect Python AI Engine**

The C++ server has built-in simple AI. To use the full PyTorch model, start the Python AI engine separately:

```powershell
python online_game/server.py -A 0 -H 0.0.0.0  # AI-only mode, acts as AI backend
```

Then configure `ai_proxy.hpp` to connect to it.

### Option B: Python Server + Web Client (legacy)

**1. Start Server (Terminal 1)**

```powershell
python online_game/server.py -A 3 -H 0.0.0.0
```

**2. Start WebSocket Bridge (Terminal 2)**

```powershell
websockify 8888 127.0.0.1:9999
```

**3. Start Web Client (Terminal 3)**

```powershell
cd online_game/web_client
python -m http.server 8080
```

Open `http://localhost:8080` in a browser.

> A command-line client is also available: `python online_game/client.py -U User1 -H localhost`

---

## Game Rules

- Four-player South (hanchan), with red fives (akadora), open tanyao (kuitan), and ippatsu
- Prohibited: kuikae by genbutsu or suji
- Kan flips the dora indicator immediately; kokushi musou cannot chankan a closed kan
- Ryuukyoku types: exhaustive draw, kyuushuu kyuuhai, suufon renda, suukan sanran, suucha riichi, sanchahou
- Ryuukyoku mangan does not count as a win; no pao rule for daisangen / dai/shou suushii

---

## C++ Backend Architecture

The new C++ backend (`cpp_backend/`) is a **self-contained WebSocket game server** with zero external dependencies.

### Files

| File | Description |
|------|-------------|
| `src/main.cpp` | Entry point, CLI argument parsing |
| `src/server.hpp` | `MahJongServer` — WebSocket accept loop, connection management, auto-start games |
| `src/websocket.hpp` | RFC 6455 WebSocket (handshake, framing, SHA-1, Base64) — self-implemented |
| `src/json.hpp` | Minimal JSON parser + serializer |
| `src/game_engine.hpp` | `MahjongGame` (yama, deal, draw, discard, dora) + `GameEnvironment` (scoring, client mgmt) + `game_main_loop` |
| `src/agent.hpp` | Player state: hand tiles, furo, riichi, furiten, pon/chi/kan logic |
| `src/ai_proxy.hpp` | IPC bridge to Python AI engine; falls back to random AI |

### Protocol

All messages are **NDJSON** (newline-delimited JSON) over WebSocket — identical to the existing protocol:

```
Client → Server          Server → Client
──────────────────       ──────────────────
{"username":"...",       join, start, draw,
 "observe":false}        select_tile, discard,
{"event":"discard",      decision, chi, pon,
 "tile_id":N}            kan, riichi, agari,
{"event":"decision",     ryuukyoku, settlement,
 "action":{...}}         score, end, update
```

---

## Godot Client Structure

The Godot client (`godot_client/`) uses **GDScript + programmatic UI** for fast iteration.

| File | Description |
|------|-------------|
| `Scripts/Main.gd` | Entry point — builds menu & game UI, wires signals |
| `Scripts/network/WebSocketClient.gd` | WebSocket connection, auto-reconnect, frame parsing |
| `Scripts/network/Protocol.gd` | NDJSON event → Godot signal dispatcher |
| `Scripts/game/GameState.gd` | Client-side game state: hand, river, furo, scores, tile constants |
| `Scenes/Main.tscn` | Minimal scene bootstrap |

### Web Export

To export for the web:

1. In Godot, **Project → Export → Add → Web**
2. Enable **Threads** and **WebSocket** support
3. Export as HTML5/WASM
4. The C++ backend already speaks WebSocket — no `websockify` bridge needed

---

## Supervised Learning

### Download Tenhou Game Records

Collect high-level play data from the [Tenhou Houou table](https://tenhou.net/):

```powershell
# Download recent 7-day game logs → logs/
python dataset/download_logs.py

# Download game records from logs → data/
python dataset/download_data.py
```

For historical data, download `scraw` archives from the [Tenhou raw log archive](https://tenhou.net/sc/raw/), then:

```powershell
./ungz.sh 2022/           # Extract .scc game records
mv 2022/*.txt logs/       # Move to logs/
python dataset/download_data.py
```

### Train Models

```powershell
# Discard model (core)
python sl_train/train_discard_model.py --num_layers 50 --epochs 10

# Riichi model
python sl_train/train_riichi_model.py --num_layers 20 --epochs 10

# Furo (meld) model
python sl_train/train_furo_model.py --mode chi --num_layers 50 --epochs 10 --pos_weight 10

# Reward predictor
python sl_train/train_reward.py
```

---

## Deep Reinforcement Learning (Self-Play Fine-tuning)

Fine-tune the discard policy via self-play and the Reward Predictor, following the Suphx approach:

```powershell
# REINFORCE (basic policy gradient)
python rl_train/train_discard_rl.py --episodes 200 --gamma 0.99 --lr 1e-5

# PPO (Actor-Critic + Clipping + Entropy regularization)
python rl_train/train_discard_ppo.py --episodes 200 --gamma 0.99 --actor_lr 1e-5 --critic_lr 2e-4

# PPO + Oracle Guiding (teacher policy uses hidden features)
python rl_train/train_discard_ppo.py --oracle_guiding --oracle_guiding_coef 0.05 --episodes 200
```

Checkpoints default to `output/discard-rl-model/checkpoints/` and `output/discard-ppo-model/checkpoints/`.

Enable fast mode during self-play:

```powershell
python online_game/server.py -A 4 -f -ob
```

---

## Project Structure

```
MahJong_RLAI/
├── mahjong/              #   Python game engine
│   ├── game.py           #     Main game loop, wall, rounds, dora management
│   ├── agent.py          #     Player / AI agent state and decision-making
│   ├── yaku.py           #     Yaku (hand pattern) evaluation
│   ├── check_agari.py    #     Winning hand detection
│   ├── utils.py          #     Utility functions (encoding/decoding/feature extraction)
│   ├── display.py        #     Game display
│   └── *.pkl             #     Precomputed tables (winning / waiting hands)
├── dataset/              #   Data collection and processing
│   ├── download_logs.py  #     Download Tenhou game logs
│   ├── download_data.py  #     Download game records
│   ├── tenhou.py         #     Tenhou game record parser
│   └── data.py           #     PyTorch Dataset wrapper
├── model/                #   Neural network models
│   ├── models.py         #     DiscardModel / RiichiModel / FuroModel / RewardPredictor
│   └── saved/            #     Trained model weights
├── sl_train/             #   Supervised learning training scripts
├── rl_train/             #   Reinforcement learning training scripts
├── online_game/          #   Legacy online play (Python server + web client)
│   ├── server.py         #     Python server (with AI)
│   ├── Server/           #     Legacy C++ server (TCP, WinSock)
│   ├── client.py         #     CLI client
│   ├── web_client/       #     Phaser.js web client
│   └── mah-jong/         #     Legacy Godot 4.6 client (early prototype)
├── cpp_backend/          # ★ New C++ WebSocket backend
│   ├── src/
│   │   ├── main.cpp      #     Entry point
│   │   ├── server.hpp    #     WebSocket server + connection manager
│   │   ├── websocket.hpp #     RFC 6455 WebSocket implementation
│   │   ├── json.hpp      #     JSON parser/serializer
│   │   ├── game_engine.hpp#    Game engine (MahjongGame + GameEnvironment + game loop)
│   │   ├── agent.hpp     #     Player state + furo logic
│   │   └── ai_proxy.hpp  #     Python AI IPC bridge
│   ├── build.bat         #     Build script
│   └── Makefile          #     Make build
├── godot_client/         # ★ New Godot 4.6 client
│   ├── Scenes/           #     Scene files
│   ├── Scripts/
│   │   ├── Main.gd       #     Entry point (programmatic UI)
│   │   ├── network/      #     WebSocketClient + Protocol handler
│   │   ├── game/         #     GameState (tile model, scores)
│   │   └── ui/           #     (planned: UI components)
│   └── project.godot     #     Godot project config
├── check.py              #   CUDA availability check
└── requirements.txt
```

---

## Protocol Quick Reference

All messages are **NDJSON over WebSocket**. The protocol is identical across all clients — C++ server, Python server, Godot, and web client speak the same language.

| Server → Client | Key Fields |
|-----------------|-----------|
| `join` | `status` (1=ok), `message` |
| `start` | `game` (round, honba, dora, agents...), `self` (tiles, seat...) |
| `draw` | `who`, `tile_id`, `where` |
| `select_tile` | `tiles`, `banned`, `tsumo`, `riichi` |
| `discard` | `who`, `tile_id`, `mode` (0=hand-cut, 1=tsumo-cut) |
| `decision` | `actions[]` — each: `{type, who, from_who, pattern, machi}` |
| `chi` / `pon` / `kan` | `action` — `{who, from_who, pattern}` |
| `riichi` | `action` — `{who, status}` |
| `agari` | `action[]` — `[{who, from_who, score, ...}]`, `ura_dora_indicator` |
| `ryuukyoku` | `why`, `nagashimangan` |
| `settlement` | `res`, `score` (deltas), `ura_dora` |
| `end` | `message` |

| Client → Server | Key Fields |
|----------------|-----------|
| Join | `{"username":"...", "observe":false}` |
| Discard | `{"event":"discard", "tile_id":N}` |
| Decision | `{"event":"decision", "action":{...}}` |
| Ready | `{"event":"ready"}` |

---

## Model Architecture

All policy networks are based on a **1D convolutional residual network**:

- **Input**: 34-channel × tile-type features (hand tiles, melds, discards, dora, round context, etc.)
- **Body**: N layers of ResBlock (3×1 Conv → BN → LeakyReLU → 3×1 Conv → BN → LeakyReLU + residual connection)
- **Output**:
  - `DiscardModel`: 34-dimensional logits (discard probabilities for 34 tile types)
  - `RiichiModel`: scalar (riichi probability)
  - `FuroModel`: scalar (meld probability)
  - `RewardPredictor`: scalar (predicted final rank / score)

---

## References

- [Suphx: Mastering Mahjong with Deep Reinforcement Learning](https://arxiv.org/abs/2003.13590)
- [Tenhou](https://tenhou.net/)
- [Tenhou Raw Log Archive](https://tenhou.net/sc/raw/)
- [Tianshou](https://tianshou.org/en/stable/)
- [RFC 6455 — The WebSocket Protocol](https://datatracker.ietf.org/doc/html/rfc6455)

## Contributers
Lanyi_adict: Model & RL algorithm develop.
YiQing: Frontend development.
