import { useState, useEffect } from "preact/hooks";

export default function DeviceView() {
  const [info, setInfo] = useState(null);
  const [updateInfo, setUpdateInfo] = useState(null);
  const [autoUpdate, setAutoUpdate] = useState(false);
  const [checking, setChecking] = useState(false);
  const [updating, setUpdating] = useState(false);
  const [rebooting, setRebooting] = useState(false);
  const [message, setMessage] = useState("");
  const [error, setError] = useState("");

  useEffect(() => {
    loadInfo();
    loadAutoUpdate();
    const timer = setInterval(loadInfo, 5000);
    return () => clearInterval(timer);
  }, []);

  const loadInfo = async () => {
    try {
      const response = await fetch("/api/device/info", { cache: "no-store" });
      if (response.ok) {
        setInfo(await response.json());
      }
    } catch (err) {
      console.error("Failed to load device info:", err);
    }
  };

  const loadAutoUpdate = async () => {
    try {
      const response = await fetch("/api/device/auto-update", { cache: "no-store" });
      if (response.ok) {
        const data = await response.json();
        setAutoUpdate(data.enabled);
      }
    } catch (err) {
      console.error("Failed to load auto-update setting:", err);
    }
  };

  const checkForUpdates = async () => {
    setChecking(true);
    setError("");
    setMessage("");
    try {
      const response = await fetch("/api/device/update-check", { cache: "no-store" });
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }
      const data = await response.json();
      setUpdateInfo(data);
      if (data.update_available) {
        setMessage(`Update available: ${data.latest}`);
      } else {
        setMessage("Firmware is up to date");
      }
      setTimeout(() => setMessage(""), 5000);
    } catch (err) {
      setError("Failed to check for updates: " + err.message);
    } finally {
      setChecking(false);
    }
  };

  const startOta = async (type) => {
    if (!updateInfo) return;

    const hasFw = updateInfo.firmware_url && updateInfo.firmware_url.length > 0;
    const hasSp = updateInfo.spiffs_url && updateInfo.spiffs_url.length > 0;

    let label, body;
    if (type === "all") {
      if (!hasFw && !hasSp) { setError("No download URLs available"); return; }
      label = "firmware and UI";
      body = { firmware_url: updateInfo.firmware_url, spiffs_url: updateInfo.spiffs_url };
    } else if (type === "firmware") {
      if (!hasFw) { setError("No firmware URL available"); return; }
      label = "firmware";
      body = { firmware_url: updateInfo.firmware_url };
    } else {
      if (!hasSp) { setError("No UI image URL available"); return; }
      label = "UI";
      body = { spiffs_url: updateInfo.spiffs_url };
    }

    if (!confirm(`Update ${label} now? The device will reboot after the update.`)) {
      return;
    }

    setUpdating(true);
    setError("");
    setMessage(`Downloading and installing ${label}...`);
    try {
      const response = await fetch("/api/device/ota", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body),
      });

      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }

      setMessage("OTA update started. Device will reboot when complete...");
    } catch (err) {
      setError("OTA failed: " + err.message);
      setUpdating(false);
    }
  };

  const toggleAutoUpdate = async (enabled) => {
    setAutoUpdate(enabled);
    try {
      await fetch("/api/device/auto-update", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ enabled }),
      });
    } catch (err) {
      setError("Failed to save auto-update setting");
      setAutoUpdate(!enabled);
    }
  };

  const reboot = async () => {
    if (!confirm("Reboot the device now?")) {
      return;
    }

    setRebooting(true);
    setError("");
    try {
      await fetch("/api/device/reboot", { method: "POST" });
      setMessage("Rebooting...");
    } catch (err) {
      setError("Reboot failed: " + err.message);
      setRebooting(false);
    }
  };

  const formatUptime = (ms) => {
    if (!ms) return "-";
    const seconds = Math.floor(ms / 1000);
    const minutes = Math.floor(seconds / 60);
    const hours = Math.floor(minutes / 60);
    if (hours > 0) {
      return `${hours}h ${minutes % 60}m ${seconds % 60}s`;
    }
    return `${minutes}m ${seconds % 60}s`;
  };

  return (
    <>
      <section class="card">
        <div class="wifi-status">
          <h3>Device Information</h3>
          <div class="status-item">
            <span class="label">Firmware Version</span>
            <span class="value">{info ? info.firmware_version : "-"}</span>
          </div>
          <div class="status-item">
            <span class="label">ESP-IDF Version</span>
            <span class="value">{info ? info.idf_version : "-"}</span>
          </div>
          <div class="status-item">
            <span class="label">CPU Cores</span>
            <span class="value">{info ? info.chip_cores : "-"}</span>
          </div>
          <div class="status-item">
            <span class="label">Free Memory</span>
            <span class="value">
              {info ? `${(info.free_heap / 1024).toFixed(1)} KB` : "-"}
            </span>
          </div>
          <div class="status-item">
            <span class="label">Uptime</span>
            <span class="value">{info ? formatUptime(info.uptime_ms) : "-"}</span>
          </div>
        </div>
      </section>

      <section class="card">
        <div class="card-header">
          <h2>Firmware Update</h2>
          <button
            class="button secondary"
            onClick={checkForUpdates}
            type="button"
            disabled={checking || updating}
          >
            {checking ? "Checking..." : "Check for Updates"}
          </button>
        </div>

        {updateInfo && (
          <div class="wifi-status">
            <div class="status-item">
              <span class="label">Current Version</span>
              <span class="value">{updateInfo.current}</span>
            </div>
            <div class="status-item">
              <span class="label">Latest Version</span>
              <span class="value">{updateInfo.latest}</span>
            </div>
            <div class="status-item">
              <span class="label">Board</span>
              <span class="value">{updateInfo.board}</span>
            </div>
            <div class="status-item">
              <span class="label">Status</span>
              <span class="value">
                {updateInfo.update_available ? "Update available" : "Up to date"}
              </span>
            </div>
            {updateInfo.update_available && (
              <>
                <div class="status-item">
                  <span class="label">Firmware</span>
                  <span class="value">
                    {updateInfo.firmware_url ? "Available" : "Not found"}
                  </span>
                </div>
                <div class="status-item">
                  <span class="label">UI (SPIFFS)</span>
                  <span class="value">
                    {updateInfo.spiffs_url ? "Available" : "Not found"}
                  </span>
                </div>
              </>
            )}
          </div>
        )}

        {updateInfo && updateInfo.update_available && (
          <div class="action-row">
            <button
              class="button primary"
              onClick={() => startOta("all")}
              type="button"
              disabled={updating || (!updateInfo.firmware_url && !updateInfo.spiffs_url)}
            >
              {updating ? "Updating..." : "Update All"}
            </button>
            <button
              class="button secondary"
              onClick={() => startOta("firmware")}
              type="button"
              disabled={updating || !updateInfo.firmware_url}
            >
              Firmware Only
            </button>
            <button
              class="button secondary"
              onClick={() => startOta("spiffs")}
              type="button"
              disabled={updating || !updateInfo.spiffs_url}
            >
              UI Only
            </button>
          </div>
        )}

        <form class="wifi-form ap-form">
          <label class="field toggle-row" for="dev-auto-update">
            <input
              id="dev-auto-update"
              type="checkbox"
              checked={autoUpdate}
              onChange={(e) => toggleAutoUpdate(e.target.checked)}
            />
            <span>Auto-Update on boot</span>
          </label>
          <p class="hint-text">
            When enabled, the device will check for firmware updates on startup
            and install them automatically.
          </p>
        </form>

        {message && <p class="message">{message}</p>}
        {error && <p class="error">{error}</p>}
      </section>

      <section class="card">
        <div class="card-header">
          <h2>System</h2>
        </div>
        <div class="action-row">
          <button
            class="button secondary"
            onClick={reboot}
            type="button"
            disabled={rebooting || updating}
          >
            {rebooting ? "Rebooting..." : "Reboot Device"}
          </button>
        </div>
        <p class="hint-text">
          Restart the device. Active connections will be interrupted.
        </p>
      </section>
    </>
  );
}
