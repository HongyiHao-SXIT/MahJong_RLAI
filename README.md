# Mahjong AI based on Suphx

A Riichi Mahjong AI reproducing Microsoft Research Asia's [Suphx](https://arxiv.org/abs/2003.13590) model, supporting a complete training pipeline: **Supervised Learning → Reinforcement Learning Fine-tuning → Online Play**.

---

## Environment Setup

```powershell
conda create -n mahjong python=3.9 -y
conda activate mahjong
pip install -r requirements.txt
```

Primary dependencies: `PyTorch 2.0`, `numpy`, `websockify`.

Training and gameplay require a GPU. Run `python check.py` to verify CUDA availability.

---

## Quick Start: Play Against AI

### 1. Prepare Models

Place trained weights under `model/saved/`:

```
model/saved/
├── discard-model/best.pt   # Discard model (required)
├── riichi-model/best.pt    # Riichi model
├── furo-model/best.pt      # Furo (meld) model
└── reward-model/best.pt    # Reward predictor
```

### 2. Start Server (Terminal 1)

```powershell
python online_game/server.py -A 3 -H 0.0.0.0
```
## Noted by YiQing
前端决定推翻重写，利用现有素材即可
服务端待分析代码结构，并进行重构

| Flag | Description |
|------|-------------|
| `-A N` | Number of AI players (remaining seats for humans) |
| `-f` | Fast mode, skip thinking/waiting animations |
| `-d` | Debug mode, output AI decision confidence |
| `-ob` | Spectator mode |

### 3. Start WebSocket Bridge (Terminal 2)

```powershell
websockify 8888 127.0.0.1:9999
```

### 4. Start Web Client (Terminal 3)

```powershell
cd online_game/web_client
python -m http.server 8080
```

Open `http://localhost:8080` in a browser, enter a username, and click "Connect" to start playing.

> A command-line client is also available: `python online_game/client.py -U User1 -H localhost`

---

## Game Rules

- Four-player South (hanchan), with red fives (akadora), open tanyao (kuitan), and ippatsu
- Prohibited: kuikae by genbutsu or suji
- Kan flips the dora indicator immediately; kokushi musou cannot chankan a closed kan
- Ryuukyoku types: exhaustive draw, kyuushuu kyuuhai, suufon renda, suukan sanran, suucha riichi, sanchahou
- Ryuukyoku mangan does not count as a win; no pao rule for daisangen / dai/shou suushii

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

Enable fast mode during self-play to reduce waiting time:

```powershell
python online_game/server.py -A 4 -f -ob
```

---

## Project Structure

```
MahJong_RLAI/
├── mahjong/              # Game engine
│   ├── game.py           #   Main game loop, wall, rounds, dora management
│   ├── agent.py          #   Player / AI agent state and decision-making
│   ├── yaku.py           #   Yaku (hand pattern) evaluation
│   ├── check_agari.py    #   Winning hand detection
│   ├── utils.py          #   Utility functions (encoding/decoding/feature extraction)
│   ├── display.py        #   Game display
│   └── *.pkl             #   Precomputed tables (winning / waiting hands)
├── dataset/              # Data collection and processing
│   ├── download_logs.py  #   Download Tenhou game logs
│   ├── download_data.py  #   Download game records
│   ├── tenhou.py         #   Tenhou game record parser
│   └── data.py           #   PyTorch Dataset wrapper
├── model/                # Neural network models
│   ├── models.py         #   DiscardModel / RiichiModel / FuroModel / RewardPredictor
│   └── saved/            #   Trained model weights
├── sl_train/             # Supervised learning training scripts
│   ├── train_discard_model.py
│   ├── train_riichi_model.py
│   ├── train_furo_model.py
│   ├── train_reward.py
│   └── training_utils.py
├── rl_train/             # Reinforcement learning training scripts
│   ├── train_discard_rl.py    #  REINFORCE
│   └── train_discard_ppo.py   #  PPO + Oracle Guiding
├── online_game/          # Online play
│   ├── server.py         #   Game server
│   ├── client.py         #   CLI client
│   └── web_client/       #   Web client (Phaser 3 + WebSocket)
│       ├── index.html
│       ├── js/src/       #   Rendering logic / networking
│       ├── img/          #   Tile textures
│       └── audio/        #   Sound effects
├── check.py              # CUDA availability check
└── requirements.txt
```

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

## Contributers
Lanyi_adict: Model & RL algorithm develop.
YiQing: Frontend developemnt.