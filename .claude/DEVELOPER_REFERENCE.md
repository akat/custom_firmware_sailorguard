# Quick Reference - Configuration System Changes

## What Changed?

### Configuration Schema Now Supports Grouping

**Before**: Flat list of configuration fields
```json
{
  "fields": [
    { "key": "anchor_gpio", ... },
    { "key": "buzzer_enabled", ... },
    ...
  ]
}
```

**After**: Organized into logical sections
```json
{
  "sections": [
    {
      "title": "Hardware Configuration",
      "fields": [
        { "key": "anchor_gpio", ... },
        { "key": "buzzer_enabled", ... }
      ]
    },
    ...
  ]
}
```

## How to Use the New System

### Adding a New Configuration Field

1. **Add to schema**: Edit `data/config.json`
   ```json
   {
     "sections": [
       {
         "title": "Hardware Configuration",
         "fields": [
           {
             "key": "my_new_field",
             "label": "My New Field",
             "type": "text",
             "default": "default_value",
             "help": "Description of this field"
           }
         ]
       }
     ]
   }
   ```

2. **Update frontend copy**: Same change to `frontend/public/config.json`
3. **Use the data in firmware**: Read from NVS namespace "config"
   ```c
   char value[64];
   nvs_handle_t handle;
   nvs_open("config", NVS_READONLY, &handle);
   nvs_get_str(handle, "my_new_field", value, sizeof(value));
   nvs_close(handle);
   ```

### Creating a New Section

1. Add a new section object to `data/config.json`:
   ```json
   {
     "title": "New Section Name",
     "description": "Section description shown to users",
     "fields": [
       // Add field objects here
     ]
   }
   ```

2. Update `frontend/public/config.json` identically
3. The UI will automatically render the new section with proper styling

### Supported Field Types

| Type | Input Element | Storage Format | Example |
|------|---------------|-----------------|---------|
| `text` | Text input | String | `"device_label": "SailorGuard"` |
| `number` | Number input | String (converted to double) | `"report_interval": "30"` |
| `gpio` | Number input | String (converted to double) | `"anchor_gpio": "4"` |
| `bool` | Checkbox | String ("true"/"false") | `"buzzer_enabled": "true"` |
| `select` | Dropdown | String (must match option value) | `"device_mode": "auto"` |

### Field Constraints

```json
{
  "key": "temperature_threshold",
  "label": "Temperature Threshold (°C)",
  "type": "number",
  "default": 25,
  "min": 0,
  "max": 100,
  "help": "Valid range: 0-100°C"
}
```

Constraints enforced:
- `min`: Minimum value (numbers only)
- `max`: Maximum value (numbers only)
- `options`: Valid enum values (select type only)
- Type coercion: String→bool, String→number as needed

## API Reference

### GET /api/config/schema
Returns the full configuration schema (with sections if defined)

**Response**:
```json
{
  "title": "Device Configuration",
  "description": "...",
  "sections": [
    {
      "title": "Hardware Configuration",
      "description": "...",
      "fields": [...]
    }
  ]
}
```

### GET /api/config/values
Returns current configuration values as a flattened object

**Response** (always flat, regardless of schema structure):
```json
{
  "anchor_gpio": 4,
  "buzzer_enabled": true,
  "report_interval": 30,
  "device_mode": "auto",
  "device_label": "SailorGuard"
}
```

### POST /api/config/values
Updates configuration values

**Request**:
```json
{
  "anchor_gpio": 5,
  "report_interval": 60
}
```

**Response**:
```json
{ "ok": true }
```

## Component Reference

### components/config_api/config_api.c

**Key Functions**:
- `config_schema_handler()` - GET /api/config/schema
- `config_values_get_handler()` - GET /api/config/values
- `config_values_post_handler()` - POST /api/config/values
- `schema_load_root()` - Loads config.json, supports both flat and sectioned
- `add_field_value()` - Retrieves single field value from NVS
- `store_field_value()` - Validates and stores single field to NVS

### frontend/src/views/ConfigView.jsx

**Key Exports**:
- `ConfigView({ schema, values, message, error, onChange, onSubmit })`

**Helper Functions**:
- `renderField(field)` - Renders single field based on type

**Supported Props**:
- `schema`: Full config schema object
- `values`: Flattened key→value map
- `onChange(key, value)`: Called when field changed
- `onSubmit(event)`: Called when form submitted
- `message`: Success message to display
- `error`: Error message to display

### frontend/src/App.jsx

**Related State**:
```js
const [configSchema, setConfigSchema] = useState(null);      // Full schema
const [configValues, setConfigValues] = useState({});        // Current values
```

**Related Functions**:
```js
updateConfigValue(key, value)  // Update local state
submitConfig()                   // POST to /api/config/values
```

## Styling

### CSS Classes

| Class | Purpose |
|-------|---------|
| `.config-form` | Container for configuration form |
| `.config-section` | Section container (fieldset) |
| `.config-section legend` | Section title |
| `.section-description` | Section description paragraph |
| `.field` | Individual field container |
| `.toggle-row` | Checkbox layout (inline label) |
| `.field-help` | Help text below field |

### Customizing Appearance

Edit `frontend/src/styles.css`:
```css
.config-section {
  border: 1px solid #e1ddd6;
  border-radius: 18px;
  padding: 16px;
  background: #f9f8f3;
}

.config-section legend {
  font-weight: 600;
  font-size: 15px;
  color: var(--ink);
}
```

## Debugging

### Check Schema Loading
```bash
# In browser console
fetch('/api/config/schema').then(r => r.json()).then(console.log)
```

### Check Current Values
```bash
fetch('/api/config/values').then(r => r.json()).then(console.log)
```

### Check NVS Storage (CLI)
```bash
idf.py monitor
# In monitor, check logs for nvs operations
```

### Common Issues

**Issue**: "Invalid schema" error on device
**Solution**: Verify config.json is valid JSON and contains either "fields" or "sections" array

**Issue**: Fields not appearing in UI
**Solution**: Check browser console for parsing errors, verify schema file exists in /spiffs/

**Issue**: Values not persisting
**Solution**: Verify NVS partition has space, check logs for "Failed to store" messages

**Issue**: Mobile UI broken
**Solution**: Clear browser cache (Ctrl+Shift+Del), check CSS in DevTools for errors

## Migration Guide

### From Flat to Sectioned Schema

1. Keep all field definitions
2. Group related fields into sections:
   ```json
   {
     "sections": [
       {
         "title": "Group Name",
         "fields": [/* move fields here */]
       }
     ]
   }
   ```
3. Update both `data/config.json` and `frontend/public/config.json`
4. Rebuild frontend: `npm run build`
5. Copy to firmware: `node frontend/scripts/copy-to-data.mjs`
6. Flash device - no NVS wipe needed! Old values persist.

### From Sectioned Back to Flat

1. Extract all fields from sections into single array:
   ```json
   {
     "fields": [
       /* all fields from sections */
     ]
   }
   ```
2. Update both config.json files
3. Rebuild/deploy as above
4. All existing NVS values continue working

## Tips & Tricks

### Reordering Fields Without Data Loss
Reordering fields within the schema doesn't affect NVS storage (keys-based, not position-based).

### Adding Optional Fields
New fields use their `default` value when first accessed if not in NVS.

### Hiding Fields Temporarily
Comment out fields in schema, NVS values are preserved (can re-enable later).

### Testing New Field
Add to schema, rebuild, update via POST, restart to verify persistence.

## Related Files
- Backend implementation: [components/config_api/config_api.c](../components/config_api/config_api.c)
- Frontend component: [frontend/src/views/ConfigView.jsx](../frontend/src/views/ConfigView.jsx)
- Styling: [frontend/src/styles.css](../frontend/src/styles.css)
- Data schema: [data/config.json](../data/config.json)
- Frontend schema: [frontend/public/config.json](../frontend/public/config.json)
