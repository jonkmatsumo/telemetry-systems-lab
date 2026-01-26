#include <cstdlib>
#include <iostream>
#include <string>

#include "training/pca_trainer.h"

static std::string get_env(const char* key) {
    const char* val = std::getenv(key);
    return val ? std::string(val) : std::string();
}

int main(int argc, char** argv) {
    std::string dataset_id;
    std::string output_dir = "artifacts/pca/default";
    std::string db_conn_str;
    int n_components = 3;
    double percentile = 99.5;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--dataset_id" && i + 1 < argc) {
            dataset_id = argv[++i];
        } else if (arg == "--output_dir" && i + 1 < argc) {
            output_dir = argv[++i];
        } else if (arg == "--db_conn" && i + 1 < argc) {
            db_conn_str = argv[++i];
        } else if (arg == "--n_components" && i + 1 < argc) {
            n_components = std::stoi(argv[++i]);
        } else if (arg == "--percentile" && i + 1 < argc) {
            percentile = std::stod(argv[++i]);
        }
    }

    if (db_conn_str.empty()) {
        db_conn_str = get_env("DATABASE_URL");
    }

    if (db_conn_str.empty()) {
        std::cerr << "Missing DB connection string (use --db_conn or DATABASE_URL)." << std::endl;
        return 1;
    }

    if (dataset_id.empty()) {
        std::cerr << "Missing --dataset_id." << std::endl;
        return 1;
    }

    std::string output_path = output_dir + "/model.json";

    try {
        auto artifact = telemetry::training::TrainPcaFromDb(db_conn_str, dataset_id, n_components, percentile);
        telemetry::training::WriteArtifactJson(artifact, output_path);
    } catch (const std::exception& e) {
        std::cerr << "Training failed: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
