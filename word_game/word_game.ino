#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#define MAX_PLAYER 6
#define MAX_WAITING 6
#define TURN_TIME 5

// Initial
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

struct Player {
  uint32_t id;
  uint8_t point;      // MAX 255
  uint8_t active;     // bool 1/0
  char name[20];      // simpan deviceId dari client
  char deviceId[32];  // simpan deviceId dari client
};

struct WaitingPlayer {
  uint32_t id;
  char name[20];      // simpan deviceId dari client
  char deviceId[32];  // simpan deviceId dari client
  uint8_t active;     // bool 1/0
};

Player players[MAX_PLAYER];
WaitingPlayer waitingPlayers[MAX_WAITING];

const char* kataGame[] = {
  // Buah
  "pisang", "mangga", "pepaya", "durian", "alpukat",
  "semangka", "manggis", "rambutan", "kelengkeng", "markisa",
  "cempedak", "kedondong", "belimbing", "sirsak",

  // Sayur
  "wortel", "kentang", "buncis", "kacang", "terong",
  "brokoli", "kembangkol", "seledri", "selada",

  // Hewan
  "jerapah", "harimau", "kangguru", "beruang", "serigala",
  "gorila", "kucing", "anjing", "kelinci", "monyet", "merpati"
};

const int jumlahKata = sizeof(kataGame) / sizeof(kataGame[0]);

// Wifi
const char* ssid = "ESP32_GAME";
const char* password = "12345678";

// Timer
const long interval = 1000;
unsigned long previousMillis = 0;
unsigned long currentMillis = 0;
uint8_t timerGame = TURN_TIME;

// Current Turn
char currentTurnDeviceId[32];
char currentTurnName[20];
uint8_t currentTurnIndex = 0;

// Current Word
char currentWord[16];
char displayWord[16];

bool gameStarted = false;
bool pauseGame = false;
unsigned long pauseStart = 0;
const unsigned long pauseDuration = 3000;

void addPlayer(uint32_t id, const char* deviceId, const char* name) {
  // cek apakah sudah ada (reconnect)
  for (int i = 0; i < MAX_PLAYER; i++) {
    if (strcmp(players[i].deviceId, deviceId) == 0) {
      strncpy(players[i].name, name, sizeof(players[i].name) - 1);
      players[i].name[19] = '\0';
      players[i].id = id;
      players[i].active = 1;
      Serial.println("Player Reconnected");
      updateWordDisplay();
      sendLeaderboard();
      startGame();
      return;
    }
  }

  // cari slot kosong
  for (int i = 0; i < MAX_PLAYER; i++) {
    if (players[i].active == 0) {
      players[i].id = id;
      players[i].point = 0;
      players[i].active = 1;

      strncpy(players[i].deviceId, deviceId, sizeof(players[i].deviceId) - 1);
      players[i].deviceId[31] = '\0';

      strncpy(players[i].name, name, sizeof(players[i].name) - 1);
      players[i].name[19] = '\0';

      Serial.println("Player Added:");
      Serial.println(players[i].deviceId);

      startGame();
      return;
    }
  }

  Serial.println("Room Full");
}

void onWebSocketEvent(AsyncWebSocket* server,
                      AsyncWebSocketClient* client,
                      AwsEventType type,
                      void* arg,
                      uint8_t* data,
                      size_t len) {

  if (type == WS_EVT_CONNECT) {
    client->text("C?");
  }

  else if (type == WS_EVT_DISCONNECT) {
    for (int i = 0; i < MAX_PLAYER; i++) {
      if (players[i].id == client->id()) {
        players[i].active = 0;
      }

      if (i == currentTurnIndex) {
        nextTurn();  // langsung pindah
      }
    }


    for (int i = 0; i < MAX_WAITING; i++) {
      if (waitingPlayers[i].id == client->id()) {
        waitingPlayers[i].active = 0;
      }
    }


    int notActive = 0;
    for (int i = 0; i < MAX_PLAYER; i++) {
      if (players[i].active == 0) {
        notActive++;
      }
    }

    if (notActive == MAX_PLAYER) {
      gameStarted = false;
    }
  }

  else if (type == WS_EVT_DATA) {
    char msg[len + 1];
    memcpy(msg, data, len);
    msg[len] = '\0';

    if (msg[0] == 'J') {
      char* payload = msg + 2;  // skip "J:"

      char* sep = strchr(payload, '|');
      if (sep == nullptr) return;

      *sep = '\0';

      char* deviceId = payload;
      char* name = sep + 1;

      addPlayer(client->id(), deviceId, name);
    }

    if (msg[0] == 'A') {
      handleAnswer(client->id(), msg);
    }
  }
}


void sendLeaderboard() {
  char packet[200];
  char temp[32];
  strcpy(packet, "L:");

  for (int i = 0; i < MAX_PLAYER; i++) {
    // if (players[i].active) {
    snprintf(temp, sizeof(temp), "%s|%u,", players[i].name, players[i].point);
    strcat(packet, temp);
    // }
  }
  if (packet[strlen(packet) - 1] == ',') {
    packet[strlen(packet) - 1] = '\0';
  }

  ws.textAll(packet);
}

void updateWordDisplay() {
  char packet[32];
  snprintf(packet, sizeof(packet), "G:%s", displayWord);
  ws.textAll(packet);
}

void nextTurn() {
  // cari player aktif berikutnya
  do {
    currentTurnIndex = (currentTurnIndex + 1) % MAX_PLAYER;
    strncpy(currentTurnDeviceId, players[currentTurnIndex].deviceId, sizeof(currentTurnDeviceId) - 1);
    currentTurnDeviceId[31] = '\0';
    strncpy(currentTurnName, players[currentTurnIndex].name, sizeof(currentTurnName) - 1);
    currentTurnName[19] = '\0';
  } while (players[currentTurnIndex].active == 0);

  // reset timer
  timerGame = TURN_TIME;

  // reset millis supaya tidak langsung kurang 1 detik
  previousMillis = millis();

  // kirim info turn baru
  char packet[64];
  snprintf(packet, sizeof(packet), "T:%s|%u|%s", currentTurnName, timerGame, currentTurnDeviceId);
  ws.textAll(packet);  // kirim ke semua client
}

void handleAnswer(uint32_t id, const char* msg) {
  // msg format: "A:pisang"
  const char* answer = msg + 2;

  if (players[currentTurnIndex].id != id) {
    return;
  }

  // ✔ cek jawaban
  // for (uint8_t i = 0; i < strlen(currentWord); i++) {
  // displayWord[i] = '_';
  // }
  // Serial.println(answer);

  if (strcmp(answer, currentWord) == 0) {
    // benar
    for (size_t i = 0; i < strlen(displayWord); i++) {
      if (displayWord[i] == '_') {
        players[currentTurnIndex].point++;
      }
    }

    ws.textAll("R:CORRECT");
    // reveal full word
    strcpy(displayWord, currentWord);
    updateWordDisplay();
    sendLeaderboard();

    pauseGame = true;
    pauseStart = millis();
    // restartGame();
    // nextTurn();
  } else {
    // salah
    ws.text(id, "R:WRONG");
    size_t wordLen = strlen(currentWord);
    size_t answerLen = strlen(answer);
    int correctIndexes[16];
    int count = 0;

    // 1️⃣ collect semua huruf yang cocok
    for (size_t i = 0; i < wordLen && i < answerLen; i++) {
      if (answer[i] == currentWord[i] && displayWord[i] == '_') {
        correctIndexes[count++] = i;
      }
    }

    // 2️⃣ kalau ada yang cocok
    if (count > 0) {

      int randomIndex = correctIndexes[random(count)];

      displayWord[randomIndex] = currentWord[randomIndex];

      players[currentTurnIndex].point++;
      sendLeaderboard();
    }
    updateWordDisplay();

    // optional: langsung next turn
    nextTurn();
  }
}

void restartGame() {
  // Pilih kata random
  loadNewWord();
  updateWordDisplay();
}

void startGame() {
  if (gameStarted == true) {
    return;
  }

  // 1️⃣ cek minimal player
  uint8_t count = 0;
  for (int i = 0; i < MAX_PLAYER; i++) {
    if (players[i].active == 1) count++;
  }

  if (count < 2) {
    Serial.println("Not enough players");
    return;
  }

  // 2️⃣ reset semua score
  for (int i = 0; i < MAX_PLAYER; i++) {
    if (players[i].active == 1) {
      players[i].point = 0;
    }
  }

  // 3️⃣ pilih turn pertama
  nextTurn();
  gameStarted = true;

  // 4️⃣ ambil kata pertama
  loadNewWord();

  // 5️⃣ reset timer
  timerGame = TURN_TIME;
  previousMillis = millis();

  // 6️⃣ kirim state ke semua client
  // ws.textAll("S:START");

  updateWordDisplay();
  sendLeaderboard();

  Serial.println("Game Started");
}

void updateTimer() {
  if (gameStarted) {
    currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
      previousMillis = currentMillis;


      if (timerGame > 0) {
        timerGame--;


        char packet[64];
        snprintf(packet, sizeof(packet), "T:%s|%u|%s", currentTurnName, timerGame, currentTurnDeviceId);
        ws.textAll(packet);  // kirim ke semua client
        // Serial.println(message);

        if (timerGame == 0) {
          nextTurn();
        }
      }
    }
  }
}

void loadNewWord() {
  int index = random(jumlahKata);
  strcpy(currentWord, kataGame[index]);
  int randomFirst = random(strlen(currentWord));
  for (uint8_t i = 0; i < strlen(currentWord); i++) {
    displayWord[i] = randomFirst == i ? currentWord[i] : '_';
  }
  displayWord[strlen(currentWord)] = '\0';
  Serial.println(currentWord);
}

void resetVaribles() {
}

void setup() {
  Serial.begin(115200);

  bool ok = WiFi.softAP(ssid, NULL, 1, 0, 12);
  Serial.println(ok ? "AP Started" : "AP Failed");
  Serial.println(WiFi.softAPIP());

  randomSeed(micros());  // penting supaya random tidak sama terus

  ws.onEvent(onWebSocketEvent);
  server.addHandler(&ws);

  // index.html langsung di-serve
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/html", R"rawliteral(
<!DOCTYPE html>
<html lang="en">

<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Document</title>
    <style>
        body {
            font-family: Arial;
            background: #0f172a;
            display: flex;
            justify-content: center;
            align-items: center;
            height: 100vh;
            margin: 0;
            color: white;
        }

        .card {
            background: #1e293b;
            padding: 20px;
            border-radius: 16px;
            width: 320px;
            box-shadow: 0 10px 25px rgba(0, 0, 0, 0.4);
        }

        .row {
            display: flex;
            gap: 10px;
            margin-bottom: 10px;
        }

        .info-bar {
            justify-content: space-between;
            font-weight: bold;
            color: #38bdf8;
        }

        input {
            flex: 1;
            padding: 10px;
            border-radius: 10px;
            border: none;
            outline: none;
        }

        button {
            padding: 10px 12px;
            border-radius: 10px;
            border: none;
            background: #38bdf8;
            font-weight: bold;
            cursor: pointer;
        }

        button:hover {
            background: #0ea5e9;
        }

        #guessword {
            letter-spacing: 8px;
            font-size: 20px;
            text-align: center;
            margin: 15px 0 5px;
        }

        #guess {
            letter-spacing: 8px;
            font-size: 20px;
            text-align: center;
            margin: 15px 0 5px;
        }

        #leaderboard {
            position: absolute;
            top: 10px;
            right: 10px;
            width: 180px;
            background: rgba(0, 0, 0, 0.6);
            color: white;
            font-family: Arial;
            padding: 10px;
            border-radius: 8px;
            font-size: 14px;
        }

        .player {
            display: flex;
            justify-content: space-between;
            margin: 2px 0;
        }
    </style>
</head>
<body>
    <div id="leaderboard"></div>
    <div class="card">
        <!-- Turn + Timer -->
        <div class="row info-bar">
            <span id="turnText">Giliran: -</span>
            <span id="timerText">Waktu: 5</span>
        </div>
        <!-- Word display -->
        <p id="guess">Nama:</p>
        <!-- Answer input -->
        <div class="row">
            <input type="text" id="answer" placeholder="Jawaban" maxlength="20">
            <button id="submitBtn" onclick="onSubmit()">Submit</button>
        </div>
    </div>
    <script>
        // CLIENT
        var submitButton = document.getElementById("submitBtn");
        var answerInput = document.getElementById("answer");
        var timerText = document.getElementById("timerText");
        var turnText = document.getElementById("turnText");
        var displayText = document.getElementById("guess");
        var leaderboardDiv = document.getElementById("leaderboard");
        const deviceId = getDeviceId().substring(0, 31);

        function generateId() {
            return 'xxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, function (c) {
                const r = Math.random() * 16 | 0;
                const v = c === 'x' ? r : (r & 0x3 | 0x8);
                return v.toString(16);
            });
        }

        function getDeviceId() {
            let id = localStorage.getItem("device_id");
            if (!id) {
                id = generateId();
                localStorage.setItem("device_id", id);
            }
            return id;
        }

        const gameState = ["JOIN", "WAITING", "PLAYING", "END"];
        var currentGameState = "";
        // const ws = new WebSocket("ws://192.168.4.1/ws");
        const ws = new WebSocket("ws://" + location.hostname + "/ws");
        var ranks = [];
        var players = [];

        ws.onopen = () => console.log("Connected");
        ws.onclose = () => console.log("Disconnected");
        ws.onerror = (e) => console.log("Error", e);

        ws.onmessage = (e) => {
            console.log("Received:", e.data);
            console.log("currentGameState:", currentGameState);
            if (e.data == "C?") {
                currentGameState = gameState[0];
                answerInput.addEventListener("keydown", onEnter);
            }


            if (e.data.startsWith("T:")) {
                let tempTurnTimer = e.data.substring(2).split("|");
                updateTurnAndTimer(tempTurnTimer[0], tempTurnTimer[1]);
                handleDisabled(tempTurnTimer[2]);
            }

            if (e.data.startsWith("G:")) {
                updateDisplayText(e.data.substring(2));
            }

            if (e.data.startsWith("L:")) {
                updateLeaderboard(e.data.substring(2));
            }
        };

        function onEnter(event) {
            if (event.key === "Enter") {
                onSubmit();
            }
        }

        function updateLeaderboard(data) {
            // format: name|point,name|point
            const players = data.split(",");
            players.sort((a, b) => {
                const [name, point] = a.split("|");
                const [name2, point2] = b.split("|");
                return Number(point) < Number(point2);
            });

            let html = "<b>Leaderboard</b><br>";

            players.forEach(p => {
                if (p.trim() === "") return;

                const [name, point] = p.split("|");

                html += `
      <div class="player">
        <span>${name}</span>
        <span>${point}</span>
      </div>
    `;
            });
            leaderboardDiv.innerHTML = html;
        }

        function handleDisabled(data) {
            if (currentGameState == gameState[0]) {
                return;
            }
            if (data == deviceId) {
                submitButton.disabled = false;
                answerInput.addEventListener("keydown", onEnter);

            } else {
                submitButton.disabled = true;
                answerInput.removeEventListener("keydown", onEnter);
            }
        }

        function updateTurnAndTimer(turn, timer) {
            timerText.innerText = "Waktu: " + timer;
            turnText.innerText = "Giliran: " + turn;
        }

        function updateDisplayText(data) {
            displayText.innerText = data;
        }

        function onSubmit() {
            if (currentGameState == gameState[0]) {
                ws.send("J:" + deviceId + "|" + answerInput.value);
                currentGameState = gameState[2];
            } else if (currentGameState == gameState[2]) {
                ws.send("A:" + answerInput.value);
            }
            submitButton.disabled = true;
            answerInput.removeEventListener("keydown", onEnter);
        }
    </script>
</body>
</html>
    )rawliteral");
  });

  server.begin();
}


void loop() {
  if (pauseGame) {
    if (millis() - pauseStart >= pauseDuration) {
      pauseGame = false;

      restartGame();
      nextTurn();
    }
    return; // stop game logic sementara
  }

  updateTimer();
}