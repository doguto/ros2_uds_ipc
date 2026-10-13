#!/usr/bin/env bash
# Builds the benchmark package, runs the full latency matrix for UDS and every RMW (Fast DDS, Cyclone DDS, Zenoh)
# and renders the charts and a Markdown report (benchmark.md) in the style of docs/benchmark.md. The Zenoh router is started and stopped by this script.
#
# Usage: scripts/run_all_benchmarks.sh [OUTPUT_DIR]
# Environment (all optional, passed through to run_latency_matrix.sh):
#   REPEAT  Number of repetitions of the whole matrix (default: 5)
#   (Defaults reproduce the docs/benchmark.md setup: 6 sizes x 4 rates x idle / all cores, 5 runs each)
#   RMWS    RMW implementations (default: fastrtps, cyclonedds, zenoh)
#   SIZES / RATES / LOADS / COUNT  See run_latency_matrix.sh
#   SKIP_BUILD=1  Skip colcon build
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(realpath -m "${1:-$ROOT/benchmark_result/$(date +%Y%m%d_%H%M%S)}")
mkdir -p "$OUT"

# ROS setup scripts reference unset variables
set +u
source /opt/ros/${ROS_DISTRO:-jazzy}/setup.bash
cd "$ROOT"

if [ -z "${SKIP_BUILD:-}" ]; then
  colcon build --packages-up-to ros2_uds_ipc_benchmark --cmake-args -DCMAKE_BUILD_TYPE=Release
fi
source "$ROOT/install/setup.bash"
set -u

export REPEAT=${REPEAT:-5}
RMWS=${RMWS:-"rmw_fastrtps_cpp rmw_cyclonedds_cpp rmw_zenoh_cpp"}

# Keep only installed RMWs
available=""
for rmw in $RMWS; do
  if ros2 pkg prefix "$rmw" > /dev/null 2>&1; then
    available="$available $rmw"
  else
    echo "Skipping $rmw: not installed (sudo apt install ros-${ROS_DISTRO:-jazzy}-${rmw//_/-})" >&2
  fi
done
export RMWS=${available# }
echo "RMWs: $RMWS / REPEAT=$REPEAT / output: $OUT" >&2

# Zenoh needs a router
ROUTER_PID=""
cleanup() { [ -n "$ROUTER_PID" ] && kill "$ROUTER_PID" 2> /dev/null || true; }
trap cleanup EXIT
case " $RMWS " in
  *" rmw_zenoh_cpp "*)
    RMW_IMPLEMENTATION=rmw_zenoh_cpp ros2 run rmw_zenoh_cpp rmw_zenohd > "$OUT/zenohd.log" 2>&1 &
    ROUTER_PID=$!
    sleep 3
    ;;
esac

CSV="$OUT/result.csv"
ros2 run ros2_uds_ipc_benchmark run_latency_matrix.sh "$CSV" 2>&1 | tee "$OUT/run.log"

ros2 run ros2_uds_ipc_benchmark plot_latency.py "$CSV" "$OUT/charts/"

CPU_MODEL=$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ *//' || true)
ros2 run ros2_uds_ipc_benchmark report_latency.py "$CSV" "$OUT/benchmark.md" --charts-dir charts \
  --environment "${CPU_MODEL:-unknown CPU} ($(nproc) threads), Linux $(uname -r), ROS 2 ${ROS_DISTRO:-jazzy}, RMWs: ${RMWS}, ${REPEAT} runs per condition, without CPU pinning or real-time scheduling."

echo "Done: $OUT" >&2
