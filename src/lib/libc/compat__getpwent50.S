/* MINIX3 */

/*
 * Compatibility jump table for renamed symbols.
 *
 * DO NOT EDIT: this file is automatically generated.
 */
.global getpwent;
.global __getpwent50;
getpwent: jmp __getpwent50
