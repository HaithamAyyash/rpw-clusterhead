// What the cluster head sends to the cloud. These fields mirror the rpw-dashboard repo, docs/contracts/cluster_head_to_cloud.md.
#pragma once

#include <cstdint>

namespace nakhel {

constexpr int kMfccFrames = 30;
constexpr int kMfccCoeffs = 13;

// Together, node_id and timestamp identify a measurement on the server. Set both once when the
// packet arrives and keep them through retries, or the server would store it twice.
struct Measurement {
  uint8_t nodeId = 0;
  int64_t timestampS = 0;  // when the node recorded the sample, Unix seconds UTC
  float mfcc[kMfccFrames][kMfccCoeffs] = {};

  // Optional. Leave has* false, or receivedAtS 0, to omit a field.
  int64_t receivedAtS = 0;
  bool hasSnr = false;
  float snrDb = 0;
  bool hasRssi = false;
  int16_t rssiDbm = 0;
};

struct NodeHealth {
  uint8_t nodeId = 0;
  int64_t timestampS = 0;
  bool hasBattery = false;
  uint32_t batteryMv = 0;
  bool hasRssi = false;
  int16_t rssiDbm = 0;
  bool hasUptime = false;
  uint32_t uptimeS = 0;
};

// One entry in the uplink queue.
struct UplinkRecord {
  enum class Kind : uint8_t { Measurement, NodeHealth };
  Kind kind = Kind::Measurement;
  Measurement measurement;  // valid when kind == Measurement
  NodeHealth nodeHealth;    // valid when kind == NodeHealth
};

// The cluster head's own status, read fresh each time it is sent.
struct ClusterHealth {
  int64_t timestampS = 0;
  uint32_t uptimeS = 0;
  bool hasRssi = false;
  int16_t rssiDbm = 0;
  uint32_t bufferedCount = 0;
};

}  // namespace nakhel
