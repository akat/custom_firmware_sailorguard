# Configuration Grouping/Sections - Testing Guide

## Overview
The configuration system now supports both flat field arrays and grouped section structures. This guide tests the end-to-end functionality.

## Files Modified

### Backend (C/ESP-IDF)
- **components/config_api/config_api.c**
  - Updated `config_values_get_handler()` to iterate over both flat fields and sections
  - Updated `config_values_post_handler()` to validate and store both structures
  - Uses extracted helpers: `add_field_value()`, `store_field_value()`

### Frontend (JavaScript/JSX)
- **frontend/src/views/ConfigView.jsx**
  - Added `renderField()` extracted function for reusable field rendering
  - Added section iteration with fieldset elements
  - Backward compatible with flat fields structure

### Styling
- **frontend/src/styles.css**
  - Added `.config-section` for section container styling
  - Added `.config-section legend` for section title
  - Added `.section-description` for section descriptions

### Configuration Schema
- **data/config.json** - Converted from flat to sectioned structure
- **frontend/public/config.json** - Kept in sync (via copy-to-data.mjs)

## Test Scenarios

### Scenario 1: Device Boot with Grouped Schema
**Expected Outcome**: Device loads and displays configuration grouped by section

**Steps**:
1. Flash firmware to device
2. Open web UI to Configuration view
3. Verify three sections appear:
   - "Hardware Configuration" (anchor_gpio, buzzer_enabled)
   - "Device Behavior" (report_interval, device_mode)
   - "Device Identity" (device_label)

**Success Criteria**:
- [ ] Sections render with proper titles
- [ ] Each section has a description visible
- [ ] Fields within sections display correctly
- [ ] No console errors in browser DevTools

### Scenario 2: Reading Configuration (GET /api/config/values)
**Expected Outcome**: API returns flattened key→value map regardless of schema structure

**Steps**:
1. Open browser DevTools (F12)
2. Go to Network tab
3. Navigate to Configuration view
4. Find the `/api/config/values` GET request
5. Inspect the response in the Response tab

**Success Criteria**:
- [ ] Response contains all 5 keys: `anchor_gpio`, `buzzer_enabled`, `report_interval`, `device_mode`, `device_label`
- [ ] Values match defaults from schema (or stored values)
- [ ] No nested structure in response (flat key→value map)

**Example Response**:
```json
{
  "anchor_gpio": 4,
  "buzzer_enabled": true,
  "report_interval": 30,
  "device_mode": "auto",
  "device_label": "SailorGuard"
}
```

### Scenario 3: Modifying Configuration (POST /api/config/values)
**Expected Outcome**: Updates persist to NVS and survive device restart

**Steps**:
1. In Configuration view, change one field in each section:
   - Change "Anchor GPIO" to 5
   - Toggle "Buzzer Enabled" off
   - Change "Report Interval" to 60
2. Click "Save Configuration"
3. Open DevTools Network tab
4. Find the `/api/config/values` POST request
5. Inspect the request body

**Success Criteria**:
- [ ] POST request body contains only changed fields (flattened)
- [ ] Success message appears ("Configuration saved" or similar)
- [ ] No error messages
- [ ] Values remain changed after page refresh

**Example POST Body**:
```json
{
  "anchor_gpio": 5,
  "buzzer_enabled": false,
  "report_interval": 60
}
```

### Scenario 4: NVS Persistence
**Expected Outcome**: Configuration values persist across device reboot

**Steps**:
1. Modify configuration values (Scenario 3)
2. Reboot the device (reset button or unplug/replug)
3. Open web UI and navigate to Configuration view
4. Check if values match what was saved

**Success Criteria**:
- [ ] All modified values retained after reboot
- [ ] Unmodified values show defaults (not corrupted)

### Scenario 5: Field Validation Within Sections
**Expected Outcome**: Field constraints (min/max/type) enforced regardless of section placement

**Steps**:
1. Try to set "Anchor GPIO" to 50 (exceeds max of 39)
   - Should not allow or show validation error
2. Try to set "Report Interval" to 2 (below min of 5)
   - Should not allow or show validation error
3. Try to set "Device Mode" to invalid value (not "auto" or "manual")
   - Should not allow or show validation error

**Success Criteria**:
- [ ] Validation errors prevent invalid saves
- [ ] Validation works correctly for all field types within sections
- [ ] Error messages are clear to user

### Scenario 6: Backward Compatibility (Optional)
**Expected Outcome**: System still works if config.json reverts to flat structure

**Steps**:
1. Modify firmware config.json temporarily to use flat "fields" array:
   ```json
   {
     "title": "Test Config",
     "fields": [
       { "key": "test_field", "label": "Test", "type": "text", "default": "test" }
     ]
   }
   ```
2. Rebuild and flash firmware
3. Open Configuration view
4. Verify it renders fields without sections

**Success Criteria**:
- [ ] System gracefully handles flat structure
- [ ] No errors when processing flat fields
- [ ] Fields render correctly (no section wrappers)

### Scenario 7: Mobile Responsive (Optional)
**Expected Outcome**: Section styling looks good on mobile devices

**Steps**:
1. Open web UI on mobile browser (or use DevTools device emulation)
2. Navigate to Configuration view
3. Scroll through all sections
4. Try changing field values on mobile

**Success Criteria**:
- [ ] Sections are readable on small screens
- [ ] Input fields are appropriately sized for mobile
- [ ] Section titles/descriptions line-wrap correctly
- [ ] Fieldset styling doesn't interfere with input functionality

## Manual Testing Commands

### Test GET /api/config/schema
```bash
curl http://192.168.4.1/api/config/schema | jq .
```
Verify output contains `sections` array with proper structure.

### Test GET /api/config/values
```bash
curl http://192.168.4.1/api/config/values | jq .
```
Verify output is flattened (no nested structure).

### Test POST /api/config/values
```bash
curl -X POST http://192.168.4.1/api/config/values \
  -H "Content-Type: application/json" \
  -d '{"anchor_gpio": 10, "report_interval": 45}'
```
Verify response is `{"ok":true}` and values persist.

## Expected Behavior Summary

| Feature | Flat Schema | Grouped Schema |
|---------|-------------|-----------------|
| GET /values | Flattened object | Flattened object (identical) |
| POST /values | Validates and stores | Validates and stores (identical) |
| GET /schema | Fields array | Sections array |
| Frontend render | Single list | Grouped sections |
| NVS storage | Keys at root | Keys at root (identical) |
| Validation | Per-field constraints | Per-field (regardless of grouping) |

## Common Issues & Troubleshooting

### Configuration not loading
- Check browser console for JavaScript errors
- Verify `/api/config/schema` returns valid JSON
- Check/api/config/values` endpoint responds

### Changes not persisting
- Verify POST returns `{"ok":true}`
- Check device logs for NVS errors
- Ensure config namespace is accessible (not corrupted NVS)

### Styling issues with sections
- Clear browser cache (Ctrl+Shift+Del / Cmd+Shift+Delete)
- Verify styles.css changes were applied
- Check DevTools for CSS errors

### Section titles not appearing
- Verify config.json has "title" field in sections
- Check browser console for cJSON parsing errors
- Inspect element to verify fieldset/legend structure

## Success Criteria Summary
- ✅ Configuration view renders sections (if schema has them)
- ✅ API values endpoint returns flattened response
- ✅ Field validation works regardless of schema structure
- ✅ NVS persistence works with both flat and grouped schemas
- ✅ Mobile UI remains responsive
