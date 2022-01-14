#pragma once

#include <stdbool.h>

#include "Semaphore.h"

#define PROTOCOL_DEBUG 0

typedef struct NodeMonitor {
	int reading, writing, waitingR, waitingW; // waiting for R(eading), W(riting).
	Semaphore mutex, entryMutex; // pthread_mutex_t does not allow semaphore inheritance.
	Semaphore readers, writers;
} NodeMonitor;

// `Readers` are threads executing the `list` and `find` operations.
// `Writers` are threads executing the `create`, `remove`, and `move` operations.

// Init and Destroy return 0 if and only if they succeed.
// Other functions cannot fail for reasons other than system errors, which leave the
// protocols in an unrecoverable state, thus terminating the program with a fatal error.

int nmInit(NodeMonitor * nm);

int nmDestroy(NodeMonitor * nm);

void nmReaderEnter(NodeMonitor * nm);

void nmReaderExit(NodeMonitor * nm);

void nmWriterEnter(NodeMonitor * nm);

void nmWriterExit(NodeMonitor * nm);

// Locks the node to prevent threads from gaining access to it just after a move operation,
// before all threads from before the move have exited. In short -- disables entry protocols.
void nmLock(NodeMonitor * nm);

// Unlocks the node and lets the protocols continue as normal.
void nmUnlock(NodeMonitor * nm);