from typing import Callable, List, Sequence, Tuple

import matplotlib
matplotlib.use('Agg')
from matplotlib import pyplot as plt
from sklearn.metrics import accuracy_score, auc, precision_recall_fscore_support, roc_curve
import torch


def split_train_test_by_files(train_set, test_set, train_ratio: float = 0.8) -> int:
    """Split dataset files into train and test sets by file order after shuffling."""
    total_files = len(train_set)
    num_train_files = int(train_ratio * total_files)
    train_set.data_files, test_set.data_files = (
        train_set.data_files[:num_train_files],
        train_set.data_files[num_train_files:],
    )
    return num_train_files


def compute_best_threshold(y_true: Sequence[float], y_score: Sequence[float]) -> Tuple[float, float, List[float], List[float]]:
    """Return best threshold by maximizing Youden's J statistic and ROC-AUC."""
    fpr, tpr, thresholds = roc_curve(y_true, y_score)
    roc_auc = auc(fpr, tpr)
    best_threshold_index = (tpr - fpr).tolist().index(max(tpr - fpr))
    threshold = thresholds[best_threshold_index]
    return float(threshold), float(roc_auc), fpr.tolist(), tpr.tolist()


def save_roc_curve(fpr: Sequence[float], tpr: Sequence[float], roc_auc: float, output_path: str) -> None:
    plt.figure()
    plt.plot(fpr, tpr, color='darkorange', lw=1, label='ROC curve (area = %0.2f)' % roc_auc)
    plt.xlim([0.0, 1.0])
    plt.ylim([0.0, 1.05])
    plt.xlabel('False Positive Rate')
    plt.ylabel('True Positive Rate')
    plt.title('Receiver Operating Characteristic')
    plt.legend(loc='lower right')
    plt.savefig(output_path)
    plt.close()


def evaluate_binary_classification(
    y_true: Sequence[float],
    y_score: Sequence[float],
    threshold: float,
) -> Tuple[float, float, float, float]:
    y_pred = [int(score > threshold) for score in y_score]
    acc = accuracy_score(y_true=y_true, y_pred=y_pred)
    precision, recall, f_score, _ = precision_recall_fscore_support(
        y_true=y_true,
        y_pred=y_pred,
        average='binary',
        zero_division=0,
    )
    return float(recall), float(precision), float(acc), float(f_score)


@torch.no_grad()
def collect_binary_scores(
    model,
    dataset,
    process_data_fn: Callable,
    label_transform: Callable,
    device,
) -> Tuple[List[float], List[float], int]:
    y_true: List[float] = []
    y_score: List[float] = []
    dataset_size = len(dataset)
    while len(dataset) > 0:
        one_batch = dataset()
        if len(one_batch) == 0:
            break
        features, labels = process_data_fn(one_batch, label_trans=label_transform)
        features, labels = features.to(device), labels.to(device)
        output = model(features).sigmoid().flatten()
        y_true.extend(labels.tolist())
        y_score.extend(output.tolist())
        print(f"Testing {dataset_size - len(dataset)} / {dataset_size}".center(50, '-'), end='\r')
    dataset.reset()
    return y_true, y_score, dataset_size
