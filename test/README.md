# MahJong_RLAI 测试文档

本文档用于说明本项目当前可执行的测试方式，以及后续如何补充自动化测试。

## 1. 测试目标

- 验证 Python 运行环境、关键依赖和 CUDA 状态。
- 验证代码静态质量（flake8）。
- 验证核心模块的单元测试能力（pytest）。
- 验证在线对战链路的集成可用性（服务端 + 客户端）。

## 2. 环境准备

在仓库根目录执行：

```powershell
conda create -n mahjong python=3.9 -y
conda activate mahjong
pip install -r requirements.txt
pip install flake8 pytest
```

## 3. 当前测试现状

- 当前仓库中尚无 `test_*.py` 或 `*_test.py` 文件。
- CI（`.github/workflows/python-package-conda.yml`）已配置：
  - `flake8` 静态检查。
  - 若存在测试文件则运行 `python -m pytest`，否则跳过。

## 4. 快速检查（Smoke Test）

### 4.1 Python 与 CUDA 检查

```powershell
python check.py
```

预期输出包含：

- PyTorch 版本
- Tianshou 版本
- CUDA 是否可用
- 若可用，显示 CUDA 版本与 GPU 设备名

### 4.2 代码静态检查

```powershell
python -m flake8 . --count --select=E9,F63,F7,F82 --show-source --statistics
python -m flake8 . --count --exit-zero --max-complexity=10 --max-line-length=127 --statistics
```

说明：

- 第一条命令用于阻断严重语法/名称错误。
- 第二条命令用于输出风格和复杂度统计，不阻断流程。

### 4.3 pytest 自动化测试

```powershell
python -m pytest -q
```

如果没有任何测试文件，属于正常现象（与 CI 行为一致）。

## 5. 手工集成测试

## 5.1 C++ 后端可用性

```powershell
cd cpp_backend
build.bat
build\server.exe -P 9999 -A 3 -ob
```

检查项：

- 服务启动成功，无崩溃。
- 端口监听正常。
- 有客户端连接时可正常开始对局。

## 5.2 Python 在线服务可用性

在仓库根目录执行：

```powershell
python online_game/server.py -A 3 -H 0.0.0.0
```

检查项：

- 服务可启动。
- AI 玩家可加入并推进流程。

## 5.3 Godot 客户端联调

- 使用 Godot 4.6+ 打开 `godot_client/project.godot`。
- 连接 `127.0.0.1:9999`。
- 验证以下流程：连接、发牌、摸切、副露决策、结算消息。

## 6. 新增 pytest 测试规范

建议在 `test/` 目录按模块拆分：

- `test_game.py`：局流程、摸牌/舍牌状态转移。
- `test_yaku.py`：役种判定与边界输入。
- `test_agent.py`：动作可选集合与合法性约束。

命名规范必须满足以下之一，CI 才会自动执行：

- `test_*.py`
- `*_test.py`

推荐测试函数命名：

- `def test_xxx():`

## 7. 建议的最小测试命令集

本地提交前建议执行：

```powershell
python check.py
python -m flake8 . --count --select=E9,F63,F7,F82 --show-source --statistics
python -m pytest -q
```

## 8. 常见问题

### 8.1 pytest 没有运行任何测试

原因：当前没有匹配命名规则的测试文件。  
处理：新增 `test_*.py` 文件后重新执行。

### 8.2 `check.py` 显示 CUDA 不可用

原因：GPU 驱动、CUDA 或 PyTorch CUDA 版本不匹配。  
处理：确认显卡驱动后，重新安装与 CUDA 匹配的 PyTorch。
