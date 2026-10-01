# MahJong RLAI —— 后端开发文档

## 一、项目概览

本项目是一个在线日式麻将（立直麻将）对战服务器系统，支持人机对战与在线玩家对战。后端由 **C++ 游戏服务器** 与 **Python AI 引擎** 两部分组成，客户端支持 **Python 终端客户端** 和 **Web 网页客户端**。

### 技术栈

| 层级 | 语言 | 核心依赖 | 用途 |
|------|------|----------|------|
| 游戏服务器 | C++17 | WinSock2 / POSIX socket | TCP 网络通信、玩家管理、游戏逻辑 |
| AI 引擎 | Python | PyTorch, NumPy | 神经网络决策（切牌/立直/副露） |
| 游戏核心 | Python | pickle 查找表 | 和牌判定、役种计算、听牌检测 |
| Web 前端 | HTML/JS | Phaser.js | 图形化麻将客户端 |

### 核心目录结构

```
MahJong_RLAI/
├── online_game/
│   ├── Server/
│   │   ├── server.cpp          # C++ 游戏服务器（主文件）
│   │   ├── server.exe          # 编译产物
│   │   └── readme              # 本文档
│   ├── server.py               # Python 版服务器（含 AI 决策逻辑）
│   ├── client.py               # Python 终端客户端
│   └── web_client/             # Web 网页客户端
│       ├── index.html
│       └── js/src/
│           ├── render.js       # Phaser 渲染引擎
│           └── communication.js # WebSocket 通信
├── mahjong/                    # 麻将游戏核心库
│   ├── game.py                 # 游戏状态机 (MahjongGame)
│   ├── agent.py                # 玩家状态 (Agent)
│   ├── yaku.py                 # 役种判定 (Yaku, YakuList)
│   ├── check_agari.py          # 和牌检测（预计算表查表法）
│   ├── utils.py                # 工具函数
│   ├── display.py              # ASCII 艺术渲染
│   ├── AGARI_TABLE_2.pkl       # 和牌查找表 (~300MB)
│   └── MACHI_TABLE.pkl         # 听牌查找表 (~200MB)
├── model/
│   └── models.py               # PyTorch 神经网络模型定义
│       ├── DiscardModel        # 切牌模型
│       ├── RiichiModel         # 立直判定模型
│       └── FuroModel           # 副露判定模型
├── sl_train/                   # 监督学习训练脚本
├── rl_train/                   # 强化学习训练脚本
└── dataset/                    # 数据加载与预处理
```

---

## 二、C++ 服务器架构 (server.cpp)

### 2.1 编译与运行

```bash
# 编译 (MinGW-w64 / GCC)
g++ -std=c++17 -O2 server.cpp -o server.exe -lws2_32

# 运行
server.exe -H 0.0.0.0 -P 9999 -A 4 -f -d -ob -m 0
```

### 2.2 命令行参数

| 参数 | 长格式 | 默认值 | 说明 |
|------|--------|--------|------|
| `-H` | `--host` | `0.0.0.0` | 监听地址 |
| `-P` | `--port` | `9999` | 监听端口 |
| `-A` | `--AI` | `0` | AI 玩家数量 (0~4) |
| `-m` | `--min-score` | `0` | 最低分数阈值 (×100) |
| `-ob` | `--allow-observe` | `false` | 允许观战 |
| `-f` | `--fast` | `false` | 跳过 AI 思考延迟 |
| `-d` | `--debug` | `false` | 启用调试日志 |

### 2.3 组件架构图

```
┌──────────────────────────────────────────────────────┐
│                      main()                          │
│  WSAStartup → create_server() → bind/listen → run() │
└──────────────────┬───────────────────────────────────┘
                   │
     ┌─────────────┴────────────────────────┐
     │            Server.run()              │
     │  ┌──────────────────────────────┐    │
     │  │  handle_connections() thread │    │
     │  │  accept() → handshake → join │    │
     │  └──────────────────────────────┘    │
     │  ┌──────────────────────────────┐    │
     │  │  game_main_loop() thread     │    │
     │  │  start → game_loop → update  │    │
     │  └──────────────────────────────┘    │
     └──────────────────────────────────────┘
                   │
     ┌─────────────┴────────────────────────┐
     │         GameEnvironment              │
     │  ┌──────────┐  ┌──────────────────┐  │
     │  │ Mahjong  │  │  Client[4]       │  │
     │  │ Game     │  │  observe_info    │  │
     │  │  ┌─────┐ │  │  observers       │  │
     │  │  │Agent│ │  └──────────────────┘  │
     │  │  │[0-3]│ │                        │
     │  │  └─────┘ │                        │
     │  └──────────┘                        │
     └──────────────────────────────────────┘
```

### 2.4 核心数据结构

#### 2.4.1 JSON 库 (`json::Value`)

内嵌的轻量级 JSON 解析/序列化库，支持所有 JSON 数据类型：

```cpp
// 解析
json::Value msg = json::parse("{\"event\":\"join\",\"status\":1}");

// 访问
std::string event = msg["event"].as_string();  // "join"
int status = msg["status"].as_int();            // 1

// 构造
json::Value resp;
resp["event"] = "draw";
resp["who"] = 0;
resp["tile_id"] = 52;

// 序列化
std::string wire = json::serialize(resp);  // "{\"event\":\"draw\",\"who\":0,\"tile_id\":52}"
```

#### 2.4.2 ControlledQueue\<T\>

线程安全的消息队列，通过 `allow_put` 原子标志实现**单次生产**语义：

```cpp
template<typename T>
class ControlledQueue {
    std::queue<T> q;           // 底层队列
    std::mutex mtx;            // 互斥锁
    std::condition_variable cv; // 条件变量
    std::atomic<bool> allow_put{false}; // 每次只允许 put 一次
};
```

**Python 语义对照**：在 Python 服务器中，`ControlledQueue` 每次只允许一次 `put` 操作，之后自动锁定，直到调用 `allow_put()` 解锁。这确保游戏循环每次只读取一个客户端消息。

#### 2.4.3 Agent（玩家状态）

| 字段 | 类型 | 说明 |
|------|------|------|
| `score` | `int` | 点数 / 100 |
| `tiles` | `set<int>` | 手牌集合（天凤编号 0-135） |
| `hand_tile_counter` | `vector<int>` | 34 种牌型计数 |
| `discard_tiles` | `vector<int>` | 舍牌历史（含被鸣走的牌） |
| `river` | `vector<int>` | 牌河（仅未被鸣走的牌） |
| `furo` | `map<FuroKey, vector<int>>` | 副露（吃/碰/杠） |
| `riichi_status` | `int` | 是否立直中 |
| `ippatsu_status` | `int` | 是否有一发 |
| `machi` | `set<int>` | 听牌列表 |
| `*_furiten` | `bool` | 三种振听状态 |

**FuroKey 编码**：`(furo_type, pattern, nth)`

| furo_type | 含义 | pattern |
|-----------|------|---------|
| 0 | 吃 | min_tile_type |
| 1 | 碰 | tile_type |
| 2 | 暗杠 | tile_type |
| 3 | 明杠/加杠 | tile_type |

#### 2.4.4 MahjongGame（游戏状态机）

| 字段 | 类型 | 说明 |
|------|------|------|
| `yama` | `vector<int>` | 牌山（136 张） |
| `left_num` | `int` | 流局前剩余摸牌数 |
| `round` | `int` | 局顺 (0=东一局) |
| `round_wind` | `int` | 场风 (27=东, 28=南, 29=西, 30=北) |
| `honba` | `int` | 本场数 |
| `riichi_ba` | `int` | 立直棒数 |
| `dora_indicator` | `vector<int>` | 宝牌指示牌列表 |
| `dora` | `vector<int>` | 宝牌列表 |
| `ura_dora_indicator` | `vector<int>` | 里宝指示牌列表 |
| `oya` | `int` | 亲家索引 (0-3) |
| `kang_num` | `vector<int>[4]` | 各玩家杠数 |
| `agents` | `vector<Agent>[4]` | 四位玩家 |
| `first_round` | `bool` | 是否第一巡（天地和/九种九牌判定用） |

**关键方法**：

| 方法 | 说明 |
|------|------|
| `new_game(round, honba, riichi_ba)` | 初始化牌山、配牌、宝牌 |
| `draw_tile(who)` | 摸牌，更新 left_num |
| `discard_tile(who, tile_id)` | 舍牌 |
| `pon(who, tiles, kui_tile, from_who)` | 碰 |
| `chi(who, tiles, kui_tile, from_who)` | 吃 |
| `kan(who, tiles, mode)` | 杠 (mode: 0=暗杠, 1=明杠, 2=加杠) |
| `riichi(who)` | 立直 |
| `new_dora()` | 翻新宝牌 |
| `check_pon/chi/kan(who, tile, mode)` | 检查副露可行性 |

### 2.5 网络协议

#### 2.5.1 传输格式

所有消息采用 **换行分隔的 JSON** (NDJSON) 格式：

```
JSON字符串\n
```

#### 2.5.2 客户端 → 服务器

| Event | 字段 | 说明 |
|-------|------|------|
| 握手 | `{"username":"...", "observe":bool}` | 加入房间 / 观战 |
| `discard` | `{"event":"discard", "tile_id":int}` | 选择舍牌 |
| `decision` | `{"event":"decision", "action":{...}}` | 选择行为（和/吃/碰/杠/立直/pass） |
| `ready` | `{"event":"ready"}` | 局间确认继续 |
| `quit` | `{"event":"quit"}` | 离开房间 |
| `change_ob` | `{"event":"change_ob", "username":"..."}` | 切换观战目标 |

#### 2.5.3 服务器 → 客户端

| Event | 字段 | 说明 |
|-------|------|------|
| `join` | `{"event":"join", "status":int, "message":"..."}` | 加入结果 (1=成功, 0=失败, -1=观战) |
| `start` | `{"event":"start", "game":{...}, "self":{...}}` | 游戏开始信息 |
| `draw` | `{"event":"draw", "who":int, "tile_id":int, "where":int}` | 摸牌通知 |
| `select_tile` | `{"event":"select_tile", "tiles":"all"/[...]}` | 请求舍牌选择 |
| `discard` | `{"event":"discard", "who":int, "tile_id":int, "mode":int, "after_tsumo":bool}` | 舍牌广播 |
| `decision` | `{"event":"decision", "actions":[{...}]}` | 请求行为选择 |
| `chi/pon/kan` | `{"event":"chi", "action":{...}}` | 副露广播 |
| `riichi` | `{"event":"riichi", "action":{...}}` | 立直广播 |
| `agari` | `{"event":"agari", "action":[{...}], "ura_dora_indicator":[...]}` | 和牌广播 |
| `ryuukyoku` | `{"event":"ryuukyoku", "why":"..."}` | 流局广播 |
| `settlement` | `{"event":"settlement", "res":{...}, "score":[...], "ura_dora":[...]}` | 局终结算 |
| `score` | `{"event":"score", "score":[[who,score],...]}` | 排名广播 |
| `end` | `{"event":"end", "message":"..."}` | 游戏结束 |
| `update` | `{"event":"update", "key":str, "value":any}` | 状态更新（furiten/machi/left_num） |

### 2.6 游戏循环流程

```
game_main_loop()
│
├── 洗牌玩家顺序
│
├── while game_start:
│   ├── env.start()         ← 开局
│   ├── send_all_game_info()
│   │
│   ├── for each turn:
│   │   ├── draw_tile(who)           ← 摸牌
│   │   │   ├── 发送 draw 消息给摸牌者
│   │   │   └── 广播 draw 给其他玩家
│   │   │
│   │   ├── select_tile(client)      ← 等待舍牌选择
│   │   │   ├── 人类: fetch_message() (阻塞)
│   │   │   └── AI: ai_discard()
│   │   │
│   │   ├── discard_tile(who, tile)  ← 执行舍牌
│   │   ├── 广播 discard 消息
│   │   ├── 广播 wait 消息
│   │   └── current_player++ (轮转)
│   │
│   ├── 流局或和牌判定
│   │
│   ├── game_update(res)             ← 点数结算
│   │
│   ├── 发送 settlement 消息
│   │
│   └── 检查游戏结束条件
│       ├── 最低分数阈值
│       ├── 局数 >= 12
│       └── 东风/南风结束条件
│
└── reset() → 等待下一局
```

### 2.7 AI 决策（当前为随机）

当前 C++ 版本中 AI 为**占位实现**，实际智能决策在 Python 服务器中：

```cpp
// 切牌: 随机选择未在禁令列表中的牌
int ai_discard(int who, const vector<int>& tiles, const vector<int>& banned) {
    vector<int> candidates;
    for (int t : tiles)
        if (find(banned.begin(), banned.end(), t / 4) == banned.end())
            candidates.push_back(t);
    return candidates[rand() % candidates.size()];
}

// 行为选择: 有和牌则和，否则 pass
json::Value ai_decision(int who, const vector<json::Value>& actions) {
    for (auto& act : actions)
        if (act["type"].as_string() == "agari") return act;
    return actions[0];  // pass
}
```

---

## 三、Python 服务器 (server.py)

Python 版本包含完整 AI 决策逻辑，使用 PyTorch 神经网络模型。

### 3.1 运行

```bash
# 基础模式（纯人类玩家）
python server.py -H 0.0.0.0 -P 9999

# 4 AI 训练模式
python server.py -A 4 -f -t

# 带观战
python server.py -A 2 -ob
```

### 3.2 AI 决策流程

```
decide_by_ai(who, actions)
│
├── 对每个 action 计算分数:
│   ├── agari → AiAgent.agari_decision()     (规则判定)
│   ├── riichi → AiAgent.riichi_decision()   (RiichiModel 网络)
│   ├── pon → AiAgent.pon_decision()         (FuroModel 网络)
│   ├── chi → AiAgent.chi_decision()         (FuroModel 网络)
│   ├── kan → AiAgent.kan_decision()         (FuroModel 网络)
│   └── ryuukyoku → 九种九牌判定
│
├── 取最高分 action
├── 分数 < 0.5 → pass
└── 返回选中的 action

discard_by_ai(who, tiles, banned)
│
├── 获取当前局面特征 (34×?通道)
├── 训练模式: 从 DiscardModel 输出概率分布中采样
└── 推理模式: 选择模型输出的最高置信度切牌
```

### 3.3 特征编码

游戏局面编码为多通道特征张量，送入 CNN 网络：

| 通道组 | 通道数 | 含义 |
|--------|--------|------|
| 手牌 | 4 | 每种牌持有 0-4 张（独热行的累计和） |
| 副露 | 16 | 4 个副露 × 4 通道 |
| 牌河 | ~20+ | 各家舍牌历史 |
| 宝牌/场风/自风 | ~10 | 指示特征 |

---

## 四、神经网络模型 (model/models.py)

### 4.1 模型架构

```
INPUT (in_channels × 34)      ← 多通道局面特征
    │
    ▼
Conv1d(in→256, k=3) + BN + LeakyReLU
    │
    ▼
ResBlock × N                  ← N 个残差块
│   Conv1d(256→256, k=3) + BN + LeakyReLU
│   Conv1d(256→256, k=3) + BN + LeakyReLU
│   skip connection
    │
    ▼
Conv1d(256→1, k=1) + BN       ← 输出通道压缩
    │
    ▼
OUTPUT (34)                   ← 34 种牌的 logits / 得分
    Softmax → 概率分布
```

### 4.2 模型变体

| 模型 | 输入通道 | 输出 | 用途 |
|------|----------|------|------|
| `DiscardModel` | 局面特征通道 | 34 维 logits | 切牌选择 |
| `RiichiModel` | 局面特征通道 | 1 维标量 | 立直意愿 (sigmoid) |
| `FuroModel` | 局面+副露特征通道 | 1 维标量 | 副露意愿 (sigmoid) |
| `RewardPredictor` | 74 通道 | 1 维标量 | 奖励预测 (用于 PPO) |

### 4.3 训练方式

| 阶段 | 方法 | 脚本 |
|------|------|------|
| 切牌模型预训练 | 监督学习（天凤牌谱） | `sl_train/train_discard_model.py` |
| 副露模型预训练 | 监督学习 | `sl_train/train_furo_model.py` |
| 立直模型预训练 | 监督学习 | `sl_train/train_riichi_model.py` |
| 强化学习微调 | PPO | `rl_train/train_discard_ppo.py` |

---

## 五、游戏规则引擎 (mahjong/)

### 5.1 和牌检测 (check_agari.py)

基于**预计算查表法**，在初始化时将 34 种牌型的 14 张牌组合预先计算并序列化：

- `AGARI_TABLE_2.pkl`：所有合法和牌形 → 和牌二进制编码
- `MACHI_TABLE.pkl`：所有听牌形 → 听牌列表

```python
def is_agari(counter):
    """查表和牌判定，O(1) 复杂度"""
    pattern = counter_to_pattern(counter)
    key = calculate_pattern_key(pattern)
    return agari_table[0].get(key)  # None = 未和牌
```

### 5.2 役种判定 (yaku.py)

`Yaku` 类执行完整的役种计算：

- **役满判定**：国士无双、四暗刻、大三元、九莲宝灯、绿一色等
- **通常役判定**：立直、平和、断幺九、一气通贯、三色同顺等
- **符数计算**：根据和牌形式的刻子/顺子/雀头计算符数
- **宝牌计算**：含赤宝牌、里宝牌

### 5.3 天凤编号系统

麻将牌使用天凤 0-135 编号：

```
万子:  0,4,8,12     = 1万  (类型 0)   赤5万 = 16
      1,5,9,13     = 2万  (类型 1)
      ...
      7,11,15,19   = 8万  (类型 7)
      3,7,11,15   ❌
      8,12,16,20   = 9万  (类型 8)
      
筒子:  36-71        = 1筒~9筒  (类型 9-17)  赤5筒 = 52
索子:  72-107       = 1索~9索  (类型 18-26) 赤5索 = 88
风牌:  108-123      = 东南西北  (类型 27-30)
箭牌:  124-135      = 白发射中  (类型 31-33)
```

每条公式：`类型 = tile_id / 4`，`数量 = tile_id % 4`

---

## 六、客户端接入指南

### 6.1 Python 终端客户端

```bash
python client.py -H localhost -P 9999 -U "玩家名"
python client.py -ob  # 观战模式
```

### 6.2 Web 客户端

在浏览器中打开 `online_game/web_client/index.html`，填入服务器地址和端口即可连接。

### 6.3 自定义客户端开发

协议流程：

```
1. 建立 TCP 连接到 server:port
2. 发送握手: {"username":"name", "observe":false}\n
3. 接收 join 响应 (status=1 成功)
4. 等待 start 事件 → 接收游戏初始化信息
5. 游戏循环:
   ├── 接收 draw → 显示摸牌
   ├── 接收 select_tile / decision → 发送玩家选择
   ├── 接收 discard → 更新牌河
   ├── 接收 chi/pon/kan/riichi → 更新局面
   └── 接收 settlement/end → 局终处理
```

---

## 七、部署建议

### 开发环境

```bash
# Python 依赖
pip install torch numpy

# C++ 编译 (Windows MinGW)
g++ -std=c++17 -O2 server.cpp -o server.exe -lws2_32

# C++ 编译 (Linux)
g++ -std=c++17 -O2 server.cpp -o server -lpthread
```

### 生产环境注意事项

1. **查找表文件**：`AGARI_TABLE_2.pkl` 和 `MACHI_TABLE.pkl` 约 500MB，Python 服务器启动时加载到内存
2. **AI 模型文件**：训练好的 `.pt` 模型需放在 `model/saved/` 目录下
3. **网络配置**：默认端口 9999，需确保防火墙放行
4. **并发**：每个客户端占用独立线程，观战者通过指针引用不额外消耗
5. **断开重连**：游戏进行中玩家断线后可用相同用户名重连

### 游戏结束条件

- 任意玩家分数低于最低阈值 (`-m` 参数，×100)
- 东风战 4 局 / 南风战 8 局后最高分不足 30000 点
- 最高分玩家为亲家且满足局数条件