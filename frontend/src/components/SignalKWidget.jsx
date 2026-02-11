import { h, Component } from "preact";
import { useState, useEffect } from "preact/hooks";

export default function SignalKWidget() {
  const [status, setStatus] = useState({
    state: 0,
    state_name: "DISCONNECTED",
    authenticated: false,
    uptime_seconds: 0,
    messages_sent: 0,
    messages_received: 0,
    server: null
  });

  useEffect(() => {
    const loadStatus = async () => {
      try {
        const response = await fetch("/api/signalk/status");
        const data = await response.json();
        setStatus(data);
      } catch (error) {
        console.error("Failed to load Signal K status:", error);
      }
    };

    loadStatus();
    const timer = setInterval(loadStatus, 5000);
    return () => clearInterval(timer);
  }, []);

  const getStateColor = (stateName) => {
    switch (stateName) {
      case "STREAMING":
        return "#28a745"; // Green
      case "CONNECTED":
        return "#17a2b8"; // Blue
      case "CONNECTING":
        return "#ffc107"; // Yellow
      case "DISCOVERING":
      case "AUTH_PENDING":
        return "#ffc107"; // Yellow
      case "ERROR":
        return "#dc3545"; // Red
      default:
        return "#6c757d"; // Gray
    }
  };

  const getStateIcon = (stateName) => {
    switch (stateName) {
      case "STREAMING":
        return "✓";
      case "CONNECTED":
        return "◐";
      case "CONNECTING":
        return "⟳";
      case "ERROR":
        return "✕";
      default:
        return "○";
    }
  };

  return (
    <div class="signalk-widget">
      <div class="widget-header">
        <h3>Signal K</h3>
        <div
          class="status-indicator"
          style={{ backgroundColor: getStateColor(status.state_name) }}
          title={status.state_name}
        >
          {getStateIcon(status.state_name)}
        </div>
      </div>

      <div class="widget-content">
        <div class="status-row">
          <span class="label">State:</span>
          <span class="value">{status.state_name}</span>
        </div>

        {status.server && status.server.hostname && (
          <div class="status-row">
            <span class="label">Server:</span>
            <span class="value">
              {status.server.hostname}:{status.server.port}
            </span>
          </div>
        )}

        <div class="status-row">
          <span class="label">Auth:</span>
          <span class={status.authenticated ? "text-success" : "text-danger"}>
            {status.authenticated ? "✓ Yes" : "✕ No"}
          </span>
        </div>

        <div class="messages-row">
          <div class="message-stat">
            <div class="stat-label">Sent</div>
            <div class="stat-value">{status.messages_sent}</div>
          </div>
          <div class="message-stat">
            <div class="stat-label">Received</div>
            <div class="stat-value">{status.messages_received}</div>
          </div>
        </div>

        <div class="uptime-row">
          <span class="label">Uptime:</span>
          <span class="value">
            {Math.floor(status.uptime_seconds / 3600)}h{" "}
            {Math.floor((status.uptime_seconds % 3600) / 60)}m
          </span>
        </div>

        {status.error_message && (
          <div class="error-message">
            <strong>Error:</strong> {status.error_message}
          </div>
        )}
      </div>

      <style>{`
        .signalk-widget {
          border: 1px solid #ddd;
          border-radius: 8px;
          background: white;
          overflow: hidden;
          box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
        }

        .widget-header {
          display: flex;
          justify-content: space-between;
          align-items: center;
          padding: 15px;
          background: #f8f9fa;
          border-bottom: 1px solid #ddd;
        }

        .widget-header h3 {
          margin: 0;
          font-size: 1.1em;
          color: #333;
        }

        .status-indicator {
          width: 30px;
          height: 30px;
          border-radius: 50%;
          display: flex;
          align-items: center;
          justify-content: center;
          color: white;
          font-weight: bold;
          font-size: 1.2em;
          transition: background-color 0.3s ease;
        }

        .widget-content {
          padding: 15px;
        }

        .status-row {
          display: flex;
          justify-content: space-between;
          align-items: center;
          padding: 8px 0;
          border-bottom: 1px solid #eee;
          font-size: 0.95em;
        }

        .status-row:last-child:not(.messages-row):not(.uptime-row) {
          border-bottom: none;
        }

        .label {
          font-weight: 600;
          color: #666;
        }

        .value {
          color: #333;
        }

        .text-success {
          color: #28a745;
          font-weight: 600;
        }

        .text-danger {
          color: #dc3545;
          font-weight: 600;
        }

        .messages-row {
          display: grid;
          grid-template-columns: 1fr 1fr;
          gap: 10px;
          padding: 10px 0;
          border-bottom: 1px solid #eee;
        }

        .message-stat {
          text-align: center;
          padding: 10px;
          background: #f8f9fa;
          border-radius: 4px;
        }

        .stat-label {
          font-size: 0.8em;
          color: #666;
          text-transform: uppercase;
          font-weight: 600;
          margin-bottom: 5px;
        }

        .stat-value {
          font-size: 1.5em;
          font-weight: bold;
          color: #007bff;
        }

        .uptime-row {
          display: flex;
          justify-content: space-between;
          align-items: center;
          padding: 8px 0;
          font-size: 0.95em;
        }

        .error-message {
          margin-top: 10px;
          padding: 10px;
          background: #f8d7da;
          color: #721c24;
          border-radius: 4px;
          font-size: 0.9em;
        }

        .error-message strong {
          display: block;
          margin-bottom: 5px;
        }
      `}</style>
    </div>
  );
}
