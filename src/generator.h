#include "types.h"
#include "idb_client.h"
#include "telemetry.grpc.pb.h"
#include <atomic>
#include <memory>

class Generator {
public:
    Generator(const telemetry::GenerateRequest& request, 
              std::string run_id, 
              std::shared_ptr<IDbClient> db_client);

    void Run();

protected:
    telemetry::GenerateRequest config_;
    std::string run_id_;
    std::shared_ptr<IDbClient> db_;

    
    std::vector<HostProfile> hosts_;
    
    void InitializeHosts();
    TelemetryRecord GenerateRecord(const HostProfile& host, 
                                   std::chrono::system_clock::time_point timestamp);
};

