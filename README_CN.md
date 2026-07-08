# 基于 Suphx 的立直麻将 AI

[English](./README.md)

复现微软亚洲研究院 [Suphx](https://arxiv.org/abs/2003.13590) 模型的立直麻将 AI，支持**有监督学习 → 强化学习微调 → 在线对战**的完整训练管线。

---
```

┌───────────────┐   WebSocket    ┌─────────────────────┐   TCP    ┌──────────────┐
│  Godot 客户端  │◄─────────────►│  C++ 后端服务器       │◄───────►│  Python AI   │
│  (Godot 4.6)  │    NDJSON     │  (C++20, WebSocket)  │   IPC    │  引擎        │
└───────────────┘              └─────────────────────┘         └──────────────┘
```

---

## 环境配置

### Python AI 引擎

```powershell
conda create -n mahjong python=3.9 -y
conda activate mahjong
pip install -r requirements.txt
```

主要依赖：`PyTorch 2.0`、`numpy`。

训练/对战需要 GPU，可通过 `python check.py` 验证 CUDA 是否可用。

### C++ 后端

**要求：** GCC 12+ / MinGW-w64（支持 C++20）。

```powershell
cd cpp_backend
g++ -std=c++20 -O2 -Isrc src/main.cpp -o build/server.exe -lws2_32
# 或直接运行: build.bat
```

**零外部依赖** — 后端自带了 WebSocket（RFC 6455）、JSON 和 SHA-1 的实现。

### Godot 客户端

用 **Godot 4.6+** 打开 `godot_client/project.godot`。所有脚本为 GDScript，无需 C# 配置。

---

## 快速开始：与 AI 对战

### 方案 A：C++ 后端 + Godot 客户端（WebSocket 直连，无需桥接）

**1. 准备模型**

将训练好的权重放在 `model/saved/` 下：

```
model/saved/
├── discard-model/best.pt   # 弃牌模型（必须）
├── riichi-model/best.pt    # 立直模型
├── furo-model/best.pt      # 副露模型
└── reward-model/best.pt    # 奖励预测器
```

**2. 启动 C++ 服务端**

```powershell
cd cpp_backend
build/server.exe -P 9999 -A 3 -ob
```

| 参数 | 说明 |
|------|------|
| `-A N` | AI 玩家数量（剩余席位留给人类） |
| `-P N` | 端口（默认: 9999） |
| `-f` | 快速模式，跳过等待延迟 |
| `-d` | Debug 日志 |
| `-ob` | 允许观战 |

**3. 打开 Godot 客户端**

在 Godot 4.6+ 中打开 `godot_client/`，运行项目，输入服务器地址（`127.0.0.1:9999`）和用户名，点击 **Connect**。

**4. (可选) 接入 Python AI 引擎**

C++ 后端内置了简易 AI。要使用完整的 PyTorch 模型，单独启动 Python AI 引擎：

```powershell
python online_game/server.py -A 0 -H 0.0.0.0  # 纯 AI 模式，作为 AI 后端
```

然后配置 `ai_proxy.hpp` 接入。

### 方案 B：Python 服务端 + 网页客户端（传统方式）

**1. 启动服务端（终端 1）**

```powershell
python online_game/server.py -A 3 -H 0.0.0.0
```

**2. 启动 WebSocket 桥接（终端 2）**

```powershell
websockify 8888 127.0.0.1:9999
```

**3. 启动网页客户端（终端 3）**

```powershell
cd online_game/web_client
python -m http.server 8080
```

然后用浏览器打开 `http://localhost:8080`，输入用户名后点击"连接"即可开始对局。

> 也可使用命令行客户端：`python online_game/client.py -U User1 -H localhost`

---

## 游戏规则

- 四人南风场、有赤牌、有食断、有一发役
- 禁止现物食替、筋食替
- 开杠即翻宝牌，国士无双不可抢暗杠
- 流局包含：荒牌流局、九种九牌、四风连打、四杠散了、四家立直、三家和了
- 流局满贯不计和牌，大三元/大小四喜不设包牌

---

## C++ 后端架构

新的 C++ 后端（`cpp_backend/`）是一个 **零依赖的 WebSocket 游戏服务器**。

### 文件

| 文件 | 说明 |
|------|------|
| `src/main.cpp` | 入口，命令行参数解析 |
| `src/server.hpp` | `MahJongServer` — WebSocket accept 循环、连接管理、自动开局 |
| `src/websocket.hpp` | RFC 6455 WebSocket（握手、分帧、SHA-1、Base64）— 自实现 |
| `src/json.hpp` | 轻量 JSON 解析 + 序列化 |
| `src/game_engine.hpp` | `MahjongGame`（牌山、配牌、摸牌、舍牌、宝牌） + `GameEnvironment`（计分、客户端管理） + `game_main_loop` |
| `src/agent.hpp` | 玩家状态：手牌、副露、立直、振听、碰/吃/杠逻辑 |
| `src/ai_proxy.hpp` | 与 Python AI 引擎的 IPC 桥接；失败时降级为随机 AI |

### 协议

所有消息为 WebSocket 上的 **NDJSON**（换行分隔的 JSON），与现有协议完全一致：

```
客户端 → 服务端           服务端 → 客户端
─────────────────        ──────────────────
{"username":"...",       join, start, draw,
 "observe":false}        select_tile, discard,
{"event":"discard",      decision, chi, pon,
 "tile_id":N}            kan, riichi, agari,
{"event":"decision",     ryuukyoku, settlement,
 "action":{...}}         score, end, update
```

---

## Godot 客户端结构

Godot 客户端（`godot_client/`）使用 **GDScript + 程序化 UI** 快速迭代。

| 文件 | 说明 |
|------|------|
| `Scripts/Main.gd` | 入口 — 构建菜单/牌桌 UI，连接信号 |
| `Scripts/network/WebSocketClient.gd` | WebSocket 连接、自动重连、帧解析 |
| `Scripts/network/Protocol.gd` | NDJSON 事件 → Godot 信号分发 |
| `Scripts/game/GameState.gd` | 客户端游戏状态：手牌、牌河、副露、分数、牌常量 |
| `Scenes/Main.tscn` | 最小场景引导 |

### Web 导出

导出为 Web 版本：

1. Godot 中：**项目 → 导出 → 添加 → Web**
2. 启用 **Threads** 和 **WebSocket** 支持
3. 导出为 HTML5/WASM
4. C++ 后端原生支持 WebSocket，无需 `websockify` 桥接

---

## 有监督学习

### 下载天凤牌谱

从[天凤凤凰桌](https://tenhou.net/)获取高水平对局数据：

```powershell
# 下载近 7 日对局日志 → logs/
python dataset/download_logs.py

# 从日志下载牌谱 → data/
python dataset/download_data.py
```

历史数据可前往[天凤日志记录](https://tenhou.net/sc/raw/)手动下载 `scraw` 压缩包，解压后用：

```powershell
./ungz.sh 2022/           # 提取 .scc 牌谱
mv 2022/*.txt logs/       # 移动到 logs/
python dataset/download_data.py
```

### 训练模型

```powershell
# 弃牌模型（基础）
python sl_train/train_discard_model.py --num_layers 50 --epochs 10

# 立直模型
python sl_train/train_riichi_model.py --num_layers 20 --epochs 10

# 副露模型
python sl_train/train_furo_model.py --mode chi --num_layers 50 --epochs 10 --pos_weight 10

# 奖励预测器
python sl_train/train_reward.py
```

---

## 深度强化学习（Self-Play 微调）

通过自博弈 + Reward Predictor 按 Suphx 思路微调弃牌策略：

```powershell
# REINFORCE（基础策略梯度）
python rl_train/train_discard_rl.py --episodes 200 --gamma 0.99 --lr 1e-5

# PPO（Actor-Critic + Clip + 熵正则）
python rl_train/train_discard_ppo.py --episodes 200 --gamma 0.99 --actor_lr 1e-5 --critic_lr 2e-4

# PPO + Oracle Guiding（教师策略使用隐藏特征）
python rl_train/train_discard_ppo.py --oracle_guiding --oracle_guiding_coef 0.05 --episodes 200
```

Checkpoint 默认保存至 `output/discard-rl-model/checkpoints/` 和 `output/discard-ppo-model/checkpoints/`。

自博弈时可开启快速模式减少等待：

```powershell
python online_game/server.py -A 4 -f -ob
```

---

## 项目结构

```
MahJong_RLAI/
├── mahjong/              #   Python 游戏引擎
│   ├── game.py           #     对局主循环、牌山、局顺、宝牌管理
│   ├── agent.py          #     玩家/AI Agent 状态与决策
│   ├── yaku.py           #     役种判定
│   ├── check_agari.py    #     和牌判定
│   ├── utils.py          #     工具函数（编码/解码/特征提取）
│   ├── display.py        #     牌局展示
│   └── *.pkl             #     预计算表（和牌/听牌）
├── dataset/              #   数据采集与处理
│   ├── download_logs.py  #     下载天凤对局日志
│   ├── download_data.py  #     下载牌谱
│   ├── tenhou.py         #     天凤牌谱解析
│   └── data.py           #     PyTorch Dataset 封装
├── model/                #   神经网络模型
│   ├── models.py         #     DiscardModel / RiichiModel / FuroModel / RewardPredictor
│   └── saved/            #     训练好的模型权重
├── sl_train/             #   有监督学习训练脚本
├── rl_train/             #   强化学习训练脚本
├── online_game/          #   传统在线对战（Python 服务端 + 网页客户端）
│   ├── server.py         #     Python 服务端（含 AI）
│   ├── Server/           #     旧版 C++ 服务端（TCP, WinSock）
│   ├── client.py         #     命令行客户端
│   ├── web_client/       #     Phaser.js 网页客户端
│   └── mah-jong/         #     旧版 Godot 4.6 客户端（早期原型）
├── cpp_backend/          # ★ 新 C++ WebSocket 后端
│   ├── src/
│   │   ├── main.cpp      #     入口
│   │   ├── server.hpp    #     WebSocket 服务器 + 连接管理
│   │   ├── websocket.hpp #     RFC 6455 WebSocket 实现
│   │   ├── json.hpp      #     JSON 解析/序列化
│   │   ├── game_engine.hpp#    游戏引擎 (MahjongGame + GameEnvironment + 游戏循环)
│   │   ├── agent.hpp     #     玩家状态 + 副露逻辑
│   │   └── ai_proxy.hpp  #     Python AI IPC 桥接
│   ├── build.bat         #     构建脚本
│   └── Makefile          #     Make 构建
├── godot_client/         # ★ 新 Godot 4.6 客户端
│   ├── Scenes/           #     场景文件
│   ├── Scripts/
│   │   ├── Main.gd       #     入口（程序化 UI）
│   │   ├── network/      #     WebSocketClient + 协议处理
│   │   ├── game/         #     GameState（牌模型、分数）
│   │   └── ui/           #     （计划中：UI 组件）
│   └── project.godot     #     Godot 项目配置
├── check.py              #   检测 CUDA 可用性
└── requirements.txt
```

---

## 协议速查

所有消息为 WebSocket 上的 **NDJSON**。协议在 C++ 服务端、Python 服务端、Godot 客户端和网页客户端间保持一致。

| 服务端 → 客户端 | 关键字段 |
|-----------------|---------|
| `join` | `status` (1=成功), `message` |
| `start` | `game` (局数、本场、宝牌、玩家...), `self` (手牌、座位...) |
| `draw` | `who`, `tile_id`, `where` |
| `select_tile` | `tiles`, `banned`, `tsumo`, `riichi` |
| `discard` | `who`, `tile_id`, `mode` (0=手切, 1=摸切) |
| `decision` | `actions[]` — 每项: `{type, who, from_who, pattern, machi}` |
| `chi` / `pon` / `kan` | `action` — `{who, from_who, pattern}` |
| `riichi` | `action` — `{who, status}` |
| `agari` | `action[]` — `[{who, from_who, score, ...}]`, `ura_dora_indicator` |
| `ryuukyoku` | `why`, `nagashimangan` |
| `settlement` | `res`, `score` (点数变化), `ura_dora` |
| `end` | `message` |

| 客户端 → 服务端 | 关键字段 |
|----------------|---------|
| 加入 | `{"username":"...", "observe":false}` |
| 切牌 | `{"event":"discard", "tile_id":N}` |
| 操作 | `{"event":"decision", "action":{...}}` |
| 就绪 | `{"event":"ready"}` |

---

## 模型架构

所有策略网络基于 **1D 卷积残差网络**：

- **输入**：34 维通道 × 牌种特征（手牌、副露、河牌、宝牌、场况等）
- **主体**：N 层 ResBlock（3×1 Conv → BN → LeakyReLU → 3×1 Conv → BN → LeakyReLU + 残差连接）
- **输出**：
  - `DiscardModel`：34 维 logits（34 种牌的弃牌概率）
  - `RiichiModel`：标量（立直概率）
  - `FuroModel`：标量（副露概率）
  - `RewardPredictor`：标量（预测最终顺位/得分）

---

## 参考资料

- [Suphx: Mastering Mahjong with Deep Reinforcement Learning](https://arxiv.org/abs/2003.13590)
- [天凤 (Tenhou)](https://tenhou.net/)
- [天凤日志记录](https://tenhou.net/sc/raw/)
- [RFC 6455 — The WebSocket Protocol](https://datatracker.ietf.org/doc/html/rfc6455)

## 贡献者
Lanyi_adict：模型 & RL 算法开发。
YiQing：前端开发。
