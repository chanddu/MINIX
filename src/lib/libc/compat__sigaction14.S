/* MINIX3 */

/*
 * Compatibility jump table for renamed symbols.
 *
 * DO NOT EDIT: this file is automatically generated.
 */
.global sigaction;
.global __sigaction14;
sigaction: jmp __sigaction14
