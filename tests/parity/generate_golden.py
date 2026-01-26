import json
import numpy as np
import os
import sys

# Load the JSON directly to rely on the artifact interface.

ARTIFACT_PATH = "artifacts/pca/default/model.json"
OUTPUT_DIR = "tests/parity/golden"

def main():
    if not os.path.exists(ARTIFACT_PATH):
        print(f"Artifact not found at {ARTIFACT_PATH}. Run training first.")
        sys.exit(1)

    with open(ARTIFACT_PATH, "r") as f:
        model = json.load(f)

    # Extract params
    mean = np.array(model["preprocessing"]["mean"])
    scale = np.array(model["preprocessing"]["scale"])
    components = np.array(model["model"]["components"])
    pca_mean = np.array(model["model"]["mean"])
    threshold = model["thresholds"]["reconstruction_error"]

    print("Loaded artifact parameters.")

    # Generate random vectors
    rng = np.random.default_rng(12345)
    n_samples = 50
    # Random values covering typical ranges
    X = np.zeros((n_samples, 5))
    X[:, 0] = rng.uniform(0, 100, n_samples) # CPU
    X[:, 1] = rng.uniform(0, 100, n_samples) # Mem
    X[:, 2] = rng.uniform(0, 100, n_samples) # Disk
    X[:, 3] = rng.uniform(0, 1000, n_samples) # Rx
    X[:, 4] = rng.uniform(0, 1000, n_samples) # Tx
    
    # Logic duplication for parity verification
    # 1. Standardize
    X_scaled = (X - mean) / scale
    
    # 2. PCA Project
    X_centered = X_scaled - pca_mean
    X_proj = X_centered @ components.T
    
    # 3. Reconstruct
    X_recon_centered = X_proj @ components
    X_recon_scaled = X_recon_centered + pca_mean
    
    # 4. Residual
    diff = X_scaled - X_recon_scaled
    errors = np.linalg.norm(diff, axis=1)

    # Save
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    
    golden_data = {
        "samples": []
    }
    
    for i in range(n_samples):
        item = {
            "input": X[i].tolist(),
            "expected_error": errors[i],
            "is_anomaly": bool(errors[i] > threshold)
        }
        golden_data["samples"].append(item)

    with open(f"{OUTPUT_DIR}/parity_b.json", "w") as f:
        json.dump(golden_data, f, indent=4)
        
    print(f"Generated {n_samples} golden samples to {OUTPUT_DIR}/parity_b.json")

if __name__ == "__main__":
    main()
