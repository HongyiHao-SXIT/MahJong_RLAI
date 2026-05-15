import os
import torch
from torch.optim import Adam
import wandb
import argparse
from torch.nn import BCEWithLogitsLoss
import sys
sys.path.append(os.path.dirname(os.path.abspath(os.path.dirname(__file__))))
from model.models import RiichiModel
from dataset.data import TenhouDataset, process_data
from sl_train.training_utils import (
    collect_binary_scores,
    compute_best_threshold,
    evaluate_binary_classification,
    save_roc_curve,
    split_train_test_by_files,
)


@torch.no_grad()
def evaluate_model(model, dataset: TenhouDataset, epoch):
    y_true, y_score, _ = collect_binary_scores(
        model=model,
        dataset=dataset,
        process_data_fn=process_data,
        label_transform=lambda x: x.float(),
        device=device,
    )
    threshold, roc_auc, fpr, tpr = compute_best_threshold(y_true, y_score)
    save_roc_curve(fpr, tpr, roc_auc, f'{mode}_model_roc_epoch{epoch}.png')
    recall, precision, acc, f_score = evaluate_binary_classification(y_true, y_score, threshold=0.6)
    return recall, precision, acc, f_score, threshold

parser = argparse.ArgumentParser()
parser.add_argument('--num_layers', '-n', default=20, type=int)
parser.add_argument('--epochs', '-e', default=10, type=int)
parser.add_argument('--pos_weight', '-w', default=None, type=int)
args = parser.parse_args()
mode = 'riichi'
experiment = wandb.init(project='Mahjong', resume='allow', anonymous='must', name=f'train-{mode}-sl')
assert experiment is not None, 'wandb init failed'
train_set = TenhouDataset(data_dir='data', batch_size=128, mode=mode, target_length=2)
test_set = TenhouDataset(data_dir='data', batch_size=128, mode=mode, target_length=2)
num_train_samples = split_train_test_by_files(train_set, test_set, train_ratio=0.8)


num_layers = args.num_layers
in_channels = 291
model = RiichiModel(num_layers=num_layers, in_channels=in_channels)
device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
model.to(device)
optimizer = Adam(model.parameters())
scheduler = torch.optim.lr_scheduler.ReduceLROnPlateau(optimizer, mode='max', patience=1)
if args.pos_weight is not None:
    loss_function = BCEWithLogitsLoss(pos_weight=torch.tensor(args.pos_weight, device=device))
else:
    loss_function = BCEWithLogitsLoss()
epochs = args.epochs

os.makedirs(f'output/{mode}-model/checkpoints', exist_ok=True)
max_f1 = 0
for epoch in range(epochs):
    while len(train_set) > 0:
        data = train_set()
        if not data:
            break
        features, labels = process_data(data, label_trans=lambda x: x.float())
        features, labels = features.to(device), labels.to(device)
        output = model(features).flatten()
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

    model.eval()
    recall, precision, acc, f_score, threshold = evaluate_model(model, test_set, epoch + 1)
    torch.save({
        "state_dict": model.state_dict(),
        "num_layers": num_layers,
        "in_channels": in_channels,
        "threshold": threshold
    }, f'output/{mode}-model/checkpoints/epoch_{epoch + 1}.pt')
    if f_score > max_f1:
        max_f1 = f_score
        torch.save({
            "state_dict": model.state_dict(),
            "num_layers": num_layers,
            "in_channels": in_channels,
            "threshold": threshold
        }, f'output/{mode}-model/checkpoints/best.pt')
    model.train()

    experiment.log({
        'epoch': epoch + 1,
        'test_f1': f_score,
        'test_recall': recall,
        'test_precision': precision,
        'test_acc': acc,
        'lr': optimizer.param_groups[0]['lr'],
        'threshold': threshold
    })
    scheduler.step(f_score)
