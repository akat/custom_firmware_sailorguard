import { useState, useEffect } from "preact/hooks";

export default function SignalKView() {
  const [config, setConfig] = useState({
    enabled: false,
    auto_discovery: true,
    transport_mode: "both",
    hostname: "",
    port: 3000,
    use_ssl: false,
    udp_target_ip: "255.255.255.255",
    udp_broadcast_port: 5555,
    udp_listen_port: 5556,
    vessel_name: "SailorGuard"
  });

  const [status, setStatus] = useState({
    state: 0,
    state_name: "DISCONNECTED",
    authenticated: false,
    uptime_seconds: 0,
    messages_sent: 0,
    messages_received: 0
  });

  const [discoveredServers, setDiscoveredServers] = useState([]);
  const [isDiscovering, setIsDiscovering] = useState(false);
  const [saveMessage, setSaveMessage] = useState("");
  const [errorMessage, setErrorMessage] = useState("");

  // Send data states
  const [sendPath, setSendPath] = useState("navigation.sailorguard.anchor");
  const [sendValue, setSendValue] = useState("");
  const [isSending, setIsSending] = useState(false);

  // Load config and status on mount
  useEffect(() => {
    loadConfig();
    loadStatus();
    const timer = setInterval(loadStatus, 2000);
    return () => clearInterval(timer);
  }, []);

  const loadConfig = async () => {
    try {
      const response = await fetch("/api/signalk/config", { cache: "no-store" });
      const data = await response.json();
      setConfig(data);
    } catch (error) {
      console.error("Failed to load config:", error);
    }
  };

  const loadStatus = async () => {
    try {
      const response = await fetch("/api/signalk/status");
      const data = await response.json();
      setStatus(data);
    } catch (error) {
      console.error("Failed to load status:", error);
    }
  };

  const saveConfig = async () => {
    try {
      setErrorMessage("");
      const response = await fetch("/api/signalk/config", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(config)
      });

      if (response.ok) {
        setSaveMessage("Configuration saved!");
        setTimeout(() => setSaveMessage(""), 3000);
        loadConfig();
      } else {
        setErrorMessage("Failed to save configuration");
      }
    } catch (error) {
      setErrorMessage("Error: " + error.message);
    }
  };

  const discoverServers = async () => {
    setIsDiscovering(true);
    setErrorMessage("");
    try {
      await fetch("/api/signalk/discover", { method: "POST" });
      // Wait a bit for discovery to complete
      await new Promise(resolve => setTimeout(resolve, 3000));
      const response = await fetch("/api/signalk/servers");
      const data = await response.json();
      setDiscoveredServers(data);
    } catch (error) {
      console.error("Discovery failed:", error);
      setErrorMessage("Discovery failed: " + error.message);
    } finally {
      setIsDiscovering(false);
    }
  };

  const selectServer = (server) => {
    setConfig({
      ...config,
      hostname: server.hostname,
      port: server.port,
      use_ssl: server.ssl
    });
  };

  const handleConnect = async () => {
    try {
      setErrorMessage("");
      const response = await fetch("/api/signalk/connect", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          hostname: config.hostname,
          port: config.port
        })
      });

      if (response.ok) {
        setSaveMessage("Connecting...");
        setTimeout(() => setSaveMessage(""), 3000);
      } else {
        setErrorMessage("Connection failed");
      }
    } catch (error) {
      setErrorMessage("Error: " + error.message);
    }
  };

  const handleDisconnect = async () => {
    try {
      setErrorMessage("");
      await fetch("/api/signalk/disconnect", { method: "POST" });
      setSaveMessage("Disconnected");
      setTimeout(() => setSaveMessage(""), 3000);
    } catch (error) {
      setErrorMessage("Error: " + error.message);
    }
  };

  const clearToken = async () => {
    try {
      setErrorMessage("");
      const response = await fetch("/api/signalk/clear-token", { method: "POST" });
      const data = await response.json();
      
      if (data.success) {
        setSaveMessage("Token cleared! Reconnect to authenticate again.");
        setTimeout(() => setSaveMessage(""), 3000);
        loadConfig();
        loadStatus();
      } else {
        setErrorMessage("Failed to clear token: " + (data.error || "Unknown error"));
      }
    } catch (error) {
      setErrorMessage("Error: " + error.message);
    }
  };

  const sendData = async () => {
    if (!sendPath || sendValue === "") {
      setErrorMessage("Please enter both path and value");
      return;
    }

    if (status.state_name !== "STREAMING" && status.state_name !== "CONNECTED") {
      setErrorMessage("Not connected to Signal K server. Please connect first.");
      return;
    }

    setIsSending(true);
    try {
      setErrorMessage("");
      
      // Parse the value to the appropriate type
      let parsedValue;
      if (sendValue === "true") {
        parsedValue = true;
      } else if (sendValue === "false") {
        parsedValue = false;
      } else if (!isNaN(sendValue) && sendValue !== "") {
        // Try to parse as number
        parsedValue = parseFloat(sendValue);
      } else {
        // Keep as string
        parsedValue = sendValue;
      }

      const response = await fetch("/api/signalk/send", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          path: sendPath,
          value: parsedValue,
          source: "sailorguard"
        })
      });

      const result = await response.json();

      if (result.ok) {
        setSaveMessage(`Successfully sent ${sendPath} = ${sendValue}`);
        setSendValue("");
        setTimeout(() => setSaveMessage(""), 3000);
      } else {
        setErrorMessage("Failed to send data: " + (result.error || "Unknown error"));
      }
    } catch (error) {
      setErrorMessage("Error: " + error.message);
    } finally {
      setIsSending(false);
    }
  };

  return (
    <section class="card">
      <div class="card-header">
        <h2>Signal K Connection</h2>
      </div>

      {/* Status Section */}
      <fieldset class="config-section">
        <legend>Connection Status</legend>
        <div class="grid">
          <div class="item">
            <span class="label">State</span>
            <span class="value">{status.state_name}</span>
          </div>
          <div class="item">
            <span class="label">Authenticated</span>
            <span class="value">{status.authenticated ? "Yes" : "No"}</span>
          </div>
          <div class="item">
            <span class="label">Uptime</span>
            <span class="value">{Math.floor(status.uptime_seconds / 60)}m {status.uptime_seconds % 60}s</span>
          </div>
          <div class="item">
            <span class="label">Messages</span>
            <span class="value">{status.messages_sent} sent, {status.messages_received} received</span>
          </div>
        </div>

        {status.state_name === "AUTH_PENDING" && (
          <p class="message">Waiting for approval from Signal K server…</p>
        )}

        {status.server && status.server.hostname && (
          <div style={{ marginTop: "15px", paddingTop: "15px", borderTop: "1px solid rgba(0,0,0,0.1)" }}>
            <div class="item">
              <span class="label">Connected Server</span>
              <span class="value">
                {status.server.hostname}:{status.server.port}
                {status.server.ssl && " (SSL)"}
              </span>
            </div>
          </div>
        )}
      </fieldset>

      {/* Configuration Section */}
      <form onSubmit={(e) => { e.preventDefault(); saveConfig(); }}>
        <fieldset class="config-section">
          <legend>General Settings</legend>

          <label class="field toggle-row" for="sk-enabled">
            <input
              id="sk-enabled"
              type="checkbox"
              checked={config.enabled}
              onChange={(e) => setConfig({ ...config, enabled: e.target.checked })}
            />
            <span>Enable Signal K</span>
          </label>

          <label class="field" for="sk-vessel">
            <span>Vessel Name</span>
            <input
              id="sk-vessel"
              class="input"
              type="text"
              value={config.vessel_name}
              onInput={(e) => setConfig({ ...config, vessel_name: e.target.value })}
              placeholder="SailorGuard"
            />
          </label>
        </fieldset>

        <fieldset class="config-section">
          <legend>Server Settings</legend>

          <label class="field" for="sk-transport">
            <span>Transport Mode</span>
            <select
              id="sk-transport"
              class="input"
              value={config.transport_mode}
              onChange={(e) => setConfig({ ...config, transport_mode: e.target.value })}
            >
              <option value="ws">WebSocket only</option>
              <option value="udp">UDP only</option>
              <option value="both">WebSocket + UDP fallback</option>
            </select>
          </label>

          <label class="field toggle-row" for="sk-autodiscovery">
            <input
              id="sk-autodiscovery"
              type="checkbox"
              checked={config.auto_discovery}
              onChange={(e) => setConfig({ ...config, auto_discovery: e.target.checked })}
            />
            <span>Auto-Discovery (mDNS)</span>
          </label>

          {config.auto_discovery && (
            <div style={{ marginBottom: "15px" }}>
              <button
                type="button"
                onClick={discoverServers}
                disabled={isDiscovering}
                class="button secondary"
              >
                {isDiscovering ? "Discovering..." : "Discover Servers"}
              </button>

              {discoveredServers.length > 0 && (
                <div style={{ marginTop: "15px" }}>
                  <p class="hint">Available Servers:</p>
                  {discoveredServers.map((server) => (
                    <div
                      key={server.ip}
                      style={{
                        display: "flex",
                        justifyContent: "space-between",
                        alignItems: "center",
                        padding: "10px",
                        marginBottom: "8px",
                        backgroundColor: "#f5f5f5",
                        borderRadius: "4px"
                      }}
                    >
                      <div>
                        <p style={{ margin: "0 0 4px 0", fontWeight: "bold" }}>{server.name}</p>
                        <p style={{ margin: 0, fontSize: "0.85em", color: "#666" }}>
                          {server.ip}:{server.port}
                        </p>
                      </div>
                      <button
                        type="button"
                        onClick={() => selectServer(server)}
                        class="button secondary"
                        style={{ marginLeft: "10px", whiteSpace: "nowrap" }}
                      >
                        Select
                      </button>
                    </div>
                  ))}
                </div>
              )}
            </div>
          )}

          <label class="field" for="sk-hostname">
            <span>Hostname / IP</span>
            <input
              id="sk-hostname"
              class="input"
              type="text"
              value={config.hostname}
              onInput={(e) => setConfig({ ...config, hostname: e.target.value })}
              placeholder="vessel.local or 192.168.1.100"
            />
          </label>

          <label class="field" for="sk-port">
            <span>Port</span>
            <input
              id="sk-port"
              class="input"
              type="number"
              value={config.port}
              onInput={(e) => setConfig({ ...config, port: parseInt(e.target.value) })}
              min="1"
              max="65535"
            />
          </label>

          <label class="field toggle-row" for="sk-ssl">
            <input
              id="sk-ssl"
              type="checkbox"
              checked={config.use_ssl}
              onChange={(e) => setConfig({ ...config, use_ssl: e.target.checked })}
            />
            <span>Use SSL/TLS</span>
          </label>

          {(config.transport_mode === "udp" || config.transport_mode === "both") && (
            <>
              <label class="field" for="sk-udp-ip">
                <span>UDP Target IP</span>
                <input
                  id="sk-udp-ip"
                  class="input"
                  type="text"
                  value={config.udp_target_ip}
                  onInput={(e) => setConfig({ ...config, udp_target_ip: e.target.value })}
                  placeholder="255.255.255.255"
                />
              </label>

              <label class="field" for="sk-udp-bcast">
                <span>UDP Broadcast Port</span>
                <input
                  id="sk-udp-bcast"
                  class="input"
                  type="number"
                  value={config.udp_broadcast_port}
                  onInput={(e) => setConfig({ ...config, udp_broadcast_port: parseInt(e.target.value) })}
                  min="1"
                  max="65535"
                />
              </label>

              <label class="field" for="sk-udp-listen">
                <span>UDP Listen Port</span>
                <input
                  id="sk-udp-listen"
                  class="input"
                  type="number"
                  value={config.udp_listen_port}
                  onInput={(e) => setConfig({ ...config, udp_listen_port: parseInt(e.target.value) })}
                  min="1"
                  max="65535"
                />
              </label>
            </>
          )}
        </fieldset>

        <fieldset class="config-section">
          <legend>Authentication</legend>
          <p class="section-description">
            Authentication is automatic. When enabled, the device will request access
            from the Signal K server and save the token after approval.
          </p>
          <button type="button" onClick={clearToken} class="button secondary" style={{ marginTop: "10px" }}>
            Clear Token
          </button>
          <p class="section-description" style={{ fontSize: "0.85em", marginTop: "5px", color: "#666" }}>
            Use this to restart the authentication process if needed.
          </p>
        </fieldset>

        <fieldset class="config-section">
          <legend>Send Data to Signal K</legend>
          <p class="section-description">
            Send values to the Signal K server using the WebSocket connection. The device must be connected first.
          </p>
          
          <label class="field" for="sk-send-path">
            <span>SignalK Path</span>
            <input
              id="sk-send-path"
              class="input"
              type="text"
              value={sendPath}
              onInput={(e) => setSendPath(e.target.value)}
              placeholder="e.g., navigation.sailorguard.anchor"
            />
          </label>

          <label class="field" for="sk-send-value">
            <span>Value</span>
            <input
              id="sk-send-value"
              class="input"
              type="text"
              value={sendValue}
              onInput={(e) => setSendValue(e.target.value)}
              placeholder="e.g., 45.5 or true or text value"
            />
          </label>

          <button 
            type="button" 
            onClick={sendData} 
            disabled={isSending || status.state_name !== "STREAMING" && status.state_name !== "CONNECTED"}
            class="button primary" 
            style={{ marginTop: "10px", width: "100%" }}
          >
            {isSending ? "Sending..." : "Send Data"}
          </button>
          <p class="section-description" style={{ fontSize: "0.85em", marginTop: "5px", color: "#666" }}>
            Values are automatically parsed as numbers (integers/floats), booleans (true/false), or strings.
          </p>
        </fieldset>

        <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr 1fr", gap: "10px", marginTop: "10px", marginBottom: "15px" }}>
          <button type="submit" class="button primary">
            Save Configuration
          </button>
          <button type="button" onClick={handleConnect} class="button primary">
            Connect
          </button>
          <button type="button" onClick={handleDisconnect} class="button secondary">
            Disconnect
          </button>
        </div>

        {saveMessage && <p class="message">{saveMessage}</p>}
        {errorMessage && <p class="error">{errorMessage}</p>}
      </form>
    </section>
  );
}
