import torch
import tianshou

print(f"PyTorch version: {torch.__version__}")
print(f"Tianshou version: {tianshou.__version__}") # 顺便打印一下 Tianshou 版本
print(f"CUDA available: {torch.cuda.is_available()}")

if torch.cuda.is_available():
    # 正确获取 PyTorch 底层 CUDA 版本的方法
    print(f"CUDA version used by PyTorch: {torch.version.cuda}")
    print(f"GPU device: {torch.cuda.get_device_name(0)}")
else:
    print("No GPU device available")