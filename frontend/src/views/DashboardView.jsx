import { useState, useEffect } from "preact/hooks";
import { formatUptime } from "../utils/format.js";
import SignalKWidget from "../components/SignalKWidget.jsx";

function formatValue(sub) {
  if (sub.value === null || sub.value === undefined) return "--";
  if (sub.type === "position") {
    return `${sub.value.latitude.toFixed(6)}, ${sub.value.longitude.toFixed(6)}`;
  }
  if (sub.type === "float") return Number(sub.value).toFixed(4);
  return String(sub.value);
}

function pathLabel(path) {
  const parts = path.split(".");
  return parts[parts.length - 1];
}

export default function DashboardView({ status, error }) {
  const [subs, setSubs] = useState([]);

  useEffect(() => {
    const load = async () => {
      try {
        const r = await fetch("/api/signalk/subscriptions");
        if (r.ok) setSubs(await r.json());
      } catch (_) {}
    };
    load();
    const t = setInterval(load, 2000);
    return () => clearInterval(t);
  }, []);

  return (
    <div class="dashboard-container">
      <section class="card">
        <h2>System</h2>
        {error && <p class="error">{error}</p>}
        {!status && !error && <p class="loading">Loading...</p>}
        {status && (
          <div class="grid">
            <div class="item">
              <span class="label">Firmware Version</span>
              <span class="value">{status.firmwareVersion}</span>
            </div>
            <div class="item">
              <span class="label">Uptime</span>
              <span class="value">{formatUptime(status.uptimeMs)}</span>
            </div>
            <div class="item">
              <span class="label">Last Reset</span>
              <span class="value">{status.lastReset}</span>
            </div>
            <div class="item">
              <span class="label">Free Memory</span>
              <span class="value">{(status.freeMemory / 1024).toFixed(1)} KB</span>
            </div>
            <div class="item">
              <span class="label">IP Address</span>
              <span class="value">{status.ipAddress}</span>
            </div>
          </div>
        )}
      </section>

      <section class="card">
        <h2>Connections</h2>
        <div class="connections-grid">
          <SignalKWidget />
        </div>
      </section>

      {subs.length > 0 && (
        <section class="card">
          <h2>Signal K Data</h2>
          <div class="sk-data-list">
            {subs.map((s) => (
              <div class="sk-data-row" key={s.path}>
                <div class="sk-data-path">
                  <span class="sk-data-label">{pathLabel(s.path)}</span>
                  <span class="sk-data-fullpath">{s.path}</span>
                </div>
                <div class="sk-data-value-wrap">
                  <span class={`sk-data-value ${s.value !== null && s.value !== undefined ? "sk-has-value" : ""}`}>
                    {formatValue(s)}
                  </span>
                  {s.source && <span class="sk-data-source">{s.source}</span>}
                </div>
              </div>
            ))}
          </div>
        </section>
      )}

      <style>{`
        .dashboard-container {
          display: grid;
          gap: 20px;
        }

        .connections-grid {
          display: grid;
          grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
          gap: 15px;
        }

        .sk-data-list {
          display: flex;
          flex-direction: column;
          gap: 0;
        }

        .sk-data-row {
          display: flex;
          justify-content: space-between;
          align-items: center;
          padding: 12px 0;
          border-bottom: 1px solid #eee;
        }

        .sk-data-row:last-child {
          border-bottom: none;
        }

        .sk-data-path {
          display: flex;
          flex-direction: column;
          gap: 2px;
          min-width: 0;
        }

        .sk-data-label {
          font-weight: 600;
          color: var(--ink);
          font-size: 0.95em;
        }

        .sk-data-fullpath {
          font-size: 0.75em;
          color: var(--muted);
          font-family: monospace;
        }

        .sk-data-value-wrap {
          text-align: right;
          display: flex;
          flex-direction: column;
          gap: 2px;
          flex-shrink: 0;
          margin-left: 16px;
        }

        .sk-data-value {
          font-size: 1.1em;
          font-weight: 600;
          color: var(--muted);
          font-family: monospace;
        }

        .sk-data-value.sk-has-value {
          color: var(--accent);
        }

        .sk-data-source {
          font-size: 0.7em;
          color: var(--muted);
        }
      `}</style>
    </div>
  );
}
