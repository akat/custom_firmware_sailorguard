export default function Header({ view, onNavigate }) {
  return (
    <header class="app-header">
      <div class="hero">
        <div>
          <p class="eyebrow">SailorGuard</p>
          <h1>Anchor Guard</h1>
          <p class="subtitle">administration panel</p>
        </div>
        <div class="status-chip">
          <span class="dot" />
          <span>Online</span>
        </div>
      </div>
      <nav class="menu">
        <button
          class={`menu-button ${view === "dashboard" ? "active" : ""}`}
          onClick={() => onNavigate("dashboard")}
        >
          Dashboard
        </button>
        <button
          class={`menu-button ${view === "wifi" ? "active" : ""}`}
          onClick={() => onNavigate("wifi")}
        >
          Wi-Fi Settings
        </button>
        <button
          class={`menu-button ${view === "config" ? "active" : ""}`}
          onClick={() => onNavigate("config")}
        >
          Configuration
        </button>
        <button
          class={`menu-button ${view === "signalk" ? "active" : ""}`}
          onClick={() => onNavigate("signalk")}
        >
          Signal K
        </button>
        <button
          class={`menu-button ${view === "device" ? "active" : ""}`}
          onClick={() => onNavigate("device")}
        >
          Device
        </button>
      </nav>
    </header>
  );
}
