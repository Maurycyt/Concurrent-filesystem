// Long live semaphore inheritance!

#pragma once

#include <pthread.h>

typedef struct Semaphore {
	int permits, waiting;
	pthread_mutex_t mutex;
	pthread_cond_t forPermit;
} Semaphore;

// Init and Destroy functions return 0 if and only if they succeed.
// Failure of the other functions result in the total collapse of the known universe
// and the universe beyond what is known, including the protocols, which are left
// in an unrecoverable state (indeed, they collapse with everything else into a singularity). And really, who are we, if not tiny specs of dust in the grand scheme of things, living on an insignificant blue marble, which we so unimaginatively called "Earth". We ask these questions, thirsty to know, but knowing, is something that is very particular to the soul; to know, is to understand the question is already answered in its asking. Thank you for coming to my TED-Talk. This is funnier with word-wrap disabled.
// That is, if they fail, `semP` and `semV` throw a fatal error and terminate the program.

// Initialize the semaphore.
int semInit (Semaphore * s, int permits);

// Destroy the semaphore.
int semDestroy (Semaphore * s);

// Acquire a permit.
void semP (Semaphore * s);

// Release a permit.
void semV (Semaphore * s);