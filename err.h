#pragma once

/**
 * File kindly provided by the university.
 */

/**
 * Outputs information about a fualty termination of
 * a system function and exits the program.
 */
extern void syserr(const char* fmt, ...);

/**
 * Outputs information about an error and exits the program.
 */
extern void fatal(const char* fmt, ...);