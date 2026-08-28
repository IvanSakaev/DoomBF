#ifndef IBF_CONFIG_SELECT_H
#define IBF_CONFIG_SELECT_H

/*
 * GNU Make keeps using evaluator/config.h exactly as before.
 * CMake supplies a generated header through IBF_CONFIG_HEADER so that the
 * portable Windows build can disable GNU Lightning/JIT without changing the
 * Linux Make configuration.
 */
#ifdef IBF_CONFIG_HEADER
#include IBF_CONFIG_HEADER
#else
#include "config.h"
#endif

#endif
