/* MINIX3 */

/*
 * Compatibility jump table for renamed symbols.
 *
 * DO NOT EDIT: this file is automatically generated.
 */
.global sigpending;
.global __sigpending14;
sigpending: jmp __sigpending14
