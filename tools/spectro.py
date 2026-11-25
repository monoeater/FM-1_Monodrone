#!/usr/bin/env python3
# spectrograms of WAVs into one PNG: spectro.py OUT.png A.wav B.wav ...
import sys, numpy as np, matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from scipy.io import wavfile
files = sys.argv[2:]
fig, ax = plt.subplots(len(files), 2, figsize=(14, 2.3 * len(files)), gridspec_kw={"width_ratios": [4, 1]})
ax = np.atleast_2d(ax)
for i, f in enumerate(files):
    sr, x = wavfile.read(f); x = x.astype(np.float32).mean(axis=1) / 32768
    ax[i, 0].specgram(x, NFFT=2048, Fs=sr, noverlap=1024, cmap="magma", vmin=-120, vmax=-20)
    ax[i, 0].set_yscale("symlog", linthresh=200); ax[i, 0].set_ylim(20, 20000)
    ax[i, 0].set_title(f.split("/")[-1], fontsize=8, loc="left")
    m = len(x) // 2; seg = x[m:m + 2205]
    ax[i, 1].plot(seg, lw=0.5); ax[i, 1].set_ylim(-1, 1)
plt.tight_layout(); plt.savefig(sys.argv[1], dpi=70)
