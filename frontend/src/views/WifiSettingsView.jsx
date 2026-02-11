export default function WifiSettingsView({
  networks,
  wifiStatus,
  wifiError,
  wifiMessage,
  wifiForm,
  apForm,
  apMessage,
  apError,
  onScan,
  onSubmit,
  onFormChange,
  onSelectNetwork,
  onApFormChange,
  onApSubmit
}) {
  return (
    <section class="card">
      <div class="card-header">
        <h2>Wi-Fi Settings</h2>
        <button class="button secondary" onClick={onScan} type="button">
          Scan
        </button>
      </div>
      <div class="wifi-grid">
        <form class="wifi-form" onSubmit={onSubmit}>
          <h3>Connect to Network</h3>
          <label class="field">
            <span>SSID</span>
            <input
              class="input"
              value={wifiForm.ssid}
              onInput={(event) => onFormChange({ ssid: event.target.value })}
              placeholder="Network name"
              required
            />
          </label>
          <label class="field">
            <span>Password</span>
            <input
              class="input"
              type="password"
              value={wifiForm.password}
              onInput={(event) => onFormChange({ password: event.target.value })}
              placeholder="Leave empty for open networks"
            />
          </label>
          <button class="button primary" type="submit">
            Save & Connect
          </button>
          {wifiMessage && <p class="message">{wifiMessage}</p>}
          {wifiError && <p class="error">{wifiError}</p>}
        </form>
        <div class="wifi-list">
          <div class="list-header">
            <h3>Available Networks</h3>
            <span class="hint">Tap to fill SSID</span>
          </div>
          {networks.length === 0 && <p class="muted">No scan results yet.</p>}
          {networks.map((network) => (
            <button
              key={`${network.ssid}-${network.rssi}`}
              class="wifi-card"
              type="button"
              onClick={() => onSelectNetwork(network.ssid)}
            >
              <div>
                <p class="wifi-ssid">{network.ssid || "Hidden"}</p>
                <p class="wifi-meta">
                  RSSI {network.rssi} dBm · {network.auth}
                </p>
              </div>
              <span class="tag">Select</span>
            </button>
          ))}
        </div>
      </div>

      <div class="wifi-status">
        <h3>Status</h3>
        <div class="status-item">
          <span class="label">STA Status</span>
          <span class="value">
            {wifiStatus?.staConnected ? "Connected" : "Disconnected"}
          </span>
        </div>
        <div class="status-item">
          <span class="label">STA SSID</span>
          <span class="value">{wifiStatus?.staSsid || "-"}</span>
        </div>
        <div class="status-item">
          <span class="label">STA IP</span>
          <span class="value">{wifiStatus?.staIp || "-"}</span>
        </div>
        <div class="status-item">
          <span class="label">SoftAP</span>
          <span class="value">
            {wifiStatus?.apStarted ? `On (${wifiStatus.apSsid})` : "Off"}
          </span>
        </div>
      </div>

      <form class="wifi-form ap-form" onSubmit={onApSubmit}>
        <h3>SoftAP Settings</h3>
        <p class="hint-text">Configure the Access Point.</p>
        <label class="field">
          <span>SoftAP SSID</span>
          <input
            class="input"
            value={apForm.ssid}
            onInput={(event) => onApFormChange({ ssid: event.target.value })}
            placeholder="Access point name"
            required
            maxLength="32"
          />
        </label>
        <label class="field">
          <span>SoftAP Password</span>
          <input
            class="input"
            type="password"
            value={apForm.password}
            onInput={(event) => onApFormChange({ password: event.target.value })}
            placeholder="At least 8 characters"
            required
            minLength="8"
            maxLength="64"
          />
        </label>
        <button class="button primary" type="submit">
          Update SoftAP
        </button>
        {apMessage && <p class="message">{apMessage}</p>}
        {apError && <p class="error">{apError}</p>}
      </form>
    </section>
  );
}
