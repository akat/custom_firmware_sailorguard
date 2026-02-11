import { formatUptime } from "../utils/format.js";
import SignalKWidget from "../components/SignalKWidget.jsx";

export default function DashboardView({ status, error }) {
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
      `}</style>
    </div>
  );
}
