#include "esp_camera.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include "esp_timer.h"
#include <WiFi.h>
#include <WebServer.h>
#include "driver/i2s.h"

// Microphone Pin & Channel Definitions
#define I2S_MIC_CLK     42
#define I2S_MIC_DATA    41
#define SAMPLE_RATE     16000U
#define SAMPLE_BITS     16

#define CAMERA_MODEL_XIAO_ESP32S3
#include "camera_pins.h"

const char* ssid = "ShakoCam";
const char* password = "password123";
WebServer server(80);

SemaphoreHandle_t sdMutex;
TaskHandle_t audioTaskHandle = NULL;

const int SD_PIN_CS = 21;

// Separate File Handles for Video and Audio
File videoFile;
File audioFile;

bool camera_sign = false;
bool sd_sign = false;
volatile bool isRecording = false;

char currentFilename[32];  // e.g. /video0.avi
char audioFilename[32];    // e.g. /audio0.wav

int frameCount = 0;
uint32_t audioBytesWritten = 0;

// Helper function to write a standard 44-byte WAV Header
void writeWavHeader(File file, uint32_t sampleRate, uint16_t bitsPerSample, uint16_t channels) {
  uint8_t header[44];
  
  // "RIFF" chunk descriptor
  header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
  uint32_t fileSize = 0; // Temporary placeholder
  memcpy(&header[4], &fileSize, 4);
  header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';

  // "fmt " sub-chunk
  header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
  uint32_t subchunk1Size = 16; // PCM format
  memcpy(&header[16], &subchunk1Size, 4);
  uint16_t audioFormat = 1;   // Uncompressed PCM
  memcpy(&header[20], &audioFormat, 2);
  memcpy(&header[22], &channels, 2);
  memcpy(&header[24], &sampleRate, 4);
  
  uint32_t byteRate = sampleRate * channels * (bitsPerSample / 8);
  memcpy(&header[28], &byteRate, 4);
  uint16_t blockAlign = channels * (bitsPerSample / 8);
  memcpy(&header[32], &blockAlign, 2);
  memcpy(&header[34], &bitsPerSample, 2);

  // "data" sub-chunk
  header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
  uint32_t dataSize = 0; // Temporary placeholder
  memcpy(&header[40], &dataSize, 4);

  file.write(header, 44);
}

void initMicrophone() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 512,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_PIN_NO_CHANGE,
    .ws_io_num = I2S_MIC_CLK,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_MIC_DATA
  };

  esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("I2S driver install failed: %d\n", err);
    return;
  }

  err = i2s_set_pin(I2S_NUM_0, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("I2S pin setup failed: %d\n", err);
    return;
  }

  i2s_start(I2S_NUM_0);
  Serial.println("Lightweight PDM Microphone Initialized!");
}

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; text-align: center; background: #121212; color: white; padding: 15px; margin: 0; }
    .btn { padding: 12px 24px; font-size: 16px; border: none; border-radius: 8px; margin: 5px; cursor: pointer; font-weight: bold; }
    .rec { background-color: #e50914; color: white; }
    .stop { background-color: #555555; color: white; }
    .save { background-color: #28a745; color: white; width: 100%; max-width: 320px; }
    .refresh { background-color: #007aff; color: white; padding: 6px 12px; font-size: 14px; border-radius: 5px; border: none; }
    #status { margin: 10px; font-size: 15px; color: #00ffcc; }
    
    #storageBox { background: #1e1e1e; padding: 12px; border-radius: 8px; margin: 15px auto; max-width: 400px; text-align: left; }
    .bar-bg { background: #333; height: 12px; border-radius: 6px; overflow: hidden; margin-top: 6px; }
    .bar-fill { background: #00ffcc; height: 100%; width: 0%; transition: width 0.4s ease; }

    #playerContainer { display: none; margin: 15px auto; max-width: 400px; background: #1e1e1e; padding: 15px; border-radius: 10px; }
    #canvasDisplay { width: 100%; height: auto; border-radius: 6px; background: #000; }
    
    #gallery { margin-top: 20px; text-align: left; background: #1e1e1e; padding: 15px; border-radius: 10px; max-width: 400px; margin-left: auto; margin-right: auto; }
    .file-item { display: flex; justify-content: space-between; align-items: center; padding: 10px 0; border-bottom: 1px solid #333; }
    .file-info { display: flex; flex-direction: column; }
    .file-meta { font-size: 12px; color: #aaa; margin-top: 3px; }
    .play-btn { background: #007aff; color: white; border: none; padding: 8px 12px; border-radius: 6px; cursor: pointer; font-weight: bold; }
  </style>
</head>
<body>
  <h2>ShakoCam</h2>
  <button class="btn rec" onclick="startRec()">RECORD</button>
  <button class="btn stop" onclick="stopRec()">STOP</button>
  <div id="status">Status: Ready</div>

  <div id="storageBox">
    <div style="display:flex; justify-content:space-between; font-size:13px;">
      <span>SD Card Storage</span>
      <span id="storageText">Loading...</span>
    </div>
    <div class="bar-bg">
      <div id="storageBar" class="bar-fill"></div>
    </div>
  </div>

  <div id="playerContainer">
    <h4 id="nowPlaying" style="margin: 5px 0 10px 0;">Video Player</h4>
    <canvas id="canvasDisplay"></canvas>
    <audio id="audioPlayer" controls style="width: 100%; margin-top: 10px; display: none;"></audio>
    <br><br>
    <button class="btn save" id="dlBtn" onclick="saveAsMP4()">Save to Phone</button>
  </div>

  <div id="gallery">
    <div style="display: flex; justify-content: space-between; align-items: center;">
      <h3 style="margin:0;">Saved Clips</h3>
      <button class="refresh" onclick="loadGallery()">Refresh</button>
    </div>
    <div id="fileList" style="margin-top: 10px;">Loading files...</div>
  </div>

  <script>
    let playAnimationId = null;
    let extractedFrames = [];
    let isPlaying = false;
    let activeFilename = "";

    function startRec() {
      fetch('/start');
      document.getElementById('status').innerText = "Status: Recording Video & Audio...";
    }
    
    function stopRec() {
      fetch('/stop');
      document.getElementById('status').innerText = "Status: Stopped & Saved!";
      setTimeout(() => { loadGallery(); loadStorage(); }, 1000);
    }
    
    function loadStorage() {
      fetch('/storage').then(r => r.json()).then(data => {
        const freeMB = data.total - data.used;
        const usedPct = ((data.used / data.total) * 100).toFixed(1);
        document.getElementById('storageText').innerText = (freeMB/1024).toFixed(1) + " GB free of " + (data.total/1024).toFixed(1) + " GB";
        document.getElementById('storageBar').style.width = usedPct + '%';
      }).catch(err => {
        document.getElementById('storageText').innerText = "Unavailable";
      });
    }

    function loadGallery() {
      fetch('/list')
        .then(r => r.json())
        .then(files => {
          let html = '';
          if (!files || files.length === 0) {
            html = '<p>No recordings found.</p>';
          } else {
            files.forEach(f => {
              const sizeMB = (f.size / (1024 * 1024)).toFixed(1);
              html += '<div class="file-item">' +
                '<div class="file-info">' +
                  '<strong>' + f.name + '</strong>' +
                  '<span class="file-meta">Size: ' + sizeMB + ' MB</span>' +
                '</div>' +
                '<button class="play-btn" onclick="playVideo(\'' + f.name + '\')">PLAY</button>' +
              '</div>';
            });
          }
          document.getElementById('fileList').innerHTML = html;
        })
        .catch(err => {
          document.getElementById('fileList').innerHTML = '<p>Error loading list</p>';
        });
    }

    function stopCurrentPlayback() {
      isPlaying = false;
      if (playAnimationId) cancelAnimationFrame(playAnimationId);
      const audioElem = document.getElementById('audioPlayer');
      audioElem.pause();
      audioElem.src = "";
      extractedFrames = [];
    }

    async function playVideo(filename) {
      stopCurrentPlayback();
      activeFilename = filename;

      const playerContainer = document.getElementById('playerContainer');
      const nowPlaying = document.getElementById('nowPlaying');
      const audioElem = document.getElementById('audioPlayer');
      
      playerContainer.style.display = 'block';
      nowPlaying.innerText = "Downloading " + filename + "...";

      try {
        const response = await fetch('/stream?file=' + filename);
        if (!response.ok) throw new Error("Fetch video failed");
        
        nowPlaying.innerText = "Extracting video frames...";
        const arrayBuffer = await response.arrayBuffer();
        const bytes = new Uint8Array(arrayBuffer);
        let i = 0;

        while (i < bytes.length - 1) {
          if (bytes[i] === 0xFF && bytes[i+1] === 0xD8) {
            let start = i;
            i += 2;
            while (i < bytes.length - 1) {
              if (bytes[i] === 0xFF && bytes[i+1] === 0xD9) {
                i += 2;
                extractedFrames.push(bytes.subarray(start, i));
                break;
              }
              i++;
            }
          } else {
            i++;
          }
        }

        if (extractedFrames.length === 0) {
          nowPlaying.innerText = "No video frames found";
          return;
        }

        const audioName = filename.replace("video", "audio").replace(".avi", ".wav");
        audioElem.src = '/stream?file=' + audioName;
        audioElem.style.display = 'block';
        audioElem.load();

        nowPlaying.innerText = "Playing: " + filename;
        isPlaying = true;

        audioElem.onplay = () => { isPlaying = true; syncLoop(); };
        audioElem.onpause = () => { isPlaying = false; };
        audioElem.onended = () => { isPlaying = false; };
        audioElem.onseeking = () => { renderFrameAtAudioClock(); };

        audioElem.play().catch(e => console.log("Audio play interaction required"));

      } catch (err) {
        nowPlaying.innerText = "Error loading clip";
      }
    }

    function syncLoop() {
      if (!isPlaying) return;
      renderFrameAtAudioClock();
      playAnimationId = requestAnimationFrame(syncLoop);
    }

    function renderFrameAtAudioClock() {
      const audioElem = document.getElementById('audioPlayer');
      if (!audioElem.duration || extractedFrames.length === 0) return;

      const progress = audioElem.currentTime / audioElem.duration;
      const frameIndex = Math.min(
        Math.floor(progress * extractedFrames.length),
        extractedFrames.length - 1
      );

      const canvas = document.getElementById('canvasDisplay');
      const ctx = canvas.getContext('2d');
      const frameData = extractedFrames[frameIndex];

      if (!frameData) return;

      const blob = new Blob([frameData], { type: 'image/jpeg' });
      const url = URL.createObjectURL(blob);
      const img = new Image();

      img.onload = () => {
        canvas.width = img.width || 640;
        canvas.height = img.height || 480;
        ctx.drawImage(img, 0, 0);
        URL.revokeObjectURL(url);
      };
      img.src = url;
    }

    async function saveAsMP4() {
      if (extractedFrames.length === 0) return;

      const canvas = document.getElementById('canvasDisplay');
      const audioElem = document.getElementById('audioPlayer');
      const dlBtn = document.getElementById('dlBtn');

      dlBtn.innerText = "Exporting Video...";
      dlBtn.disabled = true;

      const canvasStream = canvas.captureStream(15);
      
      const audioCtx = new (window.AudioContext || window.webkitAudioContext)();
      const audioSource = audioCtx.createMediaElementSource(audioElem);
      const audioDest = audioCtx.createMediaStreamDestination();
      
      audioSource.connect(audioCtx.destination);
      audioSource.connect(audioDest);

      const combinedStream = new MediaStream([
        ...canvasStream.getVideoTracks(),
        ...audioDest.stream.getAudioTracks()
      ]);

      const types = [
        'video/mp4;codecs=avc1.42E01E,mp4a.40.2',
        'video/mp4;codecs=h264,aac',
        'video/mp4',
        'video/webm;codecs=vp8,opus',
        'video/webm'
      ];

      let selectedMime = '';
      for (let t of types) {
        if (MediaRecorder.isTypeSupported(t)) {
          selectedMime = t;
          break;
        }
      }

      if (!selectedMime) {
        alert("MediaRecorder is not supported on this browser.");
        dlBtn.innerText = "Save to Phone";
        dlBtn.disabled = false;
        return;
      }

      const recorder = new MediaRecorder(combinedStream, { 
        mimeType: selectedMime,
        audioBitsPerSecond: 128000
      });

      const chunks = [];

      recorder.ondataavailable = (e) => { 
        if (e.data && e.data.size > 0) chunks.push(e.data); 
      };
      
      recorder.onstop = () => {
        const blob = new Blob(chunks, { type: selectedMime });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        
        const isMp4 = selectedMime.includes('mp4');
        const ext = isMp4 ? '.mp4' : '.webm';
        a.download = activeFilename.replace('.avi', ext);
        
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);

        dlBtn.innerText = "Downloaded!";
        dlBtn.disabled = false;
        setTimeout(() => { dlBtn.innerText = "Save to Phone"; }, 3000);
      };

      audioElem.currentTime = 0;
      await audioElem.play();
      recorder.start(100);

      audioElem.onended = () => {
        recorder.stop();
        audioCtx.close();
      };
    }
    
    window.onload = () => {
      loadGallery();
      loadStorage();
    };
  </script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

void audioTask(void *pvParameters) {
  uint8_t audioBuffer[512];
  size_t bytesRead = 0;

  for (;;) {
    if (isRecording) {
      i2s_read(I2S_NUM_0, audioBuffer, sizeof(audioBuffer), &bytesRead, pdMS_TO_TICKS(10));

      if (bytesRead > 0) {
        // --- HIGH VOLUME ATTENUATION FOR BRASS (DIVIDE BY 4) ---
        int16_t *samples = (int16_t *)audioBuffer;
        size_t sampleCount = bytesRead / 2;
        for (size_t i = 0; i < sampleCount; i++) {
          samples[i] = samples[i] / 4; 
        }

        // Non-blocking SD write with a 10ms timeout
        if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
          if (audioFile) {
            audioFile.write(audioBuffer, bytesRead);
            audioBytesWritten += bytesRead;
          }
          xSemaphoreGive(sdMutex);
        }
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(20));
    }
  }
}

void handleStart() {
  if (!camera_sign || !sd_sign) {
    server.send(500, "text/plain", "Hardware Error");
    return;
  }

  if (!isRecording) {
    // Flush 10 dummy frames to completely warm up the OV3660 camera DMA pipeline
    for (int i = 0; i < 10; i++) {
      camera_fb_t *dummy = esp_camera_fb_get();
      if (dummy) esp_camera_fb_return(dummy);
      vTaskDelay(pdMS_TO_TICKS(15));
    }

    if (videoFile) videoFile.close();
    if (audioFile) audioFile.close();

    int fileNumber = 0;
    while (true) {
      sprintf(currentFilename, "/video%d.avi", fileNumber);
      sprintf(audioFilename, "/audio%d.wav", fileNumber);
      if (!SD.exists(currentFilename) && !SD.exists(audioFilename)) break;
      fileNumber++;
    }

    videoFile = SD.open(currentFilename, FILE_WRITE);
    audioFile = SD.open(audioFilename, FILE_WRITE);

    if (videoFile && audioFile) {
      writeWavHeader(audioFile, 16000, 16, 1);
      audioBytesWritten = 0;

      isRecording = true;
      frameCount = 0;
      Serial.printf("\n--- [PHONE] Started Recording: %s & %s ---\n", currentFilename, audioFilename);
    }
  }
  server.send(200, "text/plain", "RECORDING");
}

void handleStop() {
  if (isRecording) {
    isRecording = false;

    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
      if (videoFile) {
        videoFile.flush();
        videoFile.close();
      }

      if (audioFile) {
        uint32_t totalFileSize = audioBytesWritten + 36;
        audioFile.seek(4);
        audioFile.write((uint8_t*)&totalFileSize, 4);

        audioFile.seek(40);
        audioFile.write((uint8_t*)&audioBytesWritten, 4);

        audioFile.flush();
        audioFile.close();
      }

      xSemaphoreGive(sdMutex);
    }

    Serial.printf(
      "--- [PHONE] Stopped! Saved %d frames (%s) & %d audio bytes (%s) ---\n\n",
      frameCount, currentFilename, audioBytesWritten, audioFilename
    );
  }

  server.send(200, "text/plain", "STOPPED");
}

void handleStorage() {
  uint64_t totalBytes = SD.totalBytes();
  uint64_t usedBytes = SD.usedBytes();
  
  String json = "{";
  json += "\"total\":" + String((unsigned long)(totalBytes / (1024 * 1024))) + ",";
  json += "\"used\":" + String((unsigned long)(usedBytes / (1024 * 1024)));
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleStream() {
  if (!server.hasArg("file")) {
    server.send(400, "text/plain", "Missing file parameter");
    return;
  }

  String fileName = server.arg("file");
  if (!fileName.startsWith("/")) {
    fileName = "/" + fileName;
  }

  File streamFile = SD.open(fileName, FILE_READ);
  if (!streamFile) {
    server.send(404, "text/plain", "File not found");
    return;
  }

  String dataType = "application/octet-stream";
  if (fileName.endsWith(".wav")) {
    dataType = "audio/wav";
  } else if (fileName.endsWith(".avi")) {
    dataType = "video/x-motion-jpeg";
  }

  server.setContentLength(streamFile.size());
  server.send(200, dataType, "");

  WiFiClient client = server.client();
  uint8_t buffer[1024];

  while (streamFile.available()) {
    int bytesRead = streamFile.read(buffer, sizeof(buffer));
    client.write(buffer, bytesRead);
  }

  streamFile.close();
}

void handleList() {
  File root = SD.open("/");
  if (!root) {
    server.send(500, "application/json", "[]");
    return;
  }

  String json = "[";
  File file = root.openNextFile();
  bool first = true;

  while (file) {
    String fileName = String(file.name());
    if (fileName.startsWith("/")) {
      fileName = fileName.substring(1);
    }

    if (!file.isDirectory() && fileName.endsWith(".avi")) {
      if (!first) json += ",";
      json += "{\"name\":\"" + fileName + "\",\"size\":" + String(file.size()) + "}";
      first = false;
    }
    file = root.openNextFile();
  }
  json += "]";
  root.close();
  
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 10000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA; // Scaled down to 320x240 for stable SD writing
  
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 18;
  config.fb_count = 2;

  esp_err_t err = esp_camera_init(&config);
  if (err == ESP_OK) {
    camera_sign = true;
    Serial.println("OV3660 Camera Initialized Successfully!");

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
      s->set_vflip(s, 1);
      s->set_hmirror(s, 0);
      s->set_gain_ctrl(s, 0);      // Auto Gain Control OFF
      s->set_exposure_ctrl(s, 1);  // Auto Exposure ON
      s->set_awb_gain(s, 1);       // Auto White Balance ON
      s->set_brightness(s, 0);     // Neutral Brightness
    }
  } else {
    Serial.printf("OV3660 Camera init failed with error 0x%x\n", err);
  }

  if (SD.begin(SD_PIN_CS)) {
    if (SD.cardType() != CARD_NONE) sd_sign = true;
  }
  
  initMicrophone();
  
  sdMutex = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(
    audioTask,
    "AudioTask",
    4096,
    NULL,
    1,
    &audioTaskHandle,
    0
  );

  WiFi.softAP(ssid, password);
  
  server.on("/", handleRoot);
  server.on("/start", handleStart);
  server.on("/stop", handleStop);
  server.on("/list", handleList);
  server.on("/stream", handleStream);
  server.on("/storage", handleStorage);
  server.begin();
}

void loop() {
  server.handleClient();

  if (isRecording) {
    camera_fb_t *fb = esp_camera_fb_get();
    
    if (fb) {
      // Non-blocking SD write with a 10ms timeout
      if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (videoFile) {
          videoFile.write(fb->buf, fb->len);
          frameCount++;
        }
        xSemaphoreGive(sdMutex);
      }
      esp_camera_fb_return(fb);
    }

    if (frameCount % 30 == 0) {
      if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (videoFile) videoFile.flush();
        if (audioFile) audioFile.flush();
        xSemaphoreGive(sdMutex);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20)); // Targets ~15 FPS capture rate
  }
}
