#pragma once

#include <ProgressQueue.h>

// Automatic KOReader progress push. The local position always wins: closing a
// book (or putting the reader to sleep) queues its latest position, and every
// queued book is uploaded the next time the network can be reached.
//
// Delivery paths:
//   * Background (boards with PSRAM): a short-lived task joins the last WiFi
//     network after a book is closed, uploads the queue and turns the radio
//     off, while the UI keeps running. Boards without PSRAM skip it: a WiFi
//     session leaves the internal heap fragmented, and they only use WiFi
//     before a reboot.
//   * Sleep: once the book is closed and the sleep screen is up, the queue is
//     delivered within a bounded time budget before deep sleep.
// Anything not delivered stays queued on the SD card for the next attempt.
namespace koreaderautosync {

// Loads the queue from /.crosspoint/koreader_queue.json. Call once at boot.
void begin();

// Queues a book's latest position, replacing an older one for the same
// document. Ignored when no KOReader account is configured.
void enqueue(koreader::QueuedProgress entry);

bool hasPending();

// Main loop: starts a background upload when work, an account, a saved WiFi
// network, an idle radio and enough heap are all available. Never blocks.
void poll(bool readerOpen);

// Called before any foreground WiFi use and before sleep: asks a running
// upload to stop after its current request and waits for the radio.
void stopAndWait();

// Sleep path, main task, book already closed: bounded synchronous delivery.
void deliverBeforeSleep();

}  // namespace koreaderautosync
