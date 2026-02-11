import { formatUptime } from "../utils/format.js";

export default function DashboardView({ status, error }) {
  return (
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
  );
}
