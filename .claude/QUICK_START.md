# Quick Start - Configuration Grouping Feature

**tl;dr**: Configuration settings are now organized into logical sections (Hardware, Behavior, Identity). No changes needed to use existing features—just optional customization for new fields.

## For the Impatient

### What Changed?
Configuration view now groups settings by function instead of showing a flat list.

### What Do I Need to Do?
Nothing—it's ready to use. Just build and flash.

### Can I Still Add Custom Settings?
Yes! Edit `data/config.json` to add new fields to any section.

---

## 5-Minute Setup

### 1. Enable New Schema
The new sectioned schema is already in place:
- `data/config.json` - Firmware schema
- `frontend/public/config.json` - Frontend schema  

Both automatically synced via build process. ✅

### 2. Build Firmware
```bash
cd /path/to/firmware
pio run --environment esp32s3box
```

### 3. Flash Device
```bash
pio run --environment esp32s3box --target upload
```

### 4. Test Configuration UI
1. Open web UI (http://192.168.4.1)
2. Click "Configuration" tab
3. See three sections with organized settings
4. Change any value and click "Save Configuration"
5. Verify it persists after refresh

✅ Done!

---

## Adding a Custom Configuration Field

### Step 1: Edit Schema
Edit `data/config.json`:

```json
{
  "sections": [
    {
      "title": "Hardware Configuration",
      "fields": [
        // Existing fields here...
        {
          "key": "my_new_gpio",
          "label": "My GPIO Pin",
          "type": "gpio",
          "default": 10,
          "min": 0,
          "max": 39,
          "help": "GPIO used for my feature"
        }
      ]
    }
  ]
}
```

### Step 2: Copy to Frontend
The build copies it automatically, or manually:
```bash
node frontend/scripts/copy-to-data.mjs
```

### Step 3: Use in Firmware
```c
// Read the value from NVS
char value[64];
nvs_handle_t handle;
nvs_open("config", NVS_READONLY, &handle);
nvs_get_str(handle, "my_new_gpio", value, sizeof(value));
nvs_close(handle);

int gpio_pin = atoi(value);  // Convert string to int
```

### Step 4: Build & Test
```bash
npm run build                           # Build frontend
node frontend/scripts/copy-to-data.mjs  # Copy assets
pio run --environment esp32s3box        # Build firmware
pio run --environment esp32s3box --target upload  # Flash
```

---

## Understanding the Three Sections

### Hardware Configuration
Settings that directly affect GPIO pins and hardware behavior.

**Current Fields**:
- `anchor_gpio` - Output GPIO for anchor control
- `buzzer_enabled` - Enable/disable buzzer

**Add Here**: New GPIO pins, hardware timing, power settings

### Device Behavior
Settings that affect how the device runs and reports data.

**Current Fields**:
- `report_interval` - How often status is reported (seconds)
- `device_mode` - Auto vs Manual operation mode

**Add Here**: Thresholds, intervals, mode settings, automation rules

### Device Identity
Settings for naming and identifying the device.

**Current Fields**:
- `device_label` - Friendly name for this device

**Add Here**: Serial number, location, version info, tags

---

## API Endpoints Reference

### Get Current Configuration
```bash
curl http://192.168.4.1/api/config/values
# Returns: { "anchor_gpio": 4, "buzzer_enabled": true, ... }
```

### Get Full Schema (With Sections)
```bash
curl http://192.168.4.1/api/config/schema
# Returns: { "title": "...", "sections": [ { "title": "...", "fields": [...] } ] }
```

### Update Configuration
```bash
curl -X POST http://192.168.4.1/api/config/values \
  -H "Content-Type: application/json" \
  -d '{ "anchor_gpio": 5, "report_interval": 60 }'
# Returns: { "ok": true }
```

---

## Field Type Reference

When adding fields to schema:

| Type | HTML Input | NVS Storage | Validation | Example |
|------|-----------|-------------|-----------|---------|
| `text` | `<input type="text">` | String | None | "device_label" |
| `number` | `<input type="number">` | String | min/max range | "report_interval": "30" |
| `gpio` | `<input type="number">` | String | min 0, max 39 | "anchor_gpio": "4" |
| `bool` | `<input type="checkbox">` | "true"/"false" | None | "buzzer_enabled": "true" |
| `select` | `<select>` | String | Must match option | "device_mode": "auto" |

### Adding Validation to Number Fields
```json
{
  "key": "timeout_ms",
  "label": "Timeout (milliseconds)",
  "type": "number",
  "default": 5000,
  "min": 100,
  "max": 30000,
  "help": "Must be between 100 and 30000"
}
```

### Adding Dropdown Options
```json
{
  "key": "device_mode",
  "label": "Operation Mode",
  "type": "select",
  "default": "auto",
  "options": [
    { "label": "Automatic Mode", "value": "auto" },
    { "label": "Manual Mode", "value": "manual" },
    { "label": "Debug Mode", "value": "debug" }
  ]
}
```

---

## Troubleshooting

### Configuration not loading
**Problem**: "Loading configuration..." stays forever  
**Solution**: 
1. Check browser console (F12) for errors
2. Verify `/api/config/schema` responds: `curl http://192.168.4.1/api/config/schema`
3. Check device logs for HTTP errors

### Changes don't persist
**Problem**: Configuration saves but doesn't stick after reboot  
**Solution**:
1. Check POST returned `{ "ok": true }`
2. Verify NVS isn't corrupted (full partition? no free space?)
3. Check error logs for "Failed to store" messages
4. Try erasing NVS and reconfiguring: `pio run --environment esp32s3box --target erase`

### Sections not showing
**Problem**: UI shows flat list instead of grouped sections  
**Solution**:
1. Clear browser cache (Ctrl+Shift+Del)
2. Verify config.json has "sections" array (not "fields")
3. Check browser DevTools → Network → Response for `/api/config/schema`
4. Verify JSON is valid: `curl http://192.168.4.1/api/config/schema | jq .`

### New field not appearing
**Problem**: Added field to schema but it's not in UI  
**Solution**:
1. Did you run `npm run build`? (Rebuild frontend)
2. Did you copy to data? `node frontend/scripts/copy-to-data.mjs`
3. Did you rebuild firmware? `pio run ...`
4. Did you flash the device? `pio run --target upload`
5. Clear browser cache and refresh

---

## Next Steps

### Immediate
- [ ] Build and flash the firmware
- [ ] Test the Configuration view
- [ ] Verify sections render correctly
- [ ] Try modifying and saving a value

### Optional Customization
- [ ] Add your own configuration fields to schema
- [ ] Organize fields into appropriate sections
- [ ] Update help text for your use case
- [ ] Rename sections to match your domain

### Future Enhancements
- Add field dependencies (show/hide based on values)
- Implement nested sections
- Create section-specific API endpoints
- Add import/export functionality
- Support configuration profiles

---

## File Quick Reference

| File | Purpose | When to Edit |
|------|---------|-------------|
| `data/config.json` | Firmware configuration schema | Adding fields |
| `frontend/public/config.json` | Frontend schema copy | Keep in sync with above |
| `components/config_api/config_api.c` | Backend API logic | Never (unless adding features) |
| `frontend/src/views/ConfigView.jsx` | Configuration UI component | Never (already supports sections) |
| `frontend/src/styles.css` | UI styling | Customizing appearance |

---

## Key Concepts

### How It Works

```
Device boots
  ↓
GET /api/config/schema
  ↓
Frontend gets: { "sections": [ { "title": "...", "fields": [...] } ] }
  ↓
Renders UI with <fieldset> for each section
  ↓
User modifies field
  ↓
POST /api/config/values { "field_key": new_value }
  ↓
Backend validates against schema and stores to NVS
  ↓
Values persist across reboots
```

### Schema-Driven = Flexible

You can:
- ✅ Change field order (just reorder in JSON)
- ✅ Add fields (add to fields array)
- ✅ Remove fields (delete from fields array)
- ✅ Create new sections (add section object)
- ✅ Reorganize settings (move fields between sections)
- ✅ Change labels/help (edit text in JSON)

All without touching firmware code!

### API Always Returns Flat Format

Even though schema has sections:
```
Schema:
{
  "sections": [
    { "title": "Hardware", "fields": [ { "key": "gpio1" }, ... ] },
    { "title": "Behavior", "fields": [ { "key": "interval" }, ... ] }
  ]
}

API Response:
{
  "gpio1": 4,
  "interval": 30,
  ...
}
```

No nesting in API—just flat key→value pairs. This is intentional for simplicity.

---

## Support

For detailed information:
- Architecture overview: `.claude/ARCHITECTURE_NOTES.md`
- Testing guide: `.claude/TESTING_GUIDE.md`
- Developer reference: `.claude/DEVELOPER_REFERENCE.md`
- Before/after comparison: `.claude/BEFORE_AND_AFTER.md`
- Full implementation summary: `.claude/IMPLEMENTATION_SUMMARY.md`

---

## Summary

✅ Configuration system now supports logical grouping  
✅ Full backward compatible  
✅ Ready to use immediately  
✅ Easy to customize  
✅ Well documented  

**Start here**: Build firmware → Flash device → Test Configuration view

Happy configuring! 🚀
