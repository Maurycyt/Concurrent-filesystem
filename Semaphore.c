#include <pthread.h>
#include <errno.h>

#include "err.h"

#include "Semaphore.h"

int semInit (Semaphore * s, int permits) {
	if (s == NULL) {
		return 0;
	}

	int err;
	s->permits = permits;
	s->waiting = 0;
	if ((err = pthread_mutex_init(&s->mutex, 0)) != 0) {
		errno = err;
		return errno;
	}
	if ((err = pthread_cond_init(&s->forPermit, 0)) != 0) {
		pthread_mutex_destroy(&s->mutex);
		errno = err;
		return errno;
	}

	return 0;
}

int semDestroy (Semaphore * s) {
	if (s == NULL) {
		return 0;
	}

	int err;
	if ((err = pthread_mutex_destroy(&s->mutex)) != 0) {
		pthread_cond_destroy(&s->forPermit);
		errno = err;
		return errno;
	}
	if ((err = pthread_cond_destroy(&s->forPermit)) != 0) {
		errno = err;
		return errno;
	}

	return 0;
}

void semP (Semaphore * s) {
	int err;
	if ((err = pthread_mutex_lock(&s->mutex)) != 0) {
		syserr("semP mutex lock %d", err);
	}

	// If there are at least as many threads waiting, as there are permits, then wait.
	// This is to prevent "barging in".
	if (s->permits <= s->waiting) {
		s->waiting++;
		do {
			if ((err = pthread_cond_wait(&s->forPermit, &s->mutex)) != 0) {
				syserr("semP cond wait %d", err);
			}
		} while (s->permits == 0);
		s->waiting--;
	}

	s->permits--;
	if ((err = pthread_mutex_unlock(&s->mutex)) != 0) {
		syserr("semP mutex unlock %d", err);
	}
}

void semV (Semaphore * s) {
	int err;
	if ((err = pthread_mutex_lock(&s->mutex)) != 0) {
		syserr("semV mutex lock %d", err);
	}

	s->permits++;

	if ((err = pthread_cond_signal(&s->forPermit)) != 0) {
		syserr("semV cond signal %d", err);
	}
	if ((err = pthread_mutex_unlock(&s->mutex)) != 0) {
		syserr("semV mutex unlock %d", err);
	}
}