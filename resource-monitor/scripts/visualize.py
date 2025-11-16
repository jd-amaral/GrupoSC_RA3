#!/usr/bin/env python3
"""
scripts/visualize.py

Reads experiment results produced by scripts/experiment_overhead.sh (default:
out/experiments/overhead_summary.csv and per-run metrics CSVs) and generates
summary tables and plots. Saves outputs to out/experiments/plots by default.

Usage:
  python scripts/visualize.py --dir out/experiments --show
  python scripts/visualize.py --dir out/experiments --out plots_dir --save-formats png,svg

The script computes:
 - aggregated mean elapsed and mean CPU per interval and mode (baseline/monitored)
 - overhead percentages (time and CPU)
 - sampling latency statistics from per-run files metrics_<interval>_<run>.csv
 - saves plots and a summary CSV

"""
import argparse
import sys
from pathlib import Path
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns
import logging
import json

logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
sns.set(style="whitegrid")


def load_summary(path: Path) -> pd.DataFrame:
    if not path.exists():
        raise FileNotFoundError(f"Summary CSV not found: {path}")
    df = pd.read_csv(path)
    return df


def discover_metrics_files(experiment_dir: Path):
    # pattern metrics_<interval>_<run>.csv
    return sorted(experiment_dir.glob("metrics_*.csv"))


def compute_latency_stats(metrics_file: Path):
    try:
        df = pd.read_csv(metrics_file, header=0)
    except Exception as e:
        logging.warning(f"Failed to read {metrics_file}: {e}")
        return None

    if df.shape[0] < 2:
        return None

    # assume timestamp is first column (epoch seconds), allow floats
    ts = pd.to_numeric(df.iloc[:, 0], errors="coerce").dropna().astype(float)
    if ts.shape[0] < 2:
        return None
    diffs = np.diff(ts.values)
    # remove zeros or negative
    diffs = diffs[diffs > 0]
    if diffs.size == 0:
        return None
    stats = {
        "file": str(metrics_file.name),
        "n_samples": int(ts.shape[0]),
        "mean_latency_s": float(np.mean(diffs)),
        "median_latency_s": float(np.median(diffs)),
        "std_latency_s": float(np.std(diffs)),
        "min_latency_s": float(np.min(diffs)),
        "max_latency_s": float(np.max(diffs)),
    }
    return stats


def ensure_out_dir(p: Path):
    p.mkdir(parents=True, exist_ok=True)
    return p


def plot_and_save(df_agg: pd.DataFrame, df: pd.DataFrame, latency_df: pd.DataFrame, out_dir: Path, formats=("png",)):
    ensure_out_dir(out_dir)

    # 1) Bar: elapsed baseline vs monitored per interval
    try:
        fig, ax = plt.subplots()
        intervals = df_agg['interval'].astype(str)
        width = 0.35
        x = np.arange(len(intervals))
        ax.bar(x - width/2, df_agg['elapsed_baseline'], width, label='baseline')
        ax.bar(x + width/2, df_agg['elapsed_monitored'], width, label='monitored')
        ax.set_xticks(x)
        ax.set_xticklabels(intervals)
        ax.set_xlabel('interval (s)')
        ax.set_ylabel('elapsed (s)')
        ax.set_title('Mean elapsed: baseline vs monitored')
        ax.legend()
        for fmt in formats:
            fig_path = out_dir / f"elapsed_bar.{fmt}"
            fig.savefig(fig_path, bbox_inches='tight')
            logging.info(f"Saved {fig_path}")
        plt.close(fig)
    except Exception as e:
        logging.warning(f"Failed to create elapsed bar plot: {e}")

    # 2) Bar: CPU baseline vs monitored
    try:
        fig, ax = plt.subplots()
        ax.bar(x - width/2, df_agg['cpu_baseline'], width, label='baseline')
        ax.bar(x + width/2, df_agg['cpu_monitored'], width, label='monitored')
        ax.set_xticks(x)
        ax.set_xticklabels(intervals)
        ax.set_xlabel('interval (s)')
        ax.set_ylabel('cpu (%)')
        ax.set_title('Mean CPU: baseline vs monitored')
        ax.legend()
        for fmt in formats:
            fig_path = out_dir / f"cpu_bar.{fmt}"
            fig.savefig(fig_path, bbox_inches='tight')
            logging.info(f"Saved {fig_path}")

        plt.close(fig)
    except Exception as e:
        logging.warning(f"Failed to create cpu bar plot: {e}")

    # 3) Overhead bar
    try:
        fig, ax = plt.subplots()
        ax.bar(intervals, df_agg['elapsed_overhead_pct'], label='time overhead (%)')
        ax.bar(intervals, df_agg['cpu_overhead_pct'], alpha=0.7, label='cpu overhead (%)')
        ax.set_xlabel('interval (s)')
        ax.set_ylabel('overhead (%)')
        ax.set_title('Monitoring overhead (%)')
        ax.legend()
        for fmt in formats:
            fig_path = out_dir / f"overhead_bar.{fmt}"
            fig.savefig(fig_path, bbox_inches='tight')
            logging.info(f"Saved {fig_path}")
        plt.close(fig)
    except Exception as e:
        logging.warning(f"Failed to create overhead plot: {e}")

    # 4) Boxplot of CPU by mode and interval
    try:
        fig, ax = plt.subplots(figsize=(10,5))
        sns.boxplot(x='interval', y='percent_cpu', hue='mode', data=df, ax=ax)
        ax.set_title('CPU distribution by interval and mode')
        for fmt in formats:
            fig_path = out_dir / f"cpu_boxplot.{fmt}"
            fig.savefig(fig_path, bbox_inches='tight')
            logging.info(f"Saved {fig_path}")
        plt.close(fig)
    except Exception as e:
        logging.warning(f"Failed to create cpu boxplot: {e}")

    # 5) Latency histogram
    if latency_df is not None and not latency_df.empty:
        try:
            fig, ax = plt.subplots()
            sns.histplot(latency_df['mean_latency_s'].dropna(), bins=20, kde=True, ax=ax)
            ax.set_xlabel('mean sampling latency (s)')
            ax.set_title('Distribution of mean sampling latency (per-run)')
            for fmt in formats:
                fig_path = out_dir / f"latency_hist_mean.{fmt}"
                fig.savefig(fig_path, bbox_inches='tight')
                logging.info(f"Saved {fig_path}")
            plt.close(fig)
        except Exception as e:
            logging.warning(f"Failed to create latency histogram: {e}")


def summarize_and_plot(experiment_dir: Path, out_dir: Path, save_formats=('png',)):
    """Auto-detect experiment type and dispatch to the appropriate summarizer.

    Recognizes:
      - Experiment 1: presence of `overhead_summary.csv`
      - Experiment 2: presence of `experiment2_results.csv`
      - Experiment 3: presence of `exp3_results.csv`
    """
    exp2_csv = experiment_dir / 'experiment2_results.csv'
    exp3_csv = experiment_dir / 'exp3_results.csv'
    exp4_csv = experiment_dir / 'exp4_results.csv'
    exp5_csv = experiment_dir / 'exp5_results.csv'
    overhead_csv = experiment_dir / 'overhead_summary.csv'

    if exp2_csv.exists():
        summarize_and_plot_exp2(experiment_dir, out_dir, save_formats=save_formats)
        return
    if exp3_csv.exists():
        summarize_and_plot_exp3(experiment_dir, out_dir, save_formats=save_formats)
        return
    if exp4_csv.exists():
        summarize_and_plot_exp4(experiment_dir, out_dir, save_formats=save_formats)
        return
    if exp5_csv.exists():
        summarize_and_plot_exp5(experiment_dir, out_dir, save_formats=save_formats)
        return
    if overhead_csv.exists():
        # fallback to original experiment 1 handling
        logging.info(f"Loading summary: {overhead_csv}")
        df = load_summary(overhead_csv)

        # normalize columns
        df.columns = [c.strip().lower().replace(' ', '_') for c in df.columns]
        if 'elapsed_sec' in df.columns:
            df['elapsed_sec'] = pd.to_numeric(df['elapsed_sec'], errors='coerce')
        if 'percent_cpu' in df.columns:
            df['percent_cpu'] = pd.to_numeric(df['percent_cpu'], errors='coerce')
        if 'interval' in df.columns:
            df['interval'] = pd.to_numeric(df['interval'], errors='coerce')

        df = df.dropna(subset=['elapsed_sec','percent_cpu'])

        # aggregated
        m = df.groupby(['interval','mode']).agg(
            runs=('run','count'),
            elapsed_mean=('elapsed_sec','mean'),
            elapsed_std=('elapsed_sec','std'),
            cpu_mean=('percent_cpu','mean'),
            cpu_std=('percent_cpu','std')
        ).reset_index()

        pivot_elapsed = m.pivot(index='interval', columns='mode', values='elapsed_mean')
        pivot_cpu = m.pivot(index='interval', columns='mode', values='cpu_mean')

        agg = pd.DataFrame({'interval': pivot_elapsed.index})
        if 'baseline' in pivot_elapsed.columns and 'monitored' in pivot_elapsed.columns:
            agg['elapsed_baseline'] = pivot_elapsed['baseline'].values
            agg['elapsed_monitored'] = pivot_elapsed['monitored'].values
            # compute time overhead safely (avoid div-by-zero/infs)
            agg['elapsed_overhead_pct'] = np.where(
                agg['elapsed_baseline'] > 0,
                100.0 * (agg['elapsed_monitored'] - agg['elapsed_baseline']) / agg['elapsed_baseline'],
                np.nan
            )
        else:
            agg['elapsed_baseline'] = np.nan
            agg['elapsed_monitored'] = np.nan
            agg['elapsed_overhead_pct'] = np.nan

        if 'baseline' in pivot_cpu.columns and 'monitored' in pivot_cpu.columns:
            agg['cpu_baseline'] = pivot_cpu['baseline'].values
            agg['cpu_monitored'] = pivot_cpu['monitored'].values
            agg['cpu_overhead_pct'] = np.where(
                agg['cpu_baseline'] > 0,
                100.0 * (agg['cpu_monitored'] - agg['cpu_baseline']) / agg['cpu_baseline'],
                np.nan
            )
        else:
            agg['cpu_baseline'] = np.nan
            agg['cpu_monitored'] = np.nan
            agg['cpu_overhead_pct'] = np.nan

        agg = agg.sort_values('interval')

        logging.info('Aggregated table:')
        logging.info('\n' + agg.to_string(index=False))

        # compute latency stats from per-run metric files
        files = discover_metrics_files(experiment_dir)
        lat_stats = []
        for f in files:
            s = compute_latency_stats(f)
            if s:
                lat_stats.append(s)
        latency_df = pd.DataFrame(lat_stats) if lat_stats else pd.DataFrame()

        if not latency_df.empty:
            logging.info('Latency stats (per file):')
            logging.info('\n' + latency_df.head().to_string(index=False))
            latency_df.to_csv(out_dir / 'latency_per_run.csv', index=False)

        # save aggregated table
        agg.to_csv(out_dir / 'aggregated_summary.csv', index=False)

        # plotting
        plot_and_save(agg, df, latency_df, out_dir, formats=save_formats)
        logging.info(f'Results saved to {out_dir}')
        return

    raise FileNotFoundError(f"No recognized summary CSV in {experiment_dir}")

def start_flask_server(root_dir: Path, host: str = '127.0.0.1', port: int = 5000):
    try:
        from flask import Flask, jsonify, send_from_directory, render_template_string, request
    except Exception:
        logging.error('Flask is not installed. Install in the project venv: pip install flask')
        return

    app = Flask('rm_visualizer')

    # Map canonical experiment names to the exact plot directories requested by the user.
    # root_dir is typically 'out/experiments'. Build the expected absolute paths.
    exp_map = {}
    # Prefer the repo's resource-monitor/out/experiments location — resolve relative to this script
    repo_rm_dir = Path(__file__).resolve().parents[1]
    plots_base = repo_rm_dir / 'out' / 'experiments'
    exp_map = {
        'exp1': plots_base / 'plots',
        'exp2': plots_base / 'experiment2' / 'plots',
        'exp3': plots_base / 'experiment3' / 'plots',
        'exp4': plots_base / 'experiment4' / 'plots',
        'exp5': plots_base / 'experiment5' / 'plots',
    }

    @app.route('/')
    def index():
        html = '''
        <!doctype html>
        <html>
        <head>
          <meta charset="utf-8">
          <title>Resource Monitor Dashboard</title>
          <style>body{font-family:Arial,Helvetica,sans-serif;margin:20px} img{max-width:800px;border:1px solid #ddd;padding:4px;margin:6px}</style>
        </head>
        <body>
          <h2>Resource Monitor Dashboard</h2>
          <div id="content">Loading...</div>
          <script>
            async function listExps(){
              const res = await fetch('/api/experiments');
              const exps = await res.json();
              let html = '<ul>';
              for(const e of exps) html += `<li><a href="#" onclick="showExp('`+e+`')">${e}</a></li>`;
              html += '</ul>';
              // no remote start — UI only shows existing plots
              html += `<p>Available plots are shown per experiment. To run experiments use the Makefile locally.</p>`;
              document.getElementById('content').innerHTML = html;
            }
            async function showExp(name){
              const r = await fetch(`/api/exp/${name}/summary`);
              if(!r.ok){ document.getElementById('content').innerHTML = 'Failed to load'; return; }
              const data = await r.json();
              let html = `<h3>${name}</h3>`;
              html += '<div id="plots"></div>';
              document.getElementById('content').innerHTML = html;
              const pr = await fetch(`/api/exp/${name}/plots`);
              const plots = await pr.json();
              let phtml = '';
              for(const f of plots){ phtml += `<div><img src="/plots/${name}/${f}" alt="${f}"/><div>${f}</div></div>`; }
              document.getElementById('plots').innerHTML = phtml;
            }
                        // start() removed — server does not run experiments
            listExps();
          </script>
        </body>
        </html>
        '''
        return render_template_string(html)

    @app.route('/api/experiments')
    def list_experiments():
        # Return the canonical list in fixed order so UI shows experiments 1..5
        return jsonify([k for k in ['exp1', 'exp2', 'exp3', 'exp4', 'exp5']])

    @app.route('/api/exp/<name>/plots')
    def list_plots(name):
        pdir = exp_map.get(name)
        files = []
        if pdir and pdir.exists():
            for f in sorted(pdir.iterdir()):
                if f.is_file() and f.suffix.lower() in ('.png', '.svg'):
                    files.append(f.name)
        return jsonify(files)

    @app.route('/plots/<exp>/<path:fname>')
    def serve_plot(exp, fname):
        pdir = exp_map.get(exp)
        if not pdir or not pdir.exists():
            return jsonify({'error': 'not found'}), 404
        return send_from_directory(str(pdir.resolve()), fname)

    @app.route('/api/exp/<name>/summary')
    def exp_summary(name):
        """Return available aggregate CSV data (if present) and list of files for the experiment.
        This endpoint does NOT run or regenerate experiments; it only reports already-generated artifacts.
        """
        # use mapped plots dir if present
        plots_dir = exp_map.get(name)
        if plots_dir is None:
            expdir = root_dir / name
            if not expdir.exists():
                return jsonify({'error': 'not found'}), 404
            plots_dir = expdir / 'plots'

        # look for known aggregate CSV files and return if found
        for fn in ('aggregated_summary.csv', 'exp4_agg.csv', 'exp5_agg.csv', 'exp2_time_agg.csv'):
            p = plots_dir / fn
            if p.exists():
                try:
                    df = pd.read_csv(p)
                    return jsonify({'aggregate': fn, 'data': df.to_dict(orient='records')})
                except Exception as e:
                    logging.warning(f"Failed to read aggregate {p}: {e}")

        # No aggregate CSV found — return list of files and plots
        files = []
        # try listing files from parent experiment dir if available
        parent_dir = plots_dir.parent if plots_dir else None
        if parent_dir and parent_dir.exists():
            for x in parent_dir.iterdir():
                if x.is_file():
                    files.append(x.name)

        plots = []
        if plots_dir and plots_dir.exists():
            for f in sorted(plots_dir.iterdir()):
                if f.is_file() and f.suffix.lower() in ('.png', '.svg'):
                    plots.append(f.name)
        return jsonify({'files': files, 'plots': plots})

    @app.route('/api/exp/<name>/anomalies')
    def exp_anomalies(name):
        expdir = root_dir / name
        results = []
        for p in expdir.rglob('*.anomalies.jsonl'):
            try:
                with p.open('r', encoding='utf-8') as fh:
                    for line in fh:
                        line = line.strip()
                        if not line or line.startswith('#'): continue
                        try:
                            results.append(json.loads(line))
                        except Exception:
                            try:
                                results.append(eval(line))
                            except Exception:
                                continue
            except Exception:
                continue
        return jsonify(results)

    # The server intentionally does NOT provide endpoints to start experiments.
    # It only serves already-generated plots and CSVs under the experiments directory.

    logging.info(f"Starting Flask server on {host}:{port}, serving {root_dir}")
    app.run(host=host, port=port, debug=False)


def summarize_and_plot_exp2(experiment_dir: Path, out_dir: Path, save_formats=('png',)):
    csvp = experiment_dir / 'experiment2_results.csv'
    logging.info(f"Loading Experiment 2 CSV: {csvp}")
    try:
        df = pd.read_csv(csvp)
    except Exception:
        # Fallback parser: some combos contain commas and were not quoted.
        # Reconstruct rows by taking the last 8 fields as fixed columns
        rows = []
        with open(csvp, 'r', encoding='utf-8') as fh:
            header = fh.readline()
            for line in fh:
                line = line.strip()
                if not line:
                    continue
                parts = line.split(',')
                if len(parts) < 9:
                    continue
                # last 8 fields are: trial,time_us,pid_count,net_links,mounts,isol_pid,isol_net,isol_mnt
                combo = ','.join(parts[0:len(parts)-8])
                tail = parts[-8:]
                row = [combo] + tail
                rows.append(row)
        cols = ['combo','trial','time_us','pid_count','net_links','mounts','isol_pid','isol_net','isol_mnt']
        df = pd.DataFrame(rows, columns=cols)
    df.columns = [c.strip().lower() for c in df.columns]
    if 'time_us' in df.columns:
        df['time_us'] = pd.to_numeric(df['time_us'], errors='coerce')

    ensure_out_dir(out_dir)
    # Boxplot of creation time by combo
    try:
        fig, ax = plt.subplots(figsize=(10,5))
        sns.boxplot(x='combo', y='time_us', data=df, ax=ax)
        ax.set_title('Namespace creation time by combo (us)')
        ax.set_ylabel('time (us)')
        for fmt in save_formats:
            p = out_dir / f"exp2_time_box.{fmt}"
            fig.savefig(p, bbox_inches='tight')
            logging.info(f"Saved {p}")
        plt.close(fig)
    except Exception as e:
        logging.warning(f"Failed to plot exp2 boxplot: {e}")

    # Mean times bar
    try:
        agg = df.groupby('combo').time_us.agg(['mean','median','std','count']).reset_index()
        agg.to_csv(out_dir / 'exp2_time_agg.csv', index=False)
        fig, ax = plt.subplots()
        ax.bar(agg['combo'], agg['mean'])
        ax.set_xlabel('combo')
        ax.set_ylabel('mean time (us)')
        ax.set_title('Mean namespace creation time')
        for fmt in save_formats:
            p = out_dir / f"exp2_time_mean.{fmt}"
            fig.savefig(p, bbox_inches='tight')
            logging.info(f"Saved {p}")
        plt.close(fig)
    except Exception as e:
        logging.warning(f"Failed to create exp2 mean plot: {e}")

    # Isolation success heatmap per namespace type
    try:
        flags = ['isol_pid','isol_net','isol_mnt']
        heat = {}
        truthy = set(['yes', 'true', '1', 'y', 't'])
        for c in flags:
            if c in df.columns:
                heat[c] = df.groupby('combo')[c].apply(lambda s: s.astype(str).str.lower().map(lambda v: 1 if v in truthy else 0).sum())
        if heat:
            heat_df = pd.DataFrame(heat).fillna(0).astype(int)
            heat_df.to_csv(out_dir / 'exp2_isolation_heat.csv')
            fig, ax = plt.subplots(figsize=(8,4))
            sns.heatmap(heat_df, annot=True, fmt='d', cmap='Blues', ax=ax)
            ax.set_title('Isolation success counts per combo')
            for fmt in save_formats:
                p = out_dir / f"exp2_isolation_heatmap.{fmt}"
                fig.savefig(p, bbox_inches='tight')
                logging.info(f"Saved {p}")
            plt.close(fig)
    except Exception as e:
        logging.warning(f"Failed to create exp2 isolation heatmap: {e}")

    logging.info(f'Experiment 2 results saved to {out_dir}')


def summarize_and_plot_exp3(experiment_dir: Path, out_dir: Path, save_formats=('png',)):
    csvp = experiment_dir / 'exp3_results.csv'
    logging.info(f"Loading Experiment 3 CSV: {csvp}")
    df = pd.read_csv(csvp)
    df.columns = [c.strip().lower() for c in df.columns]
    for c in ['limit','limit_cores','max_cpu_pct','measured_cpu_pct','throughput_iters']:
        if c in df.columns:
            df[c] = pd.to_numeric(df[c], errors='coerce')

    ensure_out_dir(out_dir)
    try:
        agg = df.groupby('limit_cores').agg(
            trials=('trial','count'),
            mean_measured_pct=('measured_cpu_pct','mean'),
            std_measured_pct=('measured_cpu_pct','std'),
            mean_throughput=('throughput_iters','mean')
        ).reset_index()
        # Save aggregate
        agg.to_csv(out_dir / 'exp3_agg.csv', index=False)

        # measured vs configured
        fig, ax = plt.subplots()
        ax.errorbar(agg['limit_cores'], agg['mean_measured_pct'], yerr=agg['std_measured_pct'], fmt='o-', capsize=5)
        ax.set_xlabel('configured cores (limit_cores)')
        ax.set_ylabel('measured CPU %')
        ax.set_title('Configured cores vs measured CPU%')
        for fmt in save_formats:
            p = out_dir / f"exp3_measured_vs_config.{fmt}"
            fig.savefig(p, bbox_inches='tight')
            logging.info(f"Saved {p}")
        plt.close(fig)

        # throughput plot
        fig, ax = plt.subplots()
        ax.plot(agg['limit_cores'], agg['mean_throughput'], 'o-')
        ax.set_xlabel('configured cores')
        ax.set_ylabel('mean throughput (iters)')
        ax.set_title('Throughput vs configured cores')
        for fmt in save_formats:
            p = out_dir / f"exp3_throughput.{fmt}"
            fig.savefig(p, bbox_inches='tight')
            logging.info(f"Saved {p}")
        plt.close(fig)
    except Exception as e:
        logging.warning(f"Failed to summarize/plot exp3 data: {e}")

    logging.info(f'Experiment 3 results saved to {out_dir}')


def summarize_and_plot_exp4(experiment_dir: Path, out_dir: Path, save_formats=('png',)):
    csvp = experiment_dir / 'exp4_results.csv'
    logging.info(f"Loading Experiment 4 CSV: {csvp}")
    df = pd.read_csv(csvp)
    df.columns = [c.strip().lower() for c in df.columns]
    for c in ['limit_bytes','max_alloc_bytes','failcnt','oom_kills']:
        if c in df.columns:
            df[c] = pd.to_numeric(df[c], errors='coerce')

    # Attempt to recover missing max_alloc_bytes from per-trial logfiles before aggregation
    if 'max_alloc_bytes' in df.columns and df['max_alloc_bytes'].isna().any() and 'logfile' in df.columns:
        def extract_max_from_log(logpath):
            try:
                p = Path(str(logpath))
                if not p.exists():
                    return None
                max_v = None
                with p.open('r', encoding='utf-8', errors='ignore') as fh:
                    for line in fh:
                        if 'MAX_ALLOC:' in line:
                            try:
                                digs = ''.join(ch for ch in line if ch.isdigit())
                                if digs:
                                    max_v = int(digs)
                            except Exception:
                                pass
                        elif 'ALLOC:' in line:
                            try:
                                digs = ''.join(ch for ch in line if ch.isdigit())
                                if digs:
                                    max_v = int(digs)
                            except Exception:
                                pass
                return max_v
            except Exception:
                return None

        for idx, row in df[df['max_alloc_bytes'].isna()].iterrows():
            lf = row.get('logfile')
            if pd.isna(lf) or not lf:
                continue
            lf_str = str(lf).strip('"')
            val = extract_max_from_log(lf_str)
            if val is not None:
                df.at[idx, 'max_alloc_bytes'] = val

    ensure_out_dir(out_dir)
    try:
        agg = df.groupby('limit_bytes').agg(
            trials=('trial','count'),
            mean_max_alloc=('max_alloc_bytes','mean'),
            std_max_alloc=('max_alloc_bytes','std'),
            total_failcnt=('failcnt','sum'),
            total_oom=('oom_kills','sum')
        ).reset_index()
        agg.to_csv(out_dir / 'exp4_agg.csv', index=False)

        # plot max allocated (use categorical x positions for consistent spacing)
        fig, ax = plt.subplots()
        x = np.arange(len(agg))
        ax.bar(x, agg['mean_max_alloc'] / (1024*1024))
        ax.set_xticks(x)
        ax.set_xticklabels((agg['limit_bytes'] / (1024*1024)).astype(int).astype(str))
        ax.set_xlabel('limit (MB)')
        ax.set_ylabel('mean max allocated (MB)')
        ax.set_title('Memory: configured limit vs mean max allocated')
        for fmt in save_formats:
            p = out_dir / f"exp4_maxalloc.{fmt}"
            fig.savefig(p, bbox_inches='tight')
            logging.info(f"Saved {p}")
        plt.close(fig)

        # plot failcounts and oom (side-by-side bars)
        fig, ax = plt.subplots()
        x = np.arange(len(agg))
        width = 0.35
        ax.bar(x - width/2, agg['total_failcnt'], width=width, label='failcnt')
        ax.bar(x + width/2, agg['total_oom'], width=width, alpha=0.7, label='oom_kills')
        ax.set_xticks(x)
        ax.set_xticklabels((agg['limit_bytes'] / (1024*1024)).astype(int).astype(str))
        ax.set_xlabel('limit (MB)')
        ax.set_ylabel('counts')
        ax.set_title('Memory failcnt / OOM kills')
        ax.legend()
        for fmt in save_formats:
            p = out_dir / f"exp4_failures.{fmt}"
            fig.savefig(p, bbox_inches='tight')
            logging.info(f"Saved {p}")
        plt.close(fig)
    except Exception as e:
        logging.warning(f"Failed to summarize/plot exp4 data: {e}")

    logging.info(f'Experiment 4 results saved to {out_dir}')


def summarize_and_plot_exp5(experiment_dir: Path, out_dir: Path, save_formats=('png',)):
    csvp = experiment_dir / 'exp5_results.csv'
    logging.info(f"Loading Experiment 5 CSV: {csvp}")
    df = pd.read_csv(csvp)
    df.columns = [c.strip().lower() for c in df.columns]
    for c in ['limit_bps','measured_bytes','measured_bps','avg_write_latency_us','run_time_s']:
        if c in df.columns:
            df[c] = pd.to_numeric(df[c], errors='coerce')

    ensure_out_dir(out_dir)
    try:
        agg = df.groupby('limit_bps').agg(
            trials=('trial','count'),
            mean_measured_bps=('measured_bps','mean'),
            std_measured_bps=('measured_bps','std'),
            mean_latency_us=('avg_write_latency_us','mean')
        ).reset_index()
        agg.to_csv(out_dir / 'exp5_agg.csv', index=False)

        # measured vs config - plot against categorical x positions to avoid huge x spacing
        fig, ax = plt.subplots()
        x = np.arange(len(agg))
        ax.errorbar(x, agg['mean_measured_bps'], yerr=agg['std_measured_bps'], fmt='o-', capsize=5)
        ax.set_xticks(x)
        ax.set_xticklabels(agg['limit_bps'].astype(int).astype(str))
        ax.set_xlabel('configured bps')
        ax.set_ylabel('measured bytes/sec')
        ax.set_title('I/O: configured vs measured throughput')
        for fmt in save_formats:
            p = out_dir / f"exp5_measured_vs_config.{fmt}"
            fig.savefig(p, bbox_inches='tight')
            logging.info(f"Saved {p}")
        plt.close(fig)

        # throughput ratio - avoid division by zero, plot categorically
        agg['ratio'] = np.where(agg['limit_bps'] > 0, agg['mean_measured_bps'] / agg['limit_bps'], np.nan)
        fig, ax = plt.subplots()
        x = np.arange(len(agg))
        ax.bar(x, agg['ratio'])
        ax.set_xticks(x)
        ax.set_xticklabels(agg['limit_bps'].astype(int).astype(str))
        ax.set_xlabel('configured bps')
        ax.set_ylabel('measured/configured')
        ax.set_title('Measured/Configured throughput ratio')
        for fmt in save_formats:
            p = out_dir / f"exp5_ratio.{fmt}"
            fig.savefig(p, bbox_inches='tight')
            logging.info(f"Saved {p}")
        plt.close(fig)
    except Exception as e:
        logging.warning(f"Failed to summarize/plot exp5 data: {e}")

    logging.info(f'Experiment 5 results saved to {out_dir}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Visualize experiment overhead results')
    parser.add_argument('--dir', '-d', type=str, default='out/experiments', help='Experiment directory containing overhead_summary.csv')
    parser.add_argument('--out', '-o', type=str, default=None, help='Output directory for plots (defaults to <dir>/plots)')
    parser.add_argument('--formats', type=str, default='png', help='Comma-separated image formats (png,svg)')
    parser.add_argument('--show', action='store_true', help='Show plots interactively (requires GUI)')
    parser.add_argument('--serve', action='store_true', help='Start a Flask server to browse experiments')
    parser.add_argument('--host', type=str, default='127.0.0.1', help='Host for Flask server')
    parser.add_argument('--port', type=int, default=5000, help='Port for Flask server')

    args = parser.parse_args()
    exp_dir = Path(args.dir)
    if args.out:
        out_dir = Path(args.out)
    else:
        out_dir = exp_dir / 'plots'
    formats = tuple([s.strip() for s in args.formats.split(',') if s.strip()])

    ensure_out_dir(out_dir)
    if args.serve:
        # run server which will call summarize_on_demand
        start_flask_server(exp_dir, host=args.host, port=args.port)
        sys.exit(0)
    else:
        summarize_and_plot(exp_dir, out_dir, save_formats=formats)

    if args.show:
        # open the saved images if possible
        try:
            import webbrowser
            for p in sorted(out_dir.iterdir()):
                if p.suffix.lower() in ('.png', '.svg'):
                    webbrowser.open(str(p.resolve()))
        except Exception as e:
            logging.warning(f'Failed to open plots: {e}')
