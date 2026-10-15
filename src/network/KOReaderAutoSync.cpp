#include "KOReaderAutoSync.h"

#include <ArduinoJson.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <KOReaderCredentialStore.h>
#include <KOReaderSyncClient.h>
#include <Logging.h>
#include <PersistableStore.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <mutex>
#include <utility>
#include <vector>

#include "WifiCredentialStore.h"
#include "network/WifiPowerSaveGuard.h"

namespace {

constexpr int FORMAT_VERSION = 1;
constexpr uint16_t MIN_BATTERY_PERCENT = 20;
constexpr unsigned long JOIN_TIMEOUT_MS = 10000;
// Sleep delivery budget on top of the join: a handful of books, each one
// HTTPS request, without holding the device awake for long.
constexpr unsigned long SLEEP_UPLOAD_BUDGET_MS = 15000;
// A failed attempt (no network, server down) waits this long before the next
// background try; queuing a newer position retries sooner.
constexpr unsigned long RETRY_BACKOFF_MS = 5UL * 60UL * 1000UL;
constexpr unsigned long POLL_INTERVAL_MS = 3000;
constexpr unsigned long STOP_WAIT_MS = 20000;
// Background start gate: the WiFi driver and one TLS session (35 KB, 20 KB
// largest block, see KOReaderSyncClient) on top of what the UI already uses.
constexpr size_t MIN_INTERNAL_FREE = 110 * 1024;
constexpr size_t MIN_INTERNAL_BLOCK = 40 * 1024;
// The manual sync runs TLS on the loop task (8 KB on C3, 24 KB with vector
// fonts); 16 KB leaves headroom for wolfSSL's handshake plus the JSON request.
constexpr uint32_t TASK_STACK_BYTES = 16 * 1024;
constexpr size_t SIDECAR_CAP = 2 * 1024;

class KOReaderQueueStore : public PersistableStore<KOReaderQueueStore> {
  friend class PersistableStore<KOReaderQueueStore>;
  KOReaderQueueStore() = default;
  ~KOReaderQueueStore() = default;

 public:
  koreader::ProgressQueue queue;

  static const char* getFilePath() { return "/.crosspoint/koreader_queue.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);
};

void KOReaderQueueStore::toJson(JsonDocument& doc) const {
  doc["version"] = FORMAT_VERSION;
  doc["next"] = queue.nextRevision();
  JsonArray entries = doc["entries"].to<JsonArray>();
  for (const auto& item : queue.entries()) {
    JsonObject obj = entries.add<JsonObject>();
    obj["doc"] = item.document;
    obj["path"] = item.bookPath;
    obj["xpath"] = item.xpath;
    obj["pct"] = item.percentage;
    obj["spine"] = item.spineIndex;
    obj["page"] = item.pageNumber;
    obj["pages"] = item.totalPages;
    if (item.paragraphIndex) obj["para"] = *item.paragraphIndex;
    obj["rev"] = item.revision;
    if (item.metadata) {
      JsonObject meta = obj["meta"].to<JsonObject>();
      meta["file"] = item.metadata->filename;
      meta["title"] = item.metadata->title;
      meta["authors"] = item.metadata->authors;
      meta["isbn"] = item.metadata->isbn;
      meta["asin"] = item.metadata->asin;
      meta["series"] = item.metadata->series;
      if (item.metadata->seriesIndex) meta["seriesIndex"] = *item.metadata->seriesIndex;
    }
  }
}

bool KOReaderQueueStore::fromJson(JsonVariantConst doc) {
  std::vector<koreader::QueuedProgress> entries;
  JsonArrayConst stored = doc["entries"].as<JsonArrayConst>();
  entries.reserve(stored.size());
  for (JsonObjectConst obj : stored) {
    koreader::QueuedProgress item;
    item.document = obj["doc"] | "";
    if (item.document.empty()) continue;
    item.bookPath = obj["path"] | "";
    item.xpath = obj["xpath"] | "";
    item.percentage = obj["pct"] | 0.0f;
    item.spineIndex = obj["spine"] | static_cast<uint16_t>(0);
    item.pageNumber = obj["page"] | static_cast<uint16_t>(0);
    item.totalPages = obj["pages"] | static_cast<uint16_t>(1);
    if (obj["para"].is<uint16_t>()) item.paragraphIndex = obj["para"].as<uint16_t>();
    item.revision = obj["rev"] | 0u;
    JsonObjectConst meta = obj["meta"].as<JsonObjectConst>();
    if (!meta.isNull()) {
      KOReaderMetadata metadata;
      metadata.filename = meta["file"] | "";
      metadata.title = meta["title"] | "";
      metadata.authors = meta["authors"] | "";
      metadata.isbn = meta["isbn"] | "";
      metadata.asin = meta["asin"] | "";
      metadata.series = meta["series"] | "";
      if (meta["seriesIndex"].is<float>()) metadata.seriesIndex = meta["seriesIndex"].as<float>();
      item.metadata = std::move(metadata);
    }
    entries.push_back(std::move(item));
  }
  queue.restore(std::move(entries), doc["next"] | 1u);
  return true;
}

#define KOREADER_QUEUE KOReaderQueueStore::getInstance()

// Guards KOREADER_QUEUE.queue: the main task queues, the upload task removes.
// Lock order: queueMutex, then the store's own mutex inside saveToFile().
std::mutex queueMutex;

std::atomic<bool> uploadRunning{false};
std::atomic<bool> stopRequested{false};
// Set when stopAndWait() gave up waiting: a request stuck in its network
// timeout must not switch the radio off under the foreground that took it.
std::atomic<bool> radioHandedOver{false};
// Written by the main task only while no upload runs.
WifiCredential uploadNetwork;
// Earliest next background attempt (0 = now); the upload task sets the backoff.
std::atomic<unsigned long> nextAttemptMs{0};
unsigned long lastPollMs = 0;

std::vector<koreader::QueuedProgress> snapshotQueue() {
  std::lock_guard<std::mutex> lock(queueMutex);
  return KOREADER_QUEUE.queue.entries();
}

void markUploaded(const koreader::QueuedProgress& item) {
  std::lock_guard<std::mutex> lock(queueMutex);
  if (KOREADER_QUEUE.queue.removeIfUnchanged(item.document, item.revision) && !KOREADER_QUEUE.saveToFile()) {
    LOG_ERR("KOAUTO", "Cannot save the upload queue");
  }
}

KOReaderProgress toRequest(const koreader::QueuedProgress& item) {
  KOReaderProgress progress;
  progress.document = item.document;
  progress.progress = item.xpath;
  progress.percentage = item.percentage;
  progress.timestamp = 0;
  if (KOREADER_STORE.supportsRichProgress()) {
    KOReaderRichPosition position;
    const float pct = item.percentage < 0.0f ? 0.0f : (item.percentage > 1.0f ? 1.0f : item.percentage);
    position.pctQ = static_cast<uint32_t>(pct * 1000000.0f + 0.5f);
    position.spineIndex = item.spineIndex;
    position.pageNumber = item.pageNumber;
    position.totalPages = item.totalPages > 0 ? item.totalPages : 1;
    position.paragraphIndex = item.paragraphIndex;
    position.xpath = item.xpath;
    progress.position = std::move(position);
  }
  if (item.metadata && KOREADER_STORE.getSendMetadata()) {
    KOReaderMetadata metadata = *item.metadata;
    // Same sidecar the manual upload sends; usually absent.
    Storage.readFileToString("KOAUTO", item.bookPath + ".meta.json", SIDECAR_CAP, metadata.extraJson);
    progress.metadata = std::move(metadata);
  }
  return progress;
}

// Uploads queued books, oldest first. Returns true when every queued book was
// pushed: false means a refusal, a stop request, a network loss or an expired
// deadline left work for later, so the caller backs off before retrying.
bool uploadQueue(const unsigned long deadlineMs) {
  WifiPowerSaveGuard powerSave;
  bool allPushed = true;
  for (const auto& item : snapshotQueue()) {
    if (stopRequested.load() || WiFi.status() != WL_CONNECTED) return false;
    if (deadlineMs != 0 && static_cast<long>(millis() - deadlineMs) >= 0) return false;
    const KOReaderSyncClient::Error result = KOReaderSyncClient::updateProgress(toRequest(item));
    switch (result) {
      case KOReaderSyncClient::OK:
        markUploaded(item);
        LOG_INF("KOAUTO", "Progress pushed for %s", item.bookPath.c_str());
        break;
      case KOReaderSyncClient::SERVER_ERROR:
      case KOReaderSyncClient::JSON_ERROR:
      case KOReaderSyncClient::NOT_FOUND:
        // This book was refused; the others may still go through. It stays
        // queued and is retried after the backoff, until newer books push it
        // out of the queue.
        LOG_ERR("KOAUTO", "Push refused for %s: %s", item.bookPath.c_str(), KOReaderSyncClient::errorString(result));
        allPushed = false;
        break;
      default:
        // Credentials, network or memory: every later request would fail too.
        LOG_ERR("KOAUTO", "Push stopped: %s", KOReaderSyncClient::errorString(result));
        return false;
    }
  }
  return allPushed;
}

bool joinNetwork(const WifiCredential& network) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(network.ssid.c_str(), network.password.c_str());
  const unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < JOIN_TIMEOUT_MS) {
    if (stopRequested.load()) return false;
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  return WiFi.status() == WL_CONNECTED;
}

void releaseRadio() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

bool readyToUpload() {
  return KOREADER_STORE.hasCredentials() && powerManager.getBatteryPercentage() >= MIN_BATTERY_PERCENT;
}

#if BOARD_HAS_PSRAM
void uploadTask(void*) {
  bool complete = false;
  if (joinNetwork(uploadNetwork)) {
    complete = uploadQueue(0);
  } else if (!stopRequested.load()) {
    LOG_DBG("KOAUTO", "WiFi join timed out; progress stays queued");
  }
  LOG_DBG("KOAUTO", "Push task stack high water: %u bytes",
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  // A foreground screen that stopped waiting owns the radio now: leave it on.
  if (!radioHandedOver.load()) releaseRadio();
  // A stop request is a foreground hand-over, not a failure: retry soon.
  if (!complete && !stopRequested.load()) nextAttemptMs = millis() + RETRY_BACKOFF_MS;
  uploadRunning.store(false);
  vTaskDelete(nullptr);
}

void startBackgroundUpload() {
  const auto network = WIFI_STORE.findCredential(WIFI_STORE.getLastConnectedSsid());
  if (!network) return;
  if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < MIN_INTERNAL_FREE ||
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < MIN_INTERNAL_BLOCK) {
    LOG_DBG("KOAUTO", "Not enough heap for a background push; waiting");
    nextAttemptMs = millis() + RETRY_BACKOFF_MS;
    return;
  }
  uploadNetwork = *network;
  stopRequested.store(false);
  radioHandedOver.store(false);
  uploadRunning.store(true);
  if (xTaskCreate(&uploadTask, "KOReaderPush", TASK_STACK_BYTES, nullptr, 1, nullptr) != pdPASS) {
    LOG_ERR("KOAUTO", "Cannot start the push task");
    uploadRunning.store(false);
    nextAttemptMs = millis() + RETRY_BACKOFF_MS;
  }
}
#endif

}  // namespace

namespace koreaderautosync {

void begin() {
  std::lock_guard<std::mutex> lock(queueMutex);
  KOREADER_QUEUE.loadFromFile();
}

void enqueue(koreader::QueuedProgress entry) {
  if (!KOREADER_STORE.hasCredentials() || entry.document.empty()) return;
  {
    std::lock_guard<std::mutex> lock(queueMutex);
    KOREADER_QUEUE.queue.upsert(std::move(entry));
    if (!KOREADER_QUEUE.saveToFile()) LOG_ERR("KOAUTO", "Cannot save the upload queue");
  }
  nextAttemptMs = 0;  // fresh progress: try at the next opportunity
}

bool hasPending() {
  std::lock_guard<std::mutex> lock(queueMutex);
  return !KOREADER_QUEUE.queue.empty();
}

void poll(const bool readerOpen) {
#if BOARD_HAS_PSRAM
  const unsigned long now = millis();
  if (now - lastPollMs < POLL_INTERVAL_MS) return;
  lastPollMs = now;
  // Never while reading (the book owns the heap) nor while the foreground
  // uses the radio.
  if (readerOpen || uploadRunning.load() || WiFi.getMode() != WIFI_OFF) return;
  const unsigned long notBefore = nextAttemptMs.load();
  if (notBefore != 0 && static_cast<long>(now - notBefore) < 0) return;
  if (!hasPending() || !readyToUpload()) return;
  startBackgroundUpload();
#else
  (void)readerOpen;
#endif
}

void stopAndWait() {
  if (!uploadRunning.load()) return;
  stopRequested.store(true);
  const unsigned long started = millis();
  while (uploadRunning.load() && millis() - started < STOP_WAIT_MS) delay(20);
  if (uploadRunning.load()) {
    radioHandedOver.store(true);
    LOG_ERR("KOAUTO", "Push task still in a request; handing the radio over");
  }
}

void deliverBeforeSleep() {
  stopAndWait();
  if (!hasPending() || !readyToUpload()) return;
  stopRequested.store(false);
  if (WiFi.status() != WL_CONNECTED) {
    const auto network = WIFI_STORE.findCredential(WIFI_STORE.getLastConnectedSsid());
    if (!network || !joinNetwork(*network)) {
      LOG_DBG("KOAUTO", "No network before sleep; progress stays queued");
      return;
    }
  }
  uploadQueue(millis() + SLEEP_UPLOAD_BUDGET_MS);
}

}  // namespace koreaderautosync
