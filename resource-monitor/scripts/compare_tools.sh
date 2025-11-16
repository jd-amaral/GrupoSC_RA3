#!/usr/bin/env bash
set -euo pipefail
# compare_tools.sh - consolidated experiment runner
# Usage:
#  ./compare_tools.sh            # runs Experiment 1 (overhead) with default duration 5s
#  ./compare_tools.sh 10         # runs Experiment 1 with duration 10s
#  ./compare_tools.sh exp2       # runs Experiment 2 (namespace isolation)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd -P)"
OUTDIR="$ROOT_DIR/out/experiments"
PLOTS_DIR="$OUTDIR/plots"
mkdir -p "$OUTDIR" "$PLOTS_DIR"

MODE="${1:-}" 
# If first arg is numeric, treat as duration for exp1
if [ -z "$MODE" ]; then
  MODE="exp1"
  DURATION=5
elif [ "$MODE" = "exp2" ]; then
  : # keep MODE
elif [[ "$MODE" =~ ^[0-9]+$ ]]; then
  MODE="exp1"
  DURATION="$MODE"
else
  # allow: ./compare_tools.sh exp1 10
  if [ "$MODE" = "exp1" ]; then
    DURATION="${2:-5}"
  fi
fi

# Ensure DURATION is always defined (avoid unbound variable when running under sudo)
: ${DURATION:=5}

WORKLOAD_SRC="$ROOT_DIR/tests/bench_cpu.c"
WORKLOAD_BIN="$ROOT_DIR/tests/bench_cpu"
MONITOR_BIN="$ROOT_DIR/resource_monitor"

# If no bench_cpu.c in tests, create a temporary CPU-bound workload in /tmp
# If no bench_cpu.c in tests, create a temporary CPU-bound workload in repo tmp/
ensure_workload() {
  if [ -f "$WORKLOAD_SRC" ]; then
    return 0
  fi
  TMPDIR="$ROOT_DIR/tmp"
  mkdir -p "$TMPDIR"
  TEMP_SRC="$TMPDIR/bench_cpu_tmp.c"
  TEMP_BIN="$TMPDIR/bench_cpu_tmp"
  cat > "$TEMP_SRC" <<'CEOF'
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>
int main(int argc, char **argv) {
    int duration = argc>1 ? atoi(argv[1]) : 5;
    volatile uint64_t counter = 0;
    time_t end = time(NULL) + duration;
    while (time(NULL) < end) {
        // busy loop doing small work
        for (int i=0;i<1000;i++) counter += i;
    }
    printf("ITERATIONS:%llu\n", (unsigned long long)counter);
    return 0;
}
CEOF
  gcc -O2 -o "$TEMP_BIN" "$TEMP_SRC" -lm || return 1
  WORKLOAD_BIN="$TEMP_BIN"
  return 0
}

# Ensure memory workload (allocating until OOM or stop)
ensure_mem_workload() {
  TMPDIR="$ROOT_DIR/tmp"
  mkdir -p "$TMPDIR"
  MEM_SRC="$TMPDIR/mem_alloc.c"
  MEM_BIN="$TMPDIR/mem_alloc"
  if [ -f "$MEM_BIN" ]; then
    return 0
  fi
  cat > "$MEM_SRC" <<'CEOF'
  #include <stdio.h>
  #include <stdlib.h>
  #include <string.h>
  #include <time.h>
  #include <unistd.h>
  #include <sys/types.h>
    EXP1_DIR="$OUTDIR/experiment1"
    mkdir -p "$EXP1_DIR"

    echo "[exp1] Running overhead experiment (duration=${DURATION}s) -> $EXP1_DIR"
    # compile or ensure workload

      gcc -O2 -o "$WORKLOAD_BIN" "$WORKLOAD_SRC" -lm || { echo "Failed to compile workload" >&2; return 1; }
    else
      ensure_workload || { echo "Failed to create temporary workload" >&2; return 1; }
    fi

    SUMMARY="$EXP1_DIR/overhead_summary.csv"
    echo "mode,interval,run,elapsed_sec,percent_cpu" > "$SUMMARY"

    INTERVALS=(1 0.5 0.2)
    for interval in "${INTERVALS[@]}"; do
      for run in 1 2 3; do
        echo "[exp1] baseline interval=$interval run=$run"
        /usr/bin/time -v -o "$EXP1_DIR/baseline_${interval}_${run}.time" "$WORKLOAD_BIN" "$DURATION" || true
        elapsed_line=$(grep -E "Elapsed |Elapsed \(wall clock\)" -m1 "$EXP1_DIR/baseline_${interval}_${run}.time" || true)
        elapsed_val=$(echo "$elapsed_line" | sed -E 's/.*: (.*)/\1/')
        if [ -z "$elapsed_val" ]; then elapsed_sec=0; else elapsed_sec=$(echo "$elapsed_val" | awk -F: '{ if (NF==3) printf("%.3f", $1*3600 + $2*60 + $3); else if (NF==2) printf("%.3f", $1*60 + $2); else printf("%.3f", $1) }'); fi
        pct_val=$(grep "Percent of CPU" -m1 "$EXP1_DIR/baseline_${interval}_${run}.time" | sed -E 's/[^0-9]*([0-9]+\.?[0-9]*).*/\1/' || true)
        echo "baseline,$interval,$run,$elapsed_sec,$pct_val" >> "$SUMMARY"

        echo "[exp1] monitored interval=$interval run=$run"
        "$WORKLOAD_BIN" "$DURATION" &
        wl_pid=$!
        sleep 0.2
        if [ -x "$MONITOR_BIN" ]; then
          "$MONITOR_BIN" "$wl_pid" "$EXP1_DIR/metrics_${interval}_${run}.csv" "$interval" &
          mon_pid=$!
        else
          mon_pid=""
        fi
        wait $wl_pid || true
        sleep 0.5
        if [ -n "${mon_pid}" ] && kill -0 "$mon_pid" 2>/dev/null; then kill "$mon_pid" || true; fi

        if [ -f "$EXP1_DIR/metrics_${interval}_${run}.csv" ]; then
          first_ts=$(awk -F, 'NR==2{print $1; exit}' "$EXP1_DIR/metrics_${interval}_${run}.csv")
          last_ts=$(awk -F, 'END{print $1}' "$EXP1_DIR/metrics_${interval}_${run}.csv")
          if [ -n "$first_ts" ] && [ -n "$last_ts" ]; then
            elapsed_mon=$(awk "BEGIN{ if(\"$first_ts\"==\"\" || \"$last_ts\"==\"\") print 0; else printf(\"%.6f\", ($last_ts) - ($first_ts)) }")
          else
            elapsed_mon=0
          fi
          avg_cpu=$(awk -F, 'NR>1{sum+=$3; count++} END{if(count>0) printf("%.2f", sum/count); else print "0"}' "$EXP1_DIR/metrics_${interval}_${run}.csv")
        else
          elapsed_mon=0
          avg_cpu=0
        fi
        echo "monitored,$interval,$run,$elapsed_mon,$avg_cpu" >> "$SUMMARY"
      done
    done

    echo "[exp1] Done. Summary: $SUMMARY"
ensure_io_workload() {
  TMPDIR="$ROOT_DIR/tmp"
  mkdir -p "$TMPDIR"
  IO_SRC="$TMPDIR/io_workload.c"
  IO_BIN="$TMPDIR/io_workload"
  if [ -f "$IO_BIN" ]; then
    return 0
  fi
  cat > "$IO_SRC" <<'CEOF'
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
int main(int argc, char **argv) {
    const char *path = argc>1 ? argv[1] : "/tmp/io_work.out";
    int duration = argc>2 ? atoi(argv[2]) : 5;
    size_t block = argc>3 ? (size_t)atoi(argv[3]) : 65536; // 64KB
    char *buf = malloc(block);
    if (!buf) return 2;
    memset(buf, 'A', block);
    int fd = open(path, O_CREAT|O_TRUNC|O_WRONLY, 0644);
    if (fd<0) { perror("open"); return 2; }
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    long total = 0;
    long writes = 0;
    while (1) {
        if (write(fd, buf, block) != (ssize_t)block) break;
        total += block; writes++;
        if (writes % 10 == 0) {
            fsync(fd);
        }
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (now.tv_sec - start.tv_sec) + (now.tv_nsec - start.tv_nsec)/1e9;
        if (elapsed >= duration) break;
    }
    fsync(fd);
    close(fd);
    printf("TOTAL_BYTES:%ld\n", total);
    printf("WRITES:%ld\n", writes);
    return 0;
}
CEOF
  gcc -O2 -o "$IO_BIN" "$IO_SRC" -lm || return 1
  return 0
}

run_experiment() {
  echo "[exp1] Running overhead experiment (duration=${DURATION}s) -> $OUTDIR/experiment1"
  # compile or ensure workload
  if [ -f "$WORKLOAD_SRC" ]; then
    gcc -O2 -o "$WORKLOAD_BIN" "$WORKLOAD_SRC" -lm || { echo "Failed to compile workload" >&2; return 1; }
  else
    ensure_workload || { echo "Failed to create temporary workload" >&2; return 1; }
  fi

  SUMMARY="$OUTDIR/overhead_summary.csv"
  echo "mode,interval,run,elapsed_sec,percent_cpu" > "$SUMMARY"

  INTERVALS=(1 0.5 0.2)
  for interval in "${INTERVALS[@]}"; do
    for run in 1 2 3; do
      echo "[exp1] baseline interval=$interval run=$run"
      /usr/bin/time -v -o "$OUTDIR/baseline_${interval}_${run}.time" "$WORKLOAD_BIN" "$DURATION" || true
      elapsed_line=$(grep -E "Elapsed |Elapsed \(wall clock\)" -m1 "$OUTDIR/baseline_${interval}_${run}.time" || true)
      elapsed_val=$(echo "$elapsed_line" | sed -E 's/.*: (.*)/\1/')
      if [ -z "$elapsed_val" ]; then elapsed_sec=0; else elapsed_sec=$(echo "$elapsed_val" | awk -F: '{ if (NF==3) printf("%.3f", $1*3600 + $2*60 + $3); else if (NF==2) printf("%.3f", $1*60 + $2); else printf("%.3f", $1) }'); fi
      pct_val=$(grep "Percent of CPU" -m1 "$OUTDIR/baseline_${interval}_${run}.time" | sed -E 's/[^0-9]*([0-9]+\.?[0-9]*).*/\1/' || true)
      echo "baseline,$interval,$run,$elapsed_sec,$pct_val" >> "$SUMMARY"

      echo "[exp1] monitored interval=$interval run=$run"
      "$WORKLOAD_BIN" "$DURATION" &
      wl_pid=$!
      sleep 0.2
      if [ -x "$MONITOR_BIN" ]; then
        "$MONITOR_BIN" "$wl_pid" "$OUTDIR/metrics_${interval}_${run}.csv" "$interval" &
        mon_pid=$!
      else
        mon_pid=""
      fi
      wait $wl_pid || true
      sleep 0.5
      if [ -n "${mon_pid}" ] && kill -0 "$mon_pid" 2>/dev/null; then kill "$mon_pid" || true; fi

      if [ -f "$OUTDIR/metrics_${interval}_${run}.csv" ]; then
        first_ts=$(awk -F, 'NR==2{print $1; exit}' "$OUTDIR/metrics_${interval}_${run}.csv")
        last_ts=$(awk -F, 'END{print $1}' "$OUTDIR/metrics_${interval}_${run}.csv")
        if [ -n "$first_ts" ] && [ -n "$last_ts" ]; then
          elapsed_mon=$(awk "BEGIN{ if(\"$first_ts\"==\"\" || \"$last_ts\"==\"\") print 0; else printf(\"%.6f\", ($last_ts) - ($first_ts)) }")
        else
          elapsed_mon=0
        fi
        avg_cpu=$(awk -F, 'NR>1{sum+=$3; count++} END{if(count>0) printf("%.2f", sum/count); else print "0"}' "$OUTDIR/metrics_${interval}_${run}.csv")
      else
        elapsed_mon=0
        avg_cpu=0
      fi
      echo "monitored,$interval,$run,$elapsed_mon,$avg_cpu" >> "$SUMMARY"
    done
  done

  echo "[exp1] Done. Summary: $SUMMARY"
}

run_experiment2() {
  echo "[exp2] Running Namespace isolation experiment -> $OUTDIR/experiment2"
  EXP2_DIR="$OUTDIR/experiment2"
  mkdir -p "$EXP2_DIR"
  combos=("pid" "net" "mnt" "pid,net" "pid,mnt" "net,mnt" "pid,net,mnt")
  trials=5
  host_pids=$(ps -e --no-headers | wc -l || echo 0)
  host_nets=$(ip link show 2>/dev/null | grep -c ': ' || echo 0)
  host_mounts=$(mount | wc -l || echo 0)
  out_csv="$EXP2_DIR/experiment2_results.csv"
  echo "combo,trial,time_us,pid_count,net_links,mounts,isol_pid,isol_net,isol_mnt" > "$out_csv"

  for combo in "${combos[@]}"; do
    for t in $(seq 1 $trials); do
      flags=()
      IFS=, read -ra parts <<< "$combo"
      for p in "${parts[@]}"; do
        case "$p" in
          pid) flags+=("--pid") ;;
          net) flags+=("--net") ;;
          mnt) flags+=("--mount") ;;
        esac
      done

      start_ns=$(date +%s%N)
      if [ ${#flags[@]} -eq 0 ]; then
        probe_out="$(ps -e --no-headers | wc -l),$(ip link show 2>/dev/null | grep -c ': '),$(mount | wc -l)" || probe_out=",,"
      else
        probe_out=$(unshare --fork "${flags[@]}" -- bash -c 'echo "$(ps -e --no-headers | wc -l),$(ip link show 2>/dev/null | grep -c ": "),$(mount | wc -l)"' 2>/dev/null || echo ",,")
      fi
      end_ns=$(date +%s%N)
      time_us=$(( (end_ns - start_ns) / 1000 ))

      pid_count=$(echo "$probe_out" | awk -F, '{print $1+0}')
      net_links=$(echo "$probe_out" | awk -F, '{print $2+0}')
      mounts=$(echo "$probe_out" | awk -F, '{print $3+0}')

      isol_pid=no; isol_net=no; isol_mnt=no
      if [ "$pid_count" -gt 0 ] && [ "$pid_count" -lt "$host_pids" ]; then isol_pid=yes; fi
      if [ "$net_links" -gt 0 ] && [ "$net_links" -lt "$host_nets" ]; then isol_net=yes; fi
      if [ "$mounts" -gt 0 ] && [ "$mounts" -lt "$host_mounts" ]; then isol_mnt=yes; fi

      echo "$combo,$t,$time_us,$pid_count,$net_links,$mounts,$isol_pid,$isol_net,$isol_mnt" >> "$out_csv"
      echo "[exp2] $combo trial $t: time_us=$time_us pid_count=$pid_count net_links=$net_links mounts=$mounts -> isol:$isol_pid,$isol_net,$isol_mnt"
      sleep 0.2
    done
  done

  echo "[exp2] Results saved to: $out_csv"
}

if [ "$MODE" = "exp2" ]; then
  run_experiment2
  # run visualizer for exp2 results
  if command -v python3 >/dev/null 2>&1; then
    python3 "$SCRIPT_DIR/visualize.py" --dir "$OUTDIR/experiment2" --out "$OUTDIR/experiment2/plots" --formats png,svg || true
  fi
  exit 0
elif [ "$MODE" = "exp3" ]; then
  # Ensure workload
  ensure_workload || { echo "No workload available for exp3" >&2; exit 1; }

  echo "[exp3] Running CPU throttling experiment"
  EXP3_DIR="$OUTDIR/experiment3"
  mkdir -p "$EXP3_DIR"
  limits=(0.25 0.5 1.0 2.0)
  trials=3
  echo "limit,trial,limit_cores,applied,max_cpu_pct,measured_cpu_pct,throughput_iters" > "$EXP3_DIR/exp3_results.csv"

  for lim in "${limits[@]}"; do
    for t in $(seq 1 $trials); do
      echo "[exp3] limit=$lim trial=$t"
      # Try to create cgroup and set cpu.max (best-effort)
      CG_DIR="/sys/fs/cgroup/resource_monitor_exp3"
      if [ ! -d "$CG_DIR" ]; then
        mkdir -p "$CG_DIR" 2>/dev/null || true
      fi
      period=100000
      max_usec=$(python3 - <<PY
print(int($lim * $period))
PY
)
      applied=no
      if [ -w "$CG_DIR" ] || sudo test -w "$CG_DIR" 2>/dev/null; then
        # write cgroup.subtree_control on parent if needed
        if [ -f "$CG_DIR/cpu.max" ]; then
          sudo sh -c "echo \"$max_usec $period\" > $CG_DIR/cpu.max" 2>/dev/null || sudo sh -c "echo \"$max_usec $period\" > $CG_DIR/cpu.max" 2>/dev/null || true
          applied=yes
        fi
      fi

      # Run workload and measure CPU via /proc/<pid>/stat sampling with python
      LOG_FILE="$EXP3_DIR/workload_limit_${lim}_trial_${t}.log"
      "$WORKLOAD_BIN" 5 > "$LOG_FILE" 2>&1 &
      wpid=$!
      # add to cgroup if possible
      if [ -d "$CG_DIR" ] && [ -w "$CG_DIR" ]; then
        echo $wpid > "$CG_DIR/cgroup.procs" 2>/dev/null || true
      elif command -v sudo >/dev/null 2>&1; then
        sudo sh -c "echo $wpid > $CG_DIR/cgroup.procs" 2>/dev/null || true
      fi

      # monitor CPU usage and iterations (workload prints ITERATIONS: at end)
      # use python to compute measured cpu percent from /proc/<pid>/stat
      measured=$(python3 - <<PY
import time,sys
pid = $wpid
try:
    clk = float(sysconf_clocks := 100)
except Exception:
    clk = 100.0
try:
    import os
    ticks = os.sysconf(os.sysconf_names['SC_CLK_TCK'])
except Exception:
    ticks = 100
start = time.time()
def utime(pid):
    try:
        with open(f"/proc/{pid}/stat") as f:
            s=f.read().split()
            ut=int(s[13]); st=int(s[14]); return (ut+st)
    except Exception:
        return None
v1=utime(pid)
time.sleep(5)
v2=utime(pid)
if v1 is None or v2 is None:
    print('0')
else:
    delta = v2 - v1
    # CPU seconds = delta / ticks
    cpu_sec = delta / ticks
    wall = 5.0
    # assume single core baseline -> percent = 100 * cpu_sec / wall
    pct = 100.0 * cpu_sec / wall
    print(f"{pct:.2f}")
PY
)

      # wait for workload to finish and extract throughput from its output
      wait $wpid 2>/dev/null || true
      throughput=0
      if [ -f "$LOG_FILE" ]; then
        iter_line=$(grep -Eo 'ITERATIONS:[0-9]+' "$LOG_FILE" | tail -1 || true)
        if [ -n "$iter_line" ]; then
          throughput=$(echo "$iter_line" | sed 's/[^0-9]*//g')
        fi
      fi
      echo "$lim,$t,$lim,$applied,$max_usec,$measured,$throughput" >> "$EXP3_DIR/exp3_results.csv"
      sleep 0.5
    done
  done

  # run visualizer for exp3 results (best-effort)
  if command -v python3 >/dev/null 2>&1; then
    python3 "$SCRIPT_DIR/visualize.py" --dir "$EXP3_DIR" --out "$EXP3_DIR/plots" --formats png,svg || true
  fi
  exit 0
else
  run_experiment
  # run visualizer if available
  REPO_ROOT="$ROOT_DIR/.."
  VENV_PY="$REPO_ROOT/.venv/bin/python"
  if [ -x "$VENV_PY" ]; then
    "$VENV_PY" "$SCRIPT_DIR/visualize.py" --dir "$OUTDIR" --out "$PLOTS_DIR" --formats png,svg || true
  else
    command -v python3 >/dev/null 2>&1 && python3 "$SCRIPT_DIR/visualize.py" --dir "$OUTDIR" --out "$PLOTS_DIR" --formats png,svg || true
  fi
  echo
  ls -l "$OUTDIR" || true
  echo
  ls -l "$PLOTS_DIR" || true
fi

exit 0
#!/usr/bin/env bash
set -euo pipefail
# compare_tools.sh
# Consolidated script that runs the overhead experiment (short) and the visualizer.
# This merges the prior wrapper + experiment script into a single, self-contained tool.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
RESOURCE_MONITOR_DIR="$(cd "$SCRIPT_DIR/.." && pwd -P)"

# Force output to be inside the resource-monitor folder
OUTDIR="$RESOURCE_MONITOR_DIR/out/experiments"
PLOTS_DIR="$OUTDIR/plots"
mkdir -p "$OUTDIR" "$PLOTS_DIR"

# Parse arguments: numeric first-arg => duration for exp1; otherwise explicit mode
MODE=""
DURATION=""
if [ "$#" -eq 0 ]; then
  MODE="exp1"
  DURATION=5
else
  case "$1" in
    exp1)
      MODE="exp1"
      DURATION="${2:-5}"
      ;;
    exp2)
      MODE="exp2"
      ;;
    exp3)
      MODE="exp3"
      ;;
    exp4)
      MODE="exp4"
      ;;
    exp5)
      MODE="exp5"
      ;;
    *)
      # numeric (int or float) -> duration for exp1
      if [[ "$1" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
        MODE="exp1"
        DURATION="$1"
      else
        MODE="$1"
        DURATION="${2:-5}"
      fi
      ;;
  esac
fi

# ensure a sane default
DURATION="${DURATION:-5}"

# Workload and monitor locations
WORKLOAD_SRC="$RESOURCE_MONITOR_DIR/tests/bench_cpu.c"
WORKLOAD_BIN="$RESOURCE_MONITOR_DIR/tests/bench_cpu"
MONITOR_BIN="$RESOURCE_MONITOR_DIR/resource_monitor"

INTERVALS=(1 0.5 0.2)

run_experiment() {
  echo "Running overhead experiment (duration=${DURATION}s) -> $OUTDIR"

  # compile workload if source exists
  if [ -f "$WORKLOAD_SRC" ]; then
    echo "Compiling workload: $WORKLOAD_SRC -> $WORKLOAD_BIN"
    gcc -O2 -o "$WORKLOAD_BIN" "$WORKLOAD_SRC" -lm || {
      echo "Failed to compile workload" >&2
      return 1
    }
  else
    echo "Workload source not found: $WORKLOAD_SRC" >&2
    return 1
  fi

  if [ ! -x "$MONITOR_BIN" ]; then
    echo "Warning: monitor binary not found or not executable: $MONITOR_BIN" >&2
    echo "If you have built the monitor, ensure it's at: $MONITOR_BIN" >&2
  fi

  SUMMARY_CSV="$OUTDIR/overhead_summary.csv"
  echo "mode,interval,run,elapsed_sec,percent_cpu" > "$SUMMARY_CSV"

  for interval in "${INTERVALS[@]}"; do
    for run in 1 2 3; do
      echo "--- Baseline run #$run (no monitor), interval=${interval} ---"
      # Time workload in foreground to get a baseline
      /usr/bin/time -v -o "$OUTDIR/baseline_${interval}_${run}.time" "$WORKLOAD_BIN" "$DURATION" || true

      # extract elapsed and percent CPU robustly
      elapsed_line=$(grep -E "Elapsed |Elapsed \(wall clock\)" -m1 "$OUTDIR/baseline_${interval}_${run}.time" || true)
      elapsed_val=$(echo "$elapsed_line" | sed -E 's/.*: (.*)/\1/')
      if [ -z "$elapsed_val" ]; then
        elapsed_sec=0
      else
        elapsed_sec=$(echo "$elapsed_val" | awk -F: '{ if (NF==3) printf("%.3f", $1*3600 + $2*60 + $3); else if (NF==2) printf("%.3f", $1*60 + $2); else printf("%.3f", $1) }')
      fi
      pct_val=$(grep "Percent of CPU" -m1 "$OUTDIR/baseline_${interval}_${run}.time" | sed -E 's/[^0-9]*([0-9]+\.?[0-9]*).*/\1/' || true)
      echo "baseline,$interval,$run,$elapsed_sec,$pct_val" >> "$SUMMARY_CSV"

      echo "--- Monitored run #$run (interval=${interval}) ---"
      # Start workload in background
      "$WORKLOAD_BIN" "$DURATION" &
      wl_pid=$!
      sleep 0.2

      # Start monitor in background if available
      if [ -x "$MONITOR_BIN" ]; then
        "$MONITOR_BIN" "$wl_pid" "$OUTDIR/metrics_${interval}_${run}.csv" "$interval" &
        mon_pid=$!
      else
        mon_pid=""
      fi

      # wait for workload
      wait $wl_pid || true
      sleep 0.5
      if [ -n "${mon_pid}" ] && kill -0 "$mon_pid" 2>/dev/null; then
        kill "$mon_pid" || true
      fi

      # compute metrics from CSV if present
      if [ -f "$OUTDIR/metrics_${interval}_${run}.csv" ]; then
        first_ts=$(awk -F, 'NR==2{print $1; exit}' "$OUTDIR/metrics_${interval}_${run}.csv")
        last_ts=$(awk -F, 'END{print $1}' "$OUTDIR/metrics_${interval}_${run}.csv")
        # handle non-integer timestamps safely
        elapsed_mon=0
        if [ -n "$first_ts" ] && [ -n "$last_ts" ]; then
          elapsed_mon=$(awk "BEGIN{ if(\"$first_ts\"==\"\" || \"$last_ts\"==\"\") print 0; else printf(\"%.6f\", ($last_ts) - ($first_ts)) }")
        fi
        avg_cpu=$(awk -F, 'NR>1{sum+=$3; count++} END{if(count>0) printf("%.2f", sum/count); else print "0"}' "$OUTDIR/metrics_${interval}_${run}.csv")
      else
        elapsed_mon=0
        avg_cpu=0
      fi
      echo "monitored,$interval,$run,$elapsed_mon,$avg_cpu" >> "$SUMMARY_CSV"

    done
  done

  echo "Done. Summary CSV: $SUMMARY_CSV"
  echo "Printing summary:"
  column -t -s, "$SUMMARY_CSV" | sed -n '1,200p' || true
}

run_visualizer() {
  echo "Running visualizer..."
  REPO_ROOT="$(cd "$RESOURCE_MONITOR_DIR/.." && pwd -P)"
  VENV_PY="$REPO_ROOT/.venv/bin/python"
  if [ ! -x "$VENV_PY" ]; then
    echo "Warning: venv python not found at $VENV_PY. Falling back to system python3." >&2
    VENV_PY="$(command -v python3 || true)"
    if [ -z "$VENV_PY" ]; then
      echo "No python3 available to run visualizer." >&2
      return 1
    fi
  fi

  VISUALIZER="$RESOURCE_MONITOR_DIR/scripts/visualize.py"
  if [ ! -f "$VISUALIZER" ]; then
    echo "Visualizer not found: $VISUALIZER" >&2
    return 1
  fi

  "$VENV_PY" "$VISUALIZER" --dir "$OUTDIR" --out "$PLOTS_DIR" --formats png,svg || {
    echo "Visualizer failed (exit code $?)." >&2
    return 1
  }

  echo "Results saved to: $PLOTS_DIR"
}

run_experiment4() {
  echo "[exp4] Memory limit experiment -> $OUTDIR/experiment4"
  EXP4_DIR="$OUTDIR/experiment4"
  mkdir -p "$EXP4_DIR"
  # Ensure mem workload is compiled into repo-local tmp/ (no sudo redirection issues)
  TMPDIR="$RESOURCE_MONITOR_DIR/tmp"
  mkdir -p "$TMPDIR"
  ensure_mem_workload || { echo "Failed to build mem workload" >&2; return 1; }

  # cgroup v2 target directory (best-effort). This script attempts to write
  # to /sys/fs/cgroup. Running the experiment with `sudo` is recommended so
  # the script can create cgroups and write controller files (e.g. memory.max).
  CG_DIR="/sys/fs/cgroup/resource_monitor_exp4"

  # detect controllers available on this host
  CG_CONTROLLERS_FILE="/sys/fs/cgroup/cgroup.controllers"
  HAS_MEMORY=0
  if [ -f "$CG_CONTROLLERS_FILE" ]; then
    controllers=$(cat "$CG_CONTROLLERS_FILE" 2>/dev/null || echo "")
    case " $controllers " in
      *" memory "*) HAS_MEMORY=1 ;;
    esac
  fi
  LIMIT_BYTES=$((100 * 1024 * 1024)) # 100MB
  trials=3
  echo "limit_bytes,trial,max_alloc_bytes,failcnt,oom_kills,exit_status,logfile" > "$EXP4_DIR/exp4_results.csv"

  for t in $(seq 1 $trials); do
    echo "[exp4] trial=$t"
    # create cgroup (best-effort). Warn if cgroup v2 is not present.
    if [ ! -d "$CG_DIR" ]; then mkdir -p "$CG_DIR" || true; fi
    if [ "$(stat -f -c %T /sys/fs/cgroup 2>/dev/null || echo none)" != "cgroup2" ]; then
      echo "[exp4] Warning: system does not appear to be using cgroup v2; results may be best-effort only." >&2
    fi
    applied=no
    if [ -w "$CG_DIR" ] || sudo test -w "$CG_DIR" 2>/dev/null; then
      if [ -f "$CG_DIR/memory.max" ]; then
        sudo sh -c "echo $LIMIT_BYTES > $CG_DIR/memory.max" 2>/dev/null || true
        applied=yes
      fi
    fi

    LOG="$EXP4_DIR/mem_trial_${t}.log"
    # run workload inside cgroup if possible
    "$TMPDIR/mem_alloc" 0 > "$LOG" 2>&1 &
    wpid=$!
    # move to cgroup
    if [ -d "$CG_DIR" ]; then
      if [ -w "$CG_DIR" ]; then
        echo $wpid > "$CG_DIR/cgroup.procs" 2>/dev/null || true
      else
        sudo sh -c "echo $wpid > $CG_DIR/cgroup.procs" 2>/dev/null || true
      fi
    fi
    wait $wpid || true

    # Prefer explicit MAX_ALLOC printed by workload; fall back to last ALLOC:
    max_alloc=$(grep -Eo 'MAX_ALLOC:[0-9]+' "$LOG" | tail -1 | sed 's/[^0-9]*//g' || true)
    if [ -z "$max_alloc" ]; then
      max_alloc=$(grep -Eo 'ALLOC:[0-9]+' "$LOG" | tail -1 | sed 's/[^0-9]*//g' || true)
    fi
    max_alloc=${max_alloc:-0}
    failcnt=0
    oom_kills=0
    if [ "$HAS_MEMORY" -eq 1 ] && [ -f "$CG_DIR/memory.failcnt" ]; then
      failcnt=$(cat "$CG_DIR/memory.failcnt" 2>/dev/null || echo 0)
    elif [ "$HAS_MEMORY" -eq 1 ] && [ -f "$CG_DIR/memory.events" ]; then
      oom_kills=$(grep -Eo 'oom_kill [0-9]+' "$CG_DIR/memory.events" | awk '{print $2}' || echo 0)
    fi
    status=$?

    # Quote logfile path to avoid CSV issues with commas in paths
    printf '%s,%s,%s,%s,%s,%s,"%s"\n' "$LIMIT_BYTES" "$t" "$max_alloc" "$failcnt" "$oom_kills" "$status" "$LOG" >> "$EXP4_DIR/exp4_results.csv"
    sleep 0.5
  done

  # run visualizer for exp4
  if command -v python3 >/dev/null 2>&1; then
    python3 "$SCRIPT_DIR/visualize.py" --dir "$EXP4_DIR" --out "$EXP4_DIR/plots" --formats png,svg || true
  fi
  echo "[exp4] results -> $EXP4_DIR/exp4_results.csv"
}

run_experiment5() {
  echo "[exp5] I/O limit experiment -> $OUTDIR/experiment5"
  EXP5_DIR="$OUTDIR/experiment5"
  mkdir -p "$EXP5_DIR"
  # Ensure I/O workload is compiled into repo-local tmp/ to avoid sudo redirection issues
  TMPDIR="$RESOURCE_MONITOR_DIR/tmp"
  mkdir -p "$TMPDIR"
  ensure_io_workload || { echo "Failed to build io workload" >&2; return 1; }

  # cgroup v2 directory; best-effort writes are attempted. Running with sudo
  # is recommended so io.max and cgroup.procs can be written.
  CG_DIR="/sys/fs/cgroup/resource_monitor_exp5"

  # detect controllers available on this host
  CG_CONTROLLERS_FILE="/sys/fs/cgroup/cgroup.controllers"
  HAS_IO=0
  if [ -f "$CG_CONTROLLERS_FILE" ]; then
    controllers=$(cat "$CG_CONTROLLERS_FILE" 2>/dev/null || echo "")
    case " $controllers " in
      *" io "*) HAS_IO=1 ;;
    esac
  fi
  limits=(1048576 5242880 10485760) # 1MB/s,5MB/s,10MB/s
  trials=3
  echo "limit_bps,trial,measured_bytes,measured_bps,avg_write_latency_us,run_time_s,applied,logfile" > "$EXP5_DIR/exp5_results.csv"

  for lim in "${limits[@]}"; do
    for t in $(seq 1 $trials); do
      echo "[exp5] limit=${lim} trial=${t}"
      if [ ! -d "$CG_DIR" ]; then mkdir -p "$CG_DIR" || true; fi
      if [ "$(stat -f -c %T /sys/fs/cgroup 2>/dev/null || echo none)" != "cgroup2" ]; then
        echo "[exp5] Warning: system does not appear to be using cgroup v2; I/O limits may not be applied." >&2
      fi
      if [ "$HAS_IO" -ne 1 ]; then
        echo "[exp5] Info: 'io' controller not enabled on this host; io.max writes will be skipped." >&2
      fi
      applied=no
      if [ "$HAS_IO" -eq 1 ] && [ -f "$CG_DIR/io.max" ]; then
        # best-effort: try to write a simple wildcard limit (may require root)
        sudo sh -c "echo \"* rbps=${lim} wbps=${lim}\" > $CG_DIR/io.max" 2>/dev/null || true
        applied=yes
      else
        applied=no
      fi

      LOG="$EXP5_DIR/io_limit_${lim}_trial_${t}.log"
      OUTF="$EXP5_DIR/io_out_${lim}_${t}.dat"
      # run workload (uses repo-local tmp/io_workload)
      "$TMPDIR/io_workload" "$OUTF" 5 65536 > "$LOG" 2>&1 &
      wpid=$!
      # move to cgroup
      if [ -d "$CG_DIR" ]; then
        if [ -w "$CG_DIR" ]; then
          echo $wpid > "$CG_DIR/cgroup.procs" 2>/dev/null || true
        else
          sudo sh -c "echo $wpid > $CG_DIR/cgroup.procs" 2>/dev/null || true
        fi
      fi
      wait $wpid || true

      total_bytes=$(grep -Eo 'TOTAL_BYTES:[0-9]+' "$LOG" | tail -1 | sed 's/[^0-9]*//g' || echo 0)
      writes=$(grep -Eo 'WRITES:[0-9]+' "$LOG" | tail -1 | sed 's/[^0-9]*//g' || echo 0)
      # rough run time: if writes>0, time = duration param (5s), else 0
      run_time=5
      measured_bps=0
      if [ "$run_time" -gt 0 ]; then
        measured_bps=$(( total_bytes / run_time ))
      fi
      avg_lat_us=0
      # Quote logfile path to avoid CSV parsing issues
      printf '%s,%s,%s,%s,%s,%s,%s,"%s"\n' "${lim}" "${t}" "${total_bytes}" "${measured_bps}" "${avg_lat_us}" "${run_time}" "${applied}" "${LOG}" >> "$EXP5_DIR/exp5_results.csv"
      sleep 0.5
    done
  done

  if command -v python3 >/dev/null 2>&1; then
    python3 "$SCRIPT_DIR/visualize.py" --dir "$EXP5_DIR" --out "$EXP5_DIR/plots" --formats png,svg || true
  fi
  echo "[exp5] results -> $EXP5_DIR/exp5_results.csv"
}

run_experiment2() {
  echo "Running Experiment 2: Namespace isolation"
  EXP2_OUT="$OUTDIR/experiment2"
  mkdir -p "$EXP2_OUT"

  combos=( 
    "pid" 
    "net" 
    "mnt" 
    "pid,net" 
    "pid,mnt" 
    "net,mnt" 
    "pid,net,mnt" 
  )

  trials=5
  host_pid_count=$(ps -e --no-headers | wc -l || echo 0)
  host_net_links=$(ip link show 2>/dev/null | grep -c ": " || echo 0)
  host_mounts=$(mount | wc -l || echo 0)

  echo "combo,trial,time_us,pid_count,net_links,mounts,isol_pid,isol_net,isol_mnt" > "$EXP2_OUT/experiment2_results.csv"

  for combo in "${combos[@]}"; do
    for t in $(seq 1 $trials); do
      # build unshare flags
      flags=()
      IFS=, read -ra parts <<< "$combo"
      for p in "${parts[@]}"; do
        case "$p" in
          pid) flags+=("--pid") ;;
          net) flags+=("--net") ;;
          mnt) flags+=("--mount") ;;
        esac
      done

      # time the creation using nanosecond timestamps
      start=$(date +%s%N)

      # Run simple shell probes to get pid count, net links and mounts
          # Check if unshare is available and allowed on this system (best-effort)
          if command -v unshare >/dev/null 2>&1; then
            # quick capability test: try a harmless unshare of pid namespace
            if unshare --fork --pid true 2>/dev/null; then
              UN_SHARE_OK=1
            else
              UN_SHARE_OK=0
            fi
          else
            UN_SHARE_OK=0
          fi

          if [ "$UN_SHARE_OK" -eq 1 ]; then
            if [ ${#flags[@]} -eq 0 ]; then
              out="$(ps -e --no-headers | wc -l),$(ip link show 2>/dev/null | grep -c ': '),$(mount | wc -l)" || out=",,"
            else
              # run the probes inside the new namespaces
              out=$(unshare --fork "${flags[@]}" -- bash -c 'echo "$(ps -e --no-headers | wc -l),$(ip link show 2>/dev/null | grep -c ": "),$(mount | wc -l)"' 2>/dev/null || echo ",,")
            fi
          else
            out="$(ps -e --no-headers | wc -l),$(ip link show 2>/dev/null | grep -c ': '),$(mount | wc -l)" || out=",,"
            if [ "$t" -eq 1 ] && [ ${#flags[@]} -gt 0 ]; then
              echo "[exp2] Warning: 'unshare' not available or cannot create namespaces on this host. To test namespace isolation you need 'unshare' (util-linux) and kernel support (user namespaces) or root privileges for some namespace types." >&2
            fi
          fi

      end=$(date +%s%N)
      diff_ns=$((end - start))
      # microseconds
      time_us=$((diff_ns / 1000))

      # parse outputs
      pid_count=$(echo "$out" | awk -F, '{print $1+0}')
      net_links=$(echo "$out" | awk -F, '{print $2+0}')
      mounts=$(echo "$out" | awk -F, '{print $3+0}')

      isol_pid="no"
      isol_net="no"
      isol_mnt="no"
      # heuristic: if counts are smaller than host, consider isolated
      if [ "$pid_count" -lt "$host_pid_count" ] && [ "$pid_count" -gt 0 ]; then isol_pid="yes"; fi
      if [ "$net_links" -lt "$host_net_links" ] && [ "$net_links" -gt 0 ]; then isol_net="yes"; fi
      if [ "$mounts" -lt "$host_mounts" ] && [ "$mounts" -gt 0 ]; then isol_mnt="yes"; fi

        echo "\"$combo\",$t,$time_us,$pid_count,$net_links,$mounts,$isol_pid,$isol_net,$isol_mnt" >> "$EXP2_OUT/experiment2_results.csv"
      echo "[exp2] combo=${combo} trial=${t} time_us=${time_us} pid_count=${pid_count} net_links=${net_links} mounts=${mounts} -> isol:${isol_pid},${isol_net},${isol_mnt}"
      sleep 0.2
    done
  done

  echo "Experiment 2 results: $EXP2_OUT/experiment2_results.csv"
}

case "$MODE" in
  exp2)
    run_experiment2
    exit 0
    ;;
  exp3)
    run_experiment3() { :; } || true
    # the exp3 logic exists inline earlier; call by evaluating the block by name
    # call the fragment by invoking the function run_experiment3_main if present
    if type run_experiment >/dev/null 2>&1; then
      # exp3 implementation is inline above; invoke it via MODE check path
      # call the code block by reusing the earlier branch
      # For backward compatibility call the block directly
      # (we implemented exp3 earlier; emulate by re-running script with exp3)
      bash "$SCRIPT_DIR/compare_tools.sh" exp3
      exit 0
    fi
    ;;
  exp4)
    run_experiment4
    exit 0
    ;;
  exp5)
    run_experiment5
    exit 0
    ;;
  exp1)
    run_experiment
    run_visualizer
    ;;
  *)
    run_experiment
    run_visualizer
    ;;
esac

echo
ls -l "$OUTDIR" || true
echo
ls -l "$PLOTS_DIR" || true
