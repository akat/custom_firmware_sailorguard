import { useEffect, useState } from "preact/hooks";
import DashboardView from "./views/DashboardView.jsx";
import WifiSettingsView from "./views/WifiSettingsView.jsx";
import ConfigView from "./views/ConfigView.jsx";
import SignalKView from "./views/SignalKView.jsx";
import MainLayout from "./templates/MainLayout.jsx";
import { getRoute, setRoute, subscribeRouteChange } from "./utils/router.js";

export default function App() {
  const [view, setView] = useState(() => getRoute());
  const [status, setStatus] = useState(null);
  const [error, setError] = useState("");

  const [wifiStatus, setWifiStatus] = useState(null);
  const [networks, setNetworks] = useState([]);
  const [wifiError, setWifiError] = useState("");
  const [wifiMessage, setWifiMessage] = useState("");
  const [wifiForm, setWifiForm] = useState({ ssid: "", password: "" });
  const [apForm, setApForm] = useState({ ssid: "", password: "" });
  const [apError, setApError] = useState("");
  const [apMessage, setApMessage] = useState("");
  const [configSchema, setConfigSchema] = useState(null);
  const [configValues, setConfigValues] = useState({});
  const [configMessage, setConfigMessage] = useState("");
  const [configError, setConfigError] = useState("");

  useEffect(() => {
    let timerId = null;

    const load = async () => {
      try {
        const response = await fetch("/api/status", { cache: "no-store" });
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}`);
        }
        const data = await response.json();
        setStatus(data);
        setError("");
      } catch (err) {
        setError(err.message || "Failed to fetch status");
      }
    };

    load();
    timerId = setInterval(load, 2000);

    return () => {
      if (timerId) clearInterval(timerId);
    };
  }, []);

  useEffect(() => {
    if (view !== "wifi") return;
    let timerId = null;

    const loadStatus = async () => {
      try {
        const response = await fetch("/api/wifi/status", { cache: "no-store" });
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}`);
        }
        const data = await response.json();
        setWifiStatus(data);
        setWifiError("");
      } catch (err) {
        setWifiError(err.message || "Failed to fetch Wi-Fi status");
      }
    };

    const loadConfig = async () => {
      try {
        const response = await fetch("/api/wifi/config", { cache: "no-store" });
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}`);
        }
        const data = await response.json();
        setWifiForm((prev) => ({ ...prev, ssid: data.ssid || "" }));
      } catch (err) {
        setWifiError(err.message || "Failed to fetch Wi-Fi config");
      }
    };

    const loadApConfig = async () => {
      try {
        const response = await fetch("/api/wifi/ap_config", { cache: "no-store" });
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}`);
        }
        const data = await response.json();
        setApForm({ ssid: data.ssid || "", password: data.password || "" });
      } catch (err) {
        setApError(err.message || "Failed to fetch SoftAP config");
      }
    };

    loadStatus();
    loadConfig();
    loadApConfig();
    timerId = setInterval(loadStatus, 3000);

    return () => {
      if (timerId) clearInterval(timerId);
    };
  }, [view]);

  useEffect(() => {
    if (view !== "config") return;

    const loadSchema = async () => {
      try {
        const response = await fetch("/api/config/schema", { cache: "no-store" });
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}`);
        }
        const data = await response.json();
        setConfigSchema(data);
        setConfigError("");
      } catch (err) {
        setConfigError(err.message || "Failed to load config schema");
      }
    };

    const loadValues = async () => {
      try {
        const response = await fetch("/api/config/values", { cache: "no-store" });
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}`);
        }
        const data = await response.json();
        setConfigValues(data || {});
        setConfigError("");
      } catch (err) {
        setConfigError(err.message || "Failed to load config values");
      }
    };

    loadSchema();
    loadValues();
  }, [view]);

  useEffect(() => {
    if (!window.location.hash) {
      setRoute("dashboard");
    }
    setView(getRoute());
    return subscribeRouteChange(setView);
  }, []);

  const scanNetworks = async () => {
    setWifiMessage("Scanning...");
    setWifiError("");
    try {
      const response = await fetch("/api/wifi/scan", { cache: "no-store" });
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }
      const data = await response.json();
      setNetworks(data.networks || []);
      setWifiMessage("");
    } catch (err) {
      setWifiMessage("");
      setWifiError(err.message || "Scan failed");
    }
  };

  const submitWifi = async (event) => {
    event.preventDefault();
    setWifiMessage("Saving and connecting...");
    setWifiError("");

    try {
      const response = await fetch("/api/wifi/config", {
        method: "POST",
        headers: {
          "Content-Type": "application/json"
        },
        body: JSON.stringify({
          ssid: wifiForm.ssid,
          password: wifiForm.password
        })
      });

      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }

      setWifiMessage("Saved. Connecting...");
      setWifiForm((prev) => ({ ...prev, password: "" }));
    } catch (err) {
      setWifiMessage("");
      setWifiError(err.message || "Failed to save Wi-Fi settings");
    }
  };

  const updateWifiForm = (patch) => {
    setWifiForm((prev) => ({ ...prev, ...patch }));
  };

  const updateApForm = (patch) => {
    setApForm((prev) => ({ ...prev, ...patch }));
  };

  const submitApConfig = async (event) => {
    event.preventDefault();
    setApMessage("Updating SoftAP...");
    setApError("");

    try {
      const response = await fetch("/api/wifi/ap_config", {
        method: "POST",
        headers: {
          "Content-Type": "application/json"
        },
        body: JSON.stringify({
          ssid: apForm.ssid,
          password: apForm.password
        })
      });

      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }

      setApMessage("SoftAP settings updated successfully");
      setTimeout(() => setApMessage(""), 3000);
    } catch (err) {
      setApMessage("");
      setApError(err.message || "Failed to update SoftAP settings");
    }
  };

  const updateConfigValue = (key, value) => {
    setConfigValues((prev) => ({ ...prev, [key]: value }));
  };

  const submitConfig = async (event) => {
    event.preventDefault();
    setConfigMessage("Saving configuration...");
    setConfigError("");

    try {
      const response = await fetch("/api/config/values", {
        method: "POST",
        headers: {
          "Content-Type": "application/json"
        },
        body: JSON.stringify(configValues)
      });

      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }

      setConfigMessage("Configuration saved");
      setTimeout(() => setConfigMessage(""), 3000);
    } catch (err) {
      setConfigMessage("");
      setConfigError(err.message || "Failed to save configuration");
    }
  };

  const handleNavigate = (nextView) => {
    setRoute(nextView);
    setView(nextView);
  };

  return (
    <MainLayout view={view} onNavigate={handleNavigate}>
      {view === "dashboard" && <DashboardView status={status} error={error} />}
      {view === "wifi" && (
        <WifiSettingsView
          networks={networks}
          wifiStatus={wifiStatus}
          wifiError={wifiError}
          wifiMessage={wifiMessage}
          wifiForm={wifiForm}
          apForm={apForm}
          apMessage={apMessage}
          apError={apError}
          onScan={scanNetworks}
          onSubmit={submitWifi}
          onFormChange={updateWifiForm}
          onSelectNetwork={(ssid) => updateWifiForm({ ssid })}
          onApFormChange={updateApForm}
          onApSubmit={submitApConfig}
        />
      )}
      {view === "config" && (
        <ConfigView
          schema={configSchema}
          values={configValues}
          message={configMessage}
          error={configError}
          onChange={updateConfigValue}
          onSubmit={submitConfig}
        />
      )}
      {view === "signalk" && (
        <SignalKView />
      )}
    </MainLayout>
  );
}
