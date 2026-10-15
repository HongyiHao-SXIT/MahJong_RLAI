import torch
from torch import nn


class ResBlock(nn.Module):
    def __init__(self):
        super(ResBlock, self).__init__()
        self.layers = nn.Sequential(
            nn.Conv1d(256, 256, kernel_size=3, padding='same'),
            nn.BatchNorm1d(256),
            nn.LeakyReLU(0.2),
            nn.Conv1d(256, 256, kernel_size=3, padding='same'),
            nn.BatchNorm1d(256),
            nn.LeakyReLU(0.2)
        )

    def forward(self, x):
        return x + self.layers(x)


class DiscardModel(nn.Module):
    def __init__(self, in_channels, num_layers=20):
        super(DiscardModel, self).__init__()
        self.in_conv = nn.Sequential(
            nn.Conv1d(in_channels, 256, kernel_size=3, padding='same'),
            nn.BatchNorm1d(256),
            nn.LeakyReLU(0.2)
        )
        self.res_blocks = nn.Sequential(
            *(ResBlock() for _ in range(num_layers))
        )
        self.out_conv = nn.Sequential(
            nn.Conv1d(256, 1, kernel_size=1),
            nn.BatchNorm1d(1)
        )

    def forward(self, x):
        x = self.in_conv(x)
        x = self.res_blocks(x)
        x = self.out_conv(x)
        return x.squeeze(1)


class BinaryDecisionModel(nn.Module):
    """Suphx head for Riichi / Chow / Pong / Kong: ResNet trunk, 3x1 conv to 32 channels,
    FC 1024 -> 256 -> 2 logits (index 1 = take the action)."""

    def __init__(self, in_channels, num_layers=20):
        super().__init__()
        self.in_conv = nn.Sequential(
            nn.Conv1d(in_channels, 256, kernel_size=3, padding='same'),
            nn.BatchNorm1d(256),
            nn.LeakyReLU(0.2)
        )
        self.res_blocks = nn.Sequential(
            *(ResBlock() for _ in range(num_layers))
        )
        self.out_conv = nn.Sequential(
            nn.Conv1d(256, 32, kernel_size=3, padding='same'),
            nn.BatchNorm1d(32),
            nn.LeakyReLU(0.2)
        )
        self.fc = nn.Sequential(
            nn.Flatten(),
            nn.Linear(32 * 34, 1024),
            nn.BatchNorm1d(1024),
            nn.LeakyReLU(0.2),
            nn.Linear(1024, 256),
            nn.BatchNorm1d(256),
            nn.LeakyReLU(0.2),
            nn.Linear(256, 2)
        )

    def forward(self, x):
        x = self.in_conv(x)
        x = self.res_blocks(x)
        x = self.out_conv(x)
        return self.fc(x)

    def prob(self, x):
        """Probability of taking the action, shape (batch,)."""
        return self.forward(x).softmax(-1)[:, 1]


class RiichiModel(BinaryDecisionModel):
    pass


class FuroModel(BinaryDecisionModel):
    pass


class RewardPredictor(nn.Module):
    def __init__(self, input_dims, hidden_dims, num_layers):
        super(RewardPredictor, self).__init__()
        self.gru = nn.GRU(input_dims, hidden_dims, num_layers, batch_first=True)
        self.fc = nn.Sequential(
            nn.Linear(hidden_dims, 10),
            nn.ReLU(inplace=True),
            nn.Linear(10, 1)
        )

    def forward(self, x):
        out, h_n = self.gru(x)
        return self.fc(out[:, -1, :])