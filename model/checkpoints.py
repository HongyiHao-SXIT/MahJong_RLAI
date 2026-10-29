from pathlib import Path

import torch


def load_trusted_checkpoint(path, map_location):
    """Load a project-generated checkpoint that may contain legacy NumPy metadata."""
    return torch.load(Path(path), map_location=map_location, weights_only=False)
