import json
import numpy as np
import os

def check_contract(feature_names):
    expected = [
        "cpu_usage", "memory_usage", "disk_utilization", 
        "network_rx_rate", "network_tx_rate"
    ]
    if feature_names != expected:
        raise ValueError(f"Feature contract mismatch! Expected {expected}, got {feature_names}")

def export_pca_artifact(pca_model, scaler, feature_names, threshold, output_path):
    """
    Exports Sklearn PCA + StandardScaler to a JSON artifact.
    """
    check_contract(feature_names)
    
    artifact = {
        "meta": {
            "version": "v1",
            "type": "pca_reconstruction",
            "features": feature_names
        },
        "preprocessing": {
            "mean": scaler.mean_.tolist(),
            "scale": scaler.scale_.tolist()
        },
        "model": {
            "components": pca_model.components_.tolist(),
            "explained_variance": pca_model.explained_variance_.tolist(),
            "mean": pca_model.mean_.tolist(), # PCA also centers if not using StandardScaler, but we stick to standard pipeline
            "n_components": int(pca_model.n_components_)
        },
        "thresholds": {
            "reconstruction_error": threshold
        }
    }
    
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, "w") as f:
        json.dump(artifact, f, indent=4)
        
    print(f"Artifact exported to {output_path}")
