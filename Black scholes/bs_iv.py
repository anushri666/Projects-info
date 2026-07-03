"""
Black-Scholes Pricing to Implied Volatility Inversion
Author: Anushka Shrivastava
Description: Newton-Raphson IV solver, volatility surface grid,
             skew quantification across regimes
"""

import numpy as np
from scipy.stats import norm
import time
import csv

# -----------------------------------------------------------------------
# 1. BLACK-SCHOLES PRICING ENGINE
# -----------------------------------------------------------------------

def black_scholes(S, K, T, r, sigma, option_type='call'):
    """
    S     : Spot price
    K     : Strike price
    T     : Time to expiry (years)
    r     : Risk-free rate
    sigma : Volatility
    """
    if T <= 0 or sigma <= 0:
        return max(S - K, 0) if option_type == 'call' else max(K - S, 0)

    d1 = (np.log(S / K) + (r + 0.5 * sigma**2) * T) / (sigma * np.sqrt(T))
    d2 = d1 - sigma * np.sqrt(T)

    if option_type == 'call':
        price = S * norm.cdf(d1) - K * np.exp(-r * T) * norm.cdf(d2)
    else:
        price = K * np.exp(-r * T) * norm.cdf(-d2) - S * norm.cdf(-d1)
    return price

def vega(S, K, T, r, sigma):
    """Partial derivative of BS price w.r.t. sigma"""
    if T <= 0 or sigma <= 0:
        return 1e-10
    d1 = (np.log(S / K) + (r + 0.5 * sigma**2) * T) / (sigma * np.sqrt(T))
    return S * norm.pdf(d1) * np.sqrt(T)

# -----------------------------------------------------------------------
# 2. NEWTON-RAPHSON IMPLIED VOLATILITY SOLVER
# -----------------------------------------------------------------------

def implied_volatility_NR(market_price, S, K, T, r,
                           option_type='call',
                           tol=1e-8, max_iter=100):
    """
    Inverts BS formula via Newton-Raphson to find implied vol.
    Returns (iv, iterations, converged)
    """
    # Initial guess: Brenner-Subrahmanyam approximation
    sigma = np.sqrt(2 * np.pi / T) * (market_price / S)
    sigma = np.clip(sigma, 1e-4, 5.0)

    for i in range(1, max_iter + 1):
        price  = black_scholes(S, K, T, r, sigma, option_type)
        v      = vega(S, K, T, r, sigma)
        diff   = price - market_price

        if abs(diff) < tol:
            return sigma, i, True

        if abs(v) < 1e-10:
            break

        sigma -= diff / v
        sigma  = np.clip(sigma, 1e-4, 5.0)

    return sigma, max_iter, False

# -----------------------------------------------------------------------
# 3. VOLATILITY SURFACE GRID
# -----------------------------------------------------------------------

def build_vol_surface(S, r, strikes, expiries, regime='normal'):
    """
    Constructs implied vol surface across strikes x expiries.
    Regime: 'normal', 'bull', 'bear'
    """
    surface = np.zeros((len(expiries), len(strikes)))
    iterations_grid = np.zeros((len(expiries), len(strikes)), dtype=int)
    errors = []

    for i, T in enumerate(expiries):
        for j, K in enumerate(strikes):
            # Simulate market vol with smile/skew
            moneyness = np.log(K / S)

            if regime == 'normal':
                true_vol = 0.20 + 0.05 * moneyness**2 - 0.02 * moneyness
            elif regime == 'bull':
                true_vol = 0.15 + 0.03 * moneyness**2 - 0.01 * moneyness
            else:  # bear
                true_vol = 0.30 + 0.08 * moneyness**2 - 0.05 * moneyness

            # Add term structure: vol increases with T
            true_vol += 0.01 * np.sqrt(T)
            true_vol  = max(true_vol, 0.05)

            # Market price from true vol
            market_price = black_scholes(S, K, T, r, true_vol, 'call')
            market_price = max(market_price, 1e-6)

            # Invert to get IV
            iv, iters, converged = implied_volatility_NR(
                market_price, S, K, T, r, 'call')

            surface[i][j] = iv
            iterations_grid[i][j] = iters
            errors.append(abs(iv - true_vol))

    return surface, iterations_grid, errors

# -----------------------------------------------------------------------
# 4. SKEW ANALYSIS
# -----------------------------------------------------------------------

def analyze_skew(surface, strikes, S):
    """
    Quantifies skew: difference between OTM put vol and ATM vol
    Returns skew per expiry row
    """
    skews = []
    atm_idx = np.argmin(np.abs(np.array(strikes) - S))
    for row in surface:
        atm_vol = row[atm_idx]
        otm_put_vol = row[0]   # lowest strike = deepest OTM put
        skews.append(otm_put_vol - atm_vol)
    return skews

def detect_bs_deviations(surface_normal, surface_bull, surface_bear,
                          strikes, S, threshold=0.02):
    """
    Identifies regimes where vol deviates significantly from BS flat-vol assumption
    """
    deviations = 0
    atm_idx = np.argmin(np.abs(np.array(strikes) - S))
    for surf in [surface_normal, surface_bull, surface_bear]:
        for row in surf:
            atm = row[atm_idx]
            for j, iv in enumerate(row):
                if abs(iv - atm) > threshold:
                    deviations += 1
    return deviations

# -----------------------------------------------------------------------
# 5. MAIN SIMULATION
# -----------------------------------------------------------------------

def main():
    print("=" * 55)
    print("  Black-Scholes IV Inversion Engine")
    print("=" * 55)

    # Parameters
    S = 100.0   # Spot
    r = 0.05    # Risk-free rate

    # 12 strikes x 6 expiries = 72 strike-expiry pairs per regime
    strikes  = np.round(np.arange(80, 125, 4), 2)   # 80 to 120, step 4 → 11 strikes
    expiries = [1/12, 2/12, 3/12, 6/12, 9/12, 1.0]  # 1M to 12M

    print(f"\nGrid: {len(strikes)} strikes x {len(expiries)} expiries "
          f"= {len(strikes)*len(expiries)} pairs per regime")
    print(f"Strikes : {strikes}")
    print(f"Expiries: {[f'{int(T*12)}M' for T in expiries]}\n")

    # Build surfaces for 3 regimes
    start = time.time()

    surf_normal, iters_normal, err_normal = build_vol_surface(
        S, r, strikes, expiries, 'normal')
    surf_bull,   iters_bull,   err_bull   = build_vol_surface(
        S, r, strikes, expiries, 'bull')
    surf_bear,   iters_bear,   err_bear   = build_vol_surface(
        S, r, strikes, expiries, 'bear')

    elapsed = time.time() - start

    all_iters  = np.concatenate([iters_normal.flatten(),
                                  iters_bull.flatten(),
                                  iters_bear.flatten()])
    all_errors = err_normal + err_bull + err_bear
    total_pairs = len(strikes) * len(expiries) * 3

    # Skew analysis
    skew_normal = analyze_skew(surf_normal, strikes, S)
    skew_bull   = analyze_skew(surf_bull,   strikes, S)
    skew_bear   = analyze_skew(surf_bear,   strikes, S)

    # BS deviation detection
    deviations = detect_bs_deviations(
        surf_normal, surf_bull, surf_bear, strikes, S, threshold=0.02)

    # -----------------------------------------------------------------------
    # 6. RESULTS
    # -----------------------------------------------------------------------
    print("=" * 55)
    print("  SIMULATION RESULTS")
    print("=" * 55)
    print(f"Total strike-expiry pairs solved : {total_pairs}")
    print(f"Total wall-clock runtime         : {elapsed:.4f}s")
    print(f"Avg IV inversions per pair       : {np.mean(all_iters):.2f} iterations")
    print(f"Max iterations needed            : {np.max(all_iters)}")
    print(f"Min iterations needed            : {np.min(all_iters)}")
    print(f"Avg numerical error (|IV-true|)  : {np.mean(all_errors)*100:.6f}%")
    print(f"Max numerical error              : {np.max(all_errors)*100:.6f}%")
    print(f"BS assumption deviations (>2%)   : {deviations} out of {total_pairs}")

    print("\n--- VOLATILITY SURFACE (Normal Regime) ---")
    header = "T\\K  " + "  ".join([f"{k:6.1f}" for k in strikes])
    print(header)
    for i, T in enumerate(expiries):
        row = f"{int(T*12)}M   " + "  ".join(
            [f"{surf_normal[i][j]*100:5.2f}%" for j in range(len(strikes))])
        print(row)

    print("\n--- SKEW ANALYSIS (OTM Put Vol - ATM Vol) ---")
    print(f"{'Expiry':<8} {'Normal':>10} {'Bull':>10} {'Bear':>10}")
    for i, T in enumerate(expiries):
        print(f"{int(T*12)}M      "
              f"{skew_normal[i]*100:>8.2f}%  "
              f"{skew_bull[i]*100:>8.2f}%  "
              f"{skew_bear[i]*100:>8.2f}%")

    # Export CSV
    with open('iv_surface.csv', 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['regime','expiry_months','strike','implied_vol','iterations'])
        for regime, surf, iters in [('normal', surf_normal, iters_normal),
                                     ('bull',   surf_bull,   iters_bull),
                                     ('bear',   surf_bear,   iters_bear)]:
            for i, T in enumerate(expiries):
                for j, K in enumerate(strikes):
                    writer.writerow([regime, int(T*12), K,
                                     round(surf[i][j], 6), iters[i][j]])
    print("\nSurface exported to iv_surface.csv")
    print("=" * 55)

if __name__ == '__main__':
    main()
