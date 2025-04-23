# prompt: plot pallet1 and pallet2 side by side

import numpy as np
import IPython.display as display
from matplotlib import pyplot as plt
import io
import base64
import matplotlib.pyplot as plt
import matplotlib.patches as patches

palette1 = [
    (0, 0, 0), (0, 187, 0), (0, 132, 0), (0, 255, 0),
    (0, 0, 255), (0, 187, 255), (0, 132, 255), (0, 255, 255),
    (255, 0, 0), (255, 187, 0), (255, 132, 9), (255, 255, 0),
    (255, 0, 255), (255, 187, 255), (255, 132, 255), (255, 255, 255)
]

palette2 = [
    (0, 0, 0), (46, 210, 62), (35, 168, 59), (66, 245, 75),
    (0, 37, 233), (3, 192, 237), (1, 128, 239), (29, 251, 248),
    (236, 41, 19), (213, 207, 66), (217, 140, 49), (205, 247, 79),
    (191, 53, 240), (185, 195, 234), (183, 131, 239), (255, 255, 255)
]

def plot_palette(palette, ax):
    square_size = 1 / 4
    for i, color in enumerate(palette):
        row = i // 4
        col = i % 4
        x = col * square_size
        y = row * square_size
        rect = patches.Rectangle((x, y), square_size, square_size, linewidth=1, edgecolor='black', facecolor=tuple(c / 255 for c in color))
        ax.add_patch(rect)
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.set_aspect('equal')
    plt.axis('off')

fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(10, 5)) # Create two subplots side-by-side

plot_palette(palette1, ax1)
plot_palette(palette2, ax2)

plt.show()
