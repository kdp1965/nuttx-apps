/****************************************************************************
 * apps/examples/banktest/bigtest_main.c
 *
 * banktest built again as "bigtest" with a 4 KB stack (the Makefile): on
 * the LISA task banks a stack that big gets a bank of its own and the
 * task's heap runs up to it, 24 KB less the stack.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include "banktest_main.c"
