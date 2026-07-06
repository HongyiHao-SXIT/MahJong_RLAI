# 基于 Suphx 的立直麻将 AI

复现微软亚洲研究院 [Suphx](https://arxiv.org/abs/2003.13590) 模型的立直麻将 AI，支持**有监督学习 → 强化学习微调 → 在线对战**的完整训练管线。

---

## 环境配置

```powershell
conda create -n mahjong python=3.9 -y
conda activate mahjong
pip install -r requirements.txt
```

主要依赖：`PyTorch 2.0`、`numpy`、`websockify`。

训练/对战需要 GPU，可通过 `python check.py` 验证 CUDA 是否可用。

---

## 快速开始：与 AI 对战

### 1. 准备模型

将训练好的权重放在 `model/saved/` 下：

```
model/saved/
├── discard-model/best.pt   # 弃牌模型（必须）
├── riichi-model/best.pt    # 立直模型
├── furo-model/best.pt      # 副露模型
└── reward-model/best.pt    # 奖励预测器
```

### 2. 启动服务端（终端 1）

```powershell
online_game/server.exe -A 3 -H 0.0.0.0
```

| 参数 | 说明 |
|------|------|
| `-A N` | AI 玩家数量（剩余席位留给人类） |
| `-f` | 快速模式，跳过思考/等待动画 |
| `-d` | Debug 模式，输出 AI 决策置信度 |
| `-ob` | 观战模式 |

### 3. 启动 WebSocket 桥接（终端 2）

```powershell
websockify 8888 127.0.0.1:9999
```

### 4. 启动网页客户端（终端 3）

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
├── mahjong/              # 游戏引擎
│   ├── game.py           #   对局主循环、牌山、局顺、宝牌管理
│   ├── agent.py          #   玩家/AI Agent 状态与决策
│   ├── yaku.py           #   役种判定
│   ├── check_agari.py    #   和牌判定
│   ├── utils.py          #   工具函数（编码/解码/特征提取）
│   ├── display.py        #   牌局展示
│   └── *.pkl             #   预计算表（和牌/听牌）
├── dataset/              # 数据采集与处理
│   ├── download_logs.py  #   下载天凤对局日志
│   ├── download_data.py  #   下载牌谱
│   ├── tenhou.py         #   天凤牌谱解析
│   └── data.py           #   PyTorch Dataset 封装
├── model/                # 神经网络模型
│   ├── models.py         #   DiscardModel / RiichiModel / FuroModel / RewardPredictor
│   └── saved/            #   训练好的模型权重
├── sl_train/             # 有监督学习训练脚本
│   ├── train_discard_model.py
│   ├── train_riichi_model.py
│   ├── train_furo_model.py
│   ├── train_reward.py
│   └── training_utils.py
├── rl_train/             # 强化学习训练脚本
│   ├── train_discard_rl.py    #  REINFORCE
│   └── train_discard_ppo.py   #  PPO + Oracle Guiding
├── online_game/          # 在线对战
│   ├── server.exe        #   游戏服务端
│   ├── client.py         #   命令行客户端
│   └── web_client/       #   网页客户端（Phaser 3 + WebSocket）
│       ├── index.html
│       ├── js/src/       #   渲染逻辑 / 网络通信
│       ├── img/          #   麻将牌贴图
│       └── audio/        #   音效
├── check.py              # 检测 CUDA 可用性
└── requirements.txt
```

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