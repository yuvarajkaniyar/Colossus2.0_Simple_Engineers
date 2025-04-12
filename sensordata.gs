const FIREBASE_URL = 'https://upkisaan-6246b-default-rtdb.firebaseio.com/sensorData.json';
const MOTOR_UPDATE_URL = 'https://upkisaan-6246b-default-rtdb.firebaseio.com/gsheetdata/motor.json';
const AUTO_MODE_URL = 'https://upkisaan-6246b-default-rtdb.firebaseio.com/merged1/AutomaticWater.json';

// CONFIGURATION
const EMAIL_RECIPIENT = "mahishaiva0822@gmail.com";
const TELEGRAM_BOT_TOKEN = "7586406293:AAHDhtOSXCQbTudVzs-nEiNaxRbU3zDl6xY";
const TELEGRAM_CHAT_ID = "5322613212";

function importFirebaseData() {
  const sheet = SpreadsheetApp.getActiveSpreadsheet().getActiveSheet();
  const response = UrlFetchApp.fetch(FIREBASE_URL);
  const data = JSON.parse(response.getContentText());

  if (!data) return;

  const sampleKey = Object.keys(data)[0];
  const sampleEntry = data[sampleKey];
  const expectedHeaders = ['EntryID', ...Object.keys(sampleEntry)];

  const existingHeader = sheet.getRange(1, 1, 1, expectedHeaders.length).getValues()[0];
  const isHeaderPresent = expectedHeaders.every((header, i) => existingHeader[i] === header);

  if (!isHeaderPresent) {
    sheet.clear(); // optional
    sheet.appendRow(expectedHeaders);
  }

  let existingIDs = [];
  if (sheet.getLastRow() > 1) {
    existingIDs = sheet.getRange(2, 1, sheet.getLastRow() - 1, 1).getValues().flat();
  }

  let latestEntry = null;

  for (const [entryID, entry] of Object.entries(data)) {
    if (existingIDs.includes(entryID)) continue;
    const row = [entryID, ...expectedHeaders.slice(1).map(k => entry[k] || "")];
    sheet.appendRow(row);
    latestEntry = entry;
  }

  if (!latestEntry && sheet.getLastRow() > 1) {
    const lastRow = sheet.getRange(sheet.getLastRow(), 1, 1, sheet.getLastColumn()).getValues()[0];
    latestEntry = {};
    for (let i = 1; i < expectedHeaders.length; i++) {
      latestEntry[expectedHeaders[i]] = lastRow[i];
    }
  }

  if (latestEntry) {
    controlMotorBasedOnMoisture(latestEntry);
  }
}

function controlMotorBasedOnMoisture(entry) {
  const autoModeResponse = UrlFetchApp.fetch(AUTO_MODE_URL);
  const isAutoMode = JSON.parse(autoModeResponse.getContentText());

  if (isAutoMode !== 1) {
    Logger.log("AutomaticWater is OFF. Skipping motor control.");
    return;
  }

  const sm30 = parseInt(entry.soilMoisture30);
  const sm60 = parseInt(entry.soilMoisture60);

  const MOISTURE_THRESHOLD_30 = 3400;
  const MOISTURE_THRESHOLD_60 = 3600;

  let motor = "0"; // default OFF

  if (sm30 >= MOISTURE_THRESHOLD_30 && sm60 >= MOISTURE_THRESHOLD_60) {
    motor = "1"; // Turn ON motor
  }

  const motorData = JSON.stringify({ motor });

  UrlFetchApp.fetch(MOTOR_UPDATE_URL, {
    method: "put",
    contentType: "application/json",
    payload: motorData,
  });
}

function checkRainStatusAndNotify() {
  const sheet = SpreadsheetApp.getActiveSpreadsheet().getActiveSheet();
  const data = sheet.getDataRange().getValues();
  const headers = data[0];

  const rainStatusIndex = headers.indexOf("rainStatus");
  const entryIDIndex = headers.indexOf("EntryID");
  const timestampIndex = headers.indexOf("timestamp");
  const sm30Index = headers.indexOf("soilMoisture30");
  const sm60Index = headers.indexOf("soilMoisture60");

  for (let i = 1; i < data.length; i++) {
    const row = data[i];
    const rainStatus = row[rainStatusIndex];

    if (rainStatus == 1 && !row.includes("✅")) {
      const entryID = row[entryIDIndex];
      const timestamp = row[timestampIndex];
      const sm30 = row[sm30Index];
      const sm60 = row[sm60Index];

      const motorStatusResponse = UrlFetchApp.fetch(MOTOR_UPDATE_URL);
      const motorData = JSON.parse(motorStatusResponse.getContentText());
      const motorStatus = motorData.motor || "Unknown";

      const message = `🌧️ *Rain detected!*\n🆔 EntryID: ${entryID}\n⏰ Time: ${timestamp}\n🌱 Moisture30: ${sm30}\n🌱 Moisture60: ${sm60}\n🔌 Motor Status: ${motorStatus}`;

      MailApp.sendEmail(EMAIL_RECIPIENT, "Rain Alert 🌧️", message);
      sendTelegram(message);

      sheet.getRange(i + 1, headers.length + 1).setValue("✅");
    }
  }
}

function sendTelegram(message) {
  const url = `https://api.telegram.org/bot${TELEGRAM_BOT_TOKEN}/sendMessage`;
  const payload = {
    chat_id: TELEGRAM_CHAT_ID,
    text: message,
    parse_mode: "Markdown"
  };

  const options = {
    method: "post",
    contentType: "application/json",
    payload: JSON.stringify(payload)
  };

  UrlFetchApp.fetch(url, options);
}

// Call this once to create a time-based trigger
function createTrigger() {
  ScriptApp.newTrigger("importFirebaseData")
           .timeBased()
           .everyMinutes(1)
           .create();

  ScriptApp.newTrigger("checkRainStatusAndNotify")
           .timeBased()
           .everyMinutes(1)
           .create();
}
