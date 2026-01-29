#include <iostream>
#include <string>
#include <grpcpp/grpcpp.h>
#include "telemetry.grpc.pb.h"
#include <thread>
#include <chrono>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using telemetry::TelemetryService;
using telemetry::GenerateRequest;
using telemetry::GenerateResponse;
using telemetry::GetRunRequest;
using telemetry::RunStatus;

class TelemetryClient {
public:
    TelemetryClient(std::shared_ptr<Channel> channel)
        : stub_(TelemetryService::NewStub(channel)) {}

    std::string Generate(const std::string& tier, int host_count) {
        GenerateRequest request;
        request.set_tier(tier);
        request.set_host_count(host_count);
        request.set_interval_seconds(10); // fast for testing
        // Set short duration for test: now to now + 30s
        auto now = std::chrono::system_clock::now();
        // naive iso string
        request.set_start_time_iso("2025-01-01T00:00:00Z");
        request.set_end_time_iso("2025-01-01T00:01:00Z"); // 60 seconds -> 6 points
        request.set_seed(999);
        
        GenerateResponse response;
        ClientContext context;
        
        Status status = stub_->GenerateTelemetry(&context, request, &response);
        if (status.ok()) {
            return response.run_id();
        } else {
            std::cout << "RPC failed: " << status.error_code() << ": " << status.error_message() << std::endl;
            return "";
        }
    }
    
    void GetRun(const std::string& id) {
        GetRunRequest request;
        request.set_run_id(id);
        RunStatus response;
        ClientContext context;
        Status status = stub_->GetRun(&context, request, &response);
        if(status.ok()) {
            std::cout << "Run Status: " << response.status() << " Rows: " << response.inserted_rows() << std::endl;
        }
    }

private:
    std::unique_ptr<TelemetryService::Stub> stub_;
};

int main(int argc, char** argv) {
    TelemetryClient client(grpc::CreateChannel("localhost:52051", grpc::InsecureChannelCredentials()));
    std::string run_id = client.Generate("TEST", 5);
    std::cout << "Started Run ID: " << run_id << std::endl;
    
    if (run_id.empty()) return 1;
    
    // Poll for a bit
    for(int i=0; i<5; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        client.GetRun(run_id);
    }
    return 0;
}
