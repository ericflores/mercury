import numpy as np
from numpy.polynomial.hermite import hermgauss
xs, ws = hermgauss(200)
QINV = 1.2815515655446004   # Q^-1(0.1)

def biawgn(esn0_db):
    """Capacity and dispersion (bits) of the binary-input AWGN channel at Es/N0 (per BPSK symbol)."""
    g = 10 ** (esn0_db / 10)            # Es/N0, sigma^2 = 1/(2g)
    mu, sd = 4 * g, np.sqrt(8 * g)       # LLR ~ N(4g, 8g) given +1
    L = mu + np.sqrt(2) * sd * xs
    i = 1 - np.log2(1 + np.exp(-L))
    C = np.sum(ws * i) / np.sqrt(np.pi)
    V = np.sum(ws * i * i) / np.sqrt(np.pi) - C * C
    return C, V

def need_bpsk(K, N, eps_q=QINV):
    lo, hi = -20.0, 15.0
    for _ in range(60):
        mid = (lo + hi) / 2
        C, V = biawgn(mid)
        r = C - np.sqrt(V / N) * eps_q + np.log2(N) / (2 * N)
        if r < K / N: lo = mid
        else: hi = mid
    return hi

rng = np.random.default_rng(1)
def qam16_bicm(esn0_db, n=400000):
    """BICM capacity (bits/symbol) of Gray 16QAM at Es/N0 per 16QAM symbol, Monte Carlo."""
    g = 10 ** (esn0_db / 10)
    pam = np.array([-3, -1, 1, 3]) / np.sqrt(5)      # per-dimension energy 1/2 -> symbol energy 1
    pam = pam / np.sqrt(2)
    bits = np.array([[0, 0], [0, 1], [1, 1], [1, 0]])
    sig2 = 1 / (2 * g)                               # noise variance per dimension, Es = 1
    idx = rng.integers(0, 4, n)
    y = pam[idx] + np.sqrt(sig2) * rng.standard_normal(n)
    m = -(y[:, None] - pam[None, :]) ** 2 / (2 * sig2)
    tot = 0.0
    for b in range(2):
        l1 = np.logaddexp.reduce(m[:, bits[:, b] == 1], axis=1)
        l0 = np.logaddexp.reduce(m[:, bits[:, b] == 0], axis=1)
        llr = np.where(bits[idx, b] == 1, l1 - l0, l0 - l1)
        tot += 1 - np.mean(np.log2(1 + np.exp(-llr)))
    return 2 * tot

def need_qam16(K, N):
    target = 4 * (K / N) + 4 * 0.012           # small finite-length allowance (n = 16200)
    lo, hi = 0.0, 20.0
    for _ in range(30):
        mid = (lo + hi) / 2
        if qam16_bicm(mid) < target: lo = mid
        else: hi = mid
    return hi

# mode: nc, np, Tu, Tcp, bps, N, K, measured stock 90% SNR3k, measured with #9
modes = [
 ("DATAC16", 3, 28, .016, .006, 2,   640,  128, -11.0, -11.6),
 ("DATAC15", 3, 34, .016, .006, 2,   768,  256, -10.0, -10.6),
 ("DATAC3",  9, 29, .016, .006, 2,  2048, 1024,  -3.2,  -4.1),
 ("DATAC1", 27, 38, .016, .006, 2,  8192, 4096,   1.1,   0.0),
 ("DATAC17",33, 61, .016, .006, 2, 15936, 9456,   3.7,   1.9),
 ("QAM16C2",33, 31, .016, .004, 4, 16200, 9720,   8.6,   6.9),
]
print("mode      stock   #9    idealRX  idealRX+CP  Shannon   |  left after #9 (no wire change)   structural")
for name, nc, npk, Tu, Tcp, bps, N, K, a0, a9 in modes:
    Ts = Tu + Tcp
    off = 10 * np.log10(3000 * Tu / nc)          # Es/N0 (useful part) = SNR3k + off
    if bps == 2:
        es = need_bpsk(K, N) + 3.0103            # per QPSK symbol
    else:
        es = need_qam16(K, N)
    ideal = es - off                             # SNR3k an ideal receiver of the same waveform needs
    ideal_cp = ideal - 10 * np.log10(Ts / Tu)    # ...using the cyclic prefix energy too
    dur = npk * 5 * Ts
    Rb = K / dur
    W = nc / Tu
    shannon = 10 * np.log10((2 ** (Rb / W) - 1) * W / 3000)
    print(f"{name:8s} {a0:6.1f} {a9:6.1f}  {ideal:7.2f}   {ideal_cp:7.2f}   {shannon:7.2f}   |   {a9-ideal_cp:5.2f} dB                      {ideal_cp-shannon:5.2f} dB")
