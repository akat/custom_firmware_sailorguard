import { useEffect, useState } from "preact/hooks";
import DashboardView from "./views/DashboardView.jsx";
import WifiSettingsView from "./views/WifiSettingsView.jsx";
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

    loadStatus();
    loadConfig();
    timerId = setInterval(loadStatus, 3000);

    return () => {
      if (timerId) clearInterval(timerId);
    };
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
          onScan={scanNetworks}
          onSubmit={submitWifi}
          onFormChange={updateWifiForm}
          onSelectNetwork={(ssid) => updateWifiForm({ ssid })}
        />
      )}
    </MainLayout>
  );
}
