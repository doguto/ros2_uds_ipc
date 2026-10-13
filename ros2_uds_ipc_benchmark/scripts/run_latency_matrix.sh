#!/usr/bin/env bash
# Runs latency.launch.py over CPU load x transport x publish rate x payload size and collects the results into one CSV file.
# The dds transport is measured once per RMW implementation.
#
# Usage: ros2 run ros2_uds_ipc_benchmark run_latency_matrix.sh [OUTPUT_CSV]
# Environment:
#   SIZES  Payload sizes in bytes
#   RATES  Publish rates in Hz, 0 for no throttling
#   TRANSPORTS  UDS transports to measure (default: uds_reliable uds_reliable_fixed; also: uds)
#   COUNT  Number of measured messages per run (default: 10 s worth at the rate, at most 10000)
#   LOADS  Numbers of busy-loop threads (default: idle and all cores)
#   REPEAT How many times to run the whole matrix (default: 1). plot_latency.py takes the median per condition
#   RMWS   RMW implementations to measure (default: every installed one out of KNOWN_RMWS)
set -u

# rmw_zenoh_cpp is not in the default list because it needs a running router (ros2 run rmw_zenoh_cpp rmw_zenohd).
KNOWN_RMWS="rmw_fastrtps_cpp rmw_cyclonedds_cpp rmw_connextdds"

OUTPUT=$(realpath -m "${1:-latency_result.csv}")
SIZES=${SIZES:-"64 256 1024 4096 16384 65536"}
RATES=${RATES:-"100 1000 10000 0"}
LOADS=${LOADS:-"0 $(nproc)"}
REPEAT=${REPEAT:-1}
TRANSPORTS=${TRANSPORTS:-"uds_reliable uds_reliable_fixed"}

if [ -z "${RMWS:-}" ]; then
  RMWS=""
  for rmw in $KNOWN_RMWS; do
    if ros2 pkg prefix "$rmw" > /dev/null 2>&1; then
      RMWS="$RMWS $rmw"
    fi
  done
fi
echo "RMW implementations: $RMWS" >&2

run() {  # run LOAD TRANSPORT RATE SIZE
  local count=${COUNT:-10000}
  if [ -z "${COUNT:-}" ] && [ "$3" -gt 0 ] && [ $(($3 * 10)) -lt "$count" ]; then
    count=$(($3 * 10))
  fi
  if ! ros2 launch ros2_uds_ipc_benchmark latency.launch.py \
    transport:="$2" payload_bytes:="$4" count:="$count" rate_hz:="$3.0" load_threads:="$1" \
    output_file:="$OUTPUT"; then
    echo "latency.launch.py failed: load_threads=$1 transport=$2 rate=$3 size=$4 rmw=${RMW_IMPLEMENTATION:-default}" >&2
  fi
}

for _ in $(seq "$REPEAT"); do
for load in $LOADS; do
  for transport in $TRANSPORTS; do
    for rate in $RATES; do
      for size in $SIZES; do
        run "$load" "$transport" "$rate" "$size"
      done
    done
  done
  for rmw in $RMWS; do
    if ! ros2 pkg prefix "$rmw" > /dev/null 2>&1; then
      echo "Skipping $rmw: not installed" >&2
      continue
    fi
    for rate in $RATES; do
      for size in $SIZES; do
        RMW_IMPLEMENTATION="$rmw" run "$load" dds "$rate" "$size"
      done
    done
  done
done
done

echo "Results written to $OUTPUT" >&2
