import { useEffect, useState } from "preact/hooks";

function NumberField({ id, value, min, max, step, placeholder, onChange }) {
  const toDisplay = (v) =>
    v === null || v === undefined || Number.isNaN(v) ? "" : String(v);
  const [text, setText] = useState(toDisplay(value));

  useEffect(() => {
    const parsed = text === "" ? NaN : Number(text);
    if (Number.isFinite(parsed) && parsed === value) {
      return;
    }
    setText(toDisplay(value));
  }, [value]);

  const handleInput = (event) => {
    const raw = event.target.value.replace(",", ".");
    setText(raw);
    if (raw === "" || raw === "-" || raw.endsWith(".")) {
      return;
    }
    const parsed = Number(raw);
    if (Number.isFinite(parsed)) {
      onChange(parsed);
    }
  };

  return (
    <input
      id={id}
      class="input"
      type="text"
      inputMode="decimal"
      value={text}
      min={min}
      max={max}
      step={step}
      onInput={handleInput}
      placeholder={placeholder || ""}
    />
  );
}

export default function ConfigView({
  schema,
  values,
  message,
  error,
  onChange,
  onSubmit
}) {
  if (!schema) {
    return (
      <section class="card">
        <div class="card-header">
          <h2>Configuration</h2>
        </div>
        <p class="loading">Loading configuration...</p>
      </section>
    );
  }

  const fields = Array.isArray(schema.fields) ? schema.fields : [];
  const sections = Array.isArray(schema.sections) ? schema.sections : [];
  const hasSections = sections.length > 0;

  const renderField = (field) => {
    const value = values[field.key];
    const type = field.type || "text";
    const inputId = `cfg-${field.key}`;

    if (type === "bool") {
      return (
        <label class="field toggle-row" for={inputId} key={field.key}>
          <input
            id={inputId}
            type="checkbox"
            checked={Boolean(value)}
            onChange={(event) =>
              onChange(field.key, event.target.checked)
            }
          />
          <span>{field.label || field.key}</span>
        </label>
      );
    }

    if (type === "select") {
      return (
        <label class="field" for={inputId} key={field.key}>
          <span>{field.label || field.key}</span>
          <select
            id={inputId}
            class="input select"
            value={value ?? ""}
            onChange={(event) =>
              onChange(field.key, event.target.value)
            }
          >
            {(field.options || []).map((option) => (
              <option value={option.value} key={option.value}>
                {option.label || option.value}
              </option>
            ))}
          </select>
          {field.help && <p class="field-help">{field.help}</p>}
        </label>
      );
    }

    const isNumeric = type === "number" || type === "gpio";

    if (isNumeric) {
      return (
        <label class="field" for={inputId} key={field.key}>
          <span>{field.label || field.key}</span>
          <NumberField
            id={inputId}
            value={value}
            min={field.min}
            max={field.max}
            step={field.step ?? "any"}
            placeholder={field.placeholder}
            onChange={(parsed) => onChange(field.key, parsed)}
          />
          {field.help && <p class="field-help">{field.help}</p>}
        </label>
      );
    }

    return (
      <label class="field" for={inputId} key={field.key}>
        <span>{field.label || field.key}</span>
        <input
          id={inputId}
          class="input"
          type="text"
          value={value ?? ""}
          onInput={(event) => onChange(field.key, event.target.value)}
          placeholder={field.placeholder || ""}
        />
        {field.help && <p class="field-help">{field.help}</p>}
      </label>
    );
  };

  return (
    <section class="card">
      <div class="card-header">
        <h2>{schema.title || "Configuration"}</h2>
      </div>
      {schema.description && <p class="subtitle">{schema.description}</p>}

      <form class="config-form" onSubmit={onSubmit}>
        <div class="config-grid">
          {!hasSections && fields.length > 0 && (
            <div class="config-widget">
              <div class="config-widget-header">
                <h3>Settings</h3>
              </div>
              <div class="config-widget-body">
                {fields.map(renderField)}
              </div>
            </div>
          )}

          {hasSections && sections.map((section, index) => (
            <div class="config-widget" key={section.title || section.id || `section-${index}`}>
              <div class="config-widget-header">
                <h3>{section.title || `Section ${index + 1}`}</h3>
              </div>
              <div class="config-widget-body">
                {section.description && (
                  <p class="hint-text">{section.description}</p>
                )}
                {section.fields && section.fields.map(renderField)}
              </div>
            </div>
          ))}

          {!hasSections && fields.length === 0 && (
            <div class="config-widget">
              <div class="config-widget-header">
                <h3>Settings</h3>
              </div>
              <div class="config-widget-body">
                <p class="muted">No configurable fields found.</p>
              </div>
            </div>
          )}
        </div>

        <div class="action-row">
          <button class="button primary" type="submit">
            Save Configuration
          </button>
        </div>
        {message && <p class="message">{message}</p>}
        {error && <p class="error">{error}</p>}
      </form>
    </section>
  );
}
