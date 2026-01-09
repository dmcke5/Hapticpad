// Helper for YAML strings
const char* yamlClean(String s) {
  s.trim();
  if (s.startsWith("\"") || s.startsWith("'")) s = s.substring(1, s.length() - 1);
  return s.c_str();
}

uint16_t countProfilesYaml(const char *filename) {
  File file = SD.open(filename);
  if (!file) return 0;
  uint16_t count = 0;
  file.find("Profiles:");
  while (file.find("- name:")) {
    String n = file.readStringUntil('\n');
    n.trim();
    if (n.startsWith("\"")) n = n.substring(1, n.length() - 1);
    // Note: Using author's original indexing logic
    strncpy(profileNames[count], n.c_str(), maxProfiles); 
    count++;
  }
  file.close();
  return count;
}

bool loadSettingsYaml(const char *filename) {
  File file = SD.open(filename);
  if (!file) return false;
  if (!file.find("Settings:")) return false;
  if (file.find("LED_Mode:")) {
    String m = file.readStringUntil('\n');
    ledMode = parseLEDMode(yamlClean(m));
  }
  if (file.find("LED_Primary: [")) {
    primaryColour[0] = file.parseInt(); primaryColour[1] = file.parseInt(); primaryColour[2] = file.parseInt();
  }
  if (file.find("LED_Secondary: [")) {
    secondaryColour[0] = file.parseInt(); secondaryColour[1] = file.parseInt(); secondaryColour[2] = file.parseInt();
  }
  if (file.find("Clicky_P:")) Clicky_P = file.parseFloat();
  if (file.find("Clicky_I:")) Clicky_I = file.parseFloat();
  if (file.find("Twist_P:")) Twist_P = file.parseFloat();
  if (file.find("Twist_I:")) Twist_I = file.parseFloat();
  if (file.find("Momentum_P:")) Momentum_P = file.parseFloat();
  if (file.find("Momentum_I:")) Momentum_I = file.parseFloat();
  file.close();
  return true;
}

bool loadProfileYaml(const char *filename, uint16_t index) {
  File file = SD.open(filename);
  if (!file) return false;
  file.find("Profiles:");
  for (uint16_t i = 0; i <= index; i++) {
    if (!file.find("- name:")) { file.close(); return false; }
  }
  String n = file.readStringUntil('\n');
  strncpy(profileName, yamlClean(n), sizeof(profileName));
  if (file.find("WheelMode:")) {
    String wm = file.readStringUntil('\n');
    wheelMode = parseWheelMode(yamlClean(wm));
  }
  if (file.find("WheelKey:")) wheelAction = (uint8_t)file.parseInt();
  memset(macroAction, 0, sizeof(macroAction));
  memset(macroDelay, 0, sizeof(macroDelay));
  for (uint8_t btn = 0; btn < 6; btn++) {
    if (!file.find("label:")) break;
    String lbl = file.readStringUntil(',');
    strncpy(buttonLabel[btn], yamlClean(lbl), sizeof(buttonLabel[btn]));
    file.find("actions: [");
    for (uint8_t act = 0; act < 3; act++) {
      file.find("[");
      macroDelay[btn][act] = (uint16_t)file.parseInt();
      macroAction[btn][act] = (uint8_t)file.parseInt();
    }
  }
  file.close();
  return true;
}