import argparse
import torch
from torch.nn import MSELoss
from torch.optim import Adam
import wandb
import os
import sys
sys.path.append(os.path.dirname(os.path.abspath(os.path.dirname(__file__))))
from dataset.data import process_reward_data, TenhouDataset
from model.models import RewardPredictor
from sl_train.training_utils import split_train_test_by_files


@torch.no_grad()
def evaluate_model(model, dataset: TenhouDataset):
    total_error = 0
    total = 0
    length = len(dataset)
    while len(dataset) > 0:
        data = dataset()
        if not data:
            break
        features, labels = process_reward_data(data)
        features, labels = features.to(device), labels.to(device)
        output = model(features)
        error = (output - labels).pow(2).sum()
        total_error += float(error.item())
        total += len(labels)
        print(f"Testing {length - len(dataset)} / {length} Error: {error:.3f}".center(50, '-'), end='\r')
    dataset.reset()
    if total == 0:
        return 0.0
    return total_error / total

mode = 'reward'
experiment = wandb.init(project='Mahjong', resume='allow', anonymous='must', name=f'train-{mode}')
assert experiment is not None, 'wandb init failed'


train_set = TenhouDataset(data_dir='data', batch_size=128, mode=mode, target_length=4)
test_set = TenhouDataset(data_dir='data', batch_size=128, mode=mode, target_length=4)
num_train_samples = split_train_test_by_files(train_set, test_set, train_ratio=0.8)

parser = argparse.ArgumentParser()
parser.add_argument('--hidden_dims', '-hd', default=50, type=int)
parser.add_argument('--num_layers', '-n', default=2, type=int)
parser.add_argument('--epochs', '-e', default=10, type=int)
args = parser.parse_args()

hidden_dims = args.hidden_dims
epochs = args.epochs
num_layers = args.num_layers
model = RewardPredictor(74, hidden_dims, num_layers)
device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
model.to(device)
optimizer = Adam(model.parameters())
loss_function = MSELoss()
scheduler = torch.optim.lr_scheduler.ReduceLROnPlateau(optimizer, mode='min', patience=1)

os.makedirs(f'output/{mode}-model/checkpoints', exist_ok=True)
min_mse = torch.inf
for epoch in range(epochs):
    while len(train_set) > 0:
        data = train_set()
        if not data:
            break
        features, labels = process_reward_data(data)
        features, labels = features.to(device), labels.to(device)
        output = model(features)
        loss = loss_function(output, labels)
        optimizer.zero_grad()
        loss.backward()
        optimizer.step()
        print(f"Epoch-{epoch + 1}: {num_train_samples - len(train_set)} / {num_train_samples} loss={loss.item():.3f}".center(50, '-'), end='\r')
        experiment.log({
            'train loss': loss.item(),
            'epoch': epoch + 1
        })

    train_set.reset()

    torch.save({"state_dict": model.state_dict(), "num_layers": num_layers, "hidden_dims": hidden_dims}, f'output/{mode}-model/checkpoints/epoch_{epoch + 1}.pt')
    model.eval()
    mse = evaluate_model(model, test_set)
    if mse < min_mse:
        min_mse = mse
        torch.save({"state_dict": model.state_dict(), "num_layers": num_layers, "hidden_dims": hidden_dims}, f'output/{mode}-model/checkpoints/best.pt')
    model.train()

    experiment.log({
        'epoch': epoch + 1,
        'test_mse': mse,
        'lr': optimizer.param_groups[0]['lr']
    })
    scheduler.step(mse)







