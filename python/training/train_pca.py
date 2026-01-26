import numpy as np
import pandas as pd
from sklearn.decomposition import PCA
from sklearn.preprocessing import StandardScaler
from export_utils import export_pca_artifact

# Feature Contract V1
FEATURES = [
    "cpu_usage", "memory_usage", "disk_utilization", 
    "network_rx_rate", "network_tx_rate"
]

def load_training_data(db_url=None):
    """
    Mocks loading training data from DB. 
    In prod, this uses sqlalchemy to read 'host_telemetry_archival' where is_anomaly=False.
    """
    if db_url is None:
        print("No DB URL, using synthetic training data...")
        # Synthetic Normal Data
        # 5 Features
        n_samples = 5000
        
        # 0: CPU ~ N(40, 10)
        # 1: Mem ~ 0.8*CPU + N(0, 5)
        # 2: Disk ~ N(30, 2)
        # 3: NetRx ~ N(50, 10)
        # 4: NetTx ~ 0.5*NetRx + N(0, 2)
        
        X = np.zeros((n_samples, 5))
        rng = np.random.default_rng(42)
        
        X[:, 0] = rng.normal(40, 10, n_samples)
        X[:, 1] = X[:, 0] * 0.8 + rng.normal(0, 5, n_samples)
        X[:, 2] = rng.normal(30, 2, n_samples)
        X[:, 3] = rng.normal(50, 10, n_samples)
        X[:, 4] = X[:, 3] * 0.5 + rng.normal(0, 2, n_samples)
        
        # Clamp to mimic preprocessing
        X[X < 0] = 0
        
        return pd.DataFrame(X, columns=FEATURES)
    else:
        # TODO: Implement DB loading via pandas.read_sql
        pass

def train_and_export(output_dir="artifacts/pca/default"):
    # 1. Load Data
    df = load_training_data()
    print(f"Loaded {len(df)} samples.")

    # 2. Preprocess
    # Log1p check would happen here if configured, assuming raw for now
    
    # Standardize
    scaler = StandardScaler()
    X_scaled = scaler.fit_transform(df[FEATURES].values)
    
    # 3. Train PCA
    # Keep 95% variance or fixed components. Let's fix k=3 for dimensionality reduction (5->3)
    pca = PCA(n_components=3)
    X_pca = pca.fit_transform(X_scaled)
    
    # 4. Compute Threshold (Reconstruction Error)
    X_recon = pca.inverse_transform(X_pca)
    errors = np.linalg.norm(X_scaled - X_recon, axis=1)
    threshold = np.percentile(errors, 99.5) # p99.5
    
    print(f"Training complete. p99.5 Threshold: {threshold:.4f}")
    
    # 5. Export
    export_pca_artifact(pca, scaler, FEATURES, threshold, f"{output_dir}/model.json")

if __name__ == "__main__":
    train_and_export()
