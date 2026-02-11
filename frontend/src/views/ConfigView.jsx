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
        <h2>Configuration</h2>
        <p class="loading">Loading configuration...</p>
      </section>
    );
  }

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

    const inputType = type === "number" || type === "gpio" ? "number" : "text";

    return (
      <label class="field" for={inputId} key={field.key}>
        <span>{field.label || field.key}</span>
        <input
          id={inputId}
          class="input"
          type={inputType}
          value={value ?? ""}
          min={field.min}
          max={field.max}
          step={inputType === "number" ? "1" : undefined}
          onInput={(event) =>
            onChange(
              field.key,
              inputType === "number"
                ? Number(event.target.value)
                : event.target.value
            )
          }
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
        {/* Handle flat fields structure */}
        {schema.fields && schema.fields.map(renderField)}

        {/* Handle grouped sections structure */}
        {schema.sections && schema.sections.map((section) => (
          <fieldset class="config-section" key={section.title || section.id}>
            {section.title && <legend>{section.title}</legend>}
            {section.description && <p class="section-description">{section.description}</p>}
            {section.fields && section.fields.map(renderField)}
          </fieldset>
        ))}

        <button class="button primary" type="submit">
          Save Configuration
        </button>
        {message && <p class="message">{message}</p>}
        {error && <p class="error">{error}</p>}
      </form>
    </section>
  );
}
