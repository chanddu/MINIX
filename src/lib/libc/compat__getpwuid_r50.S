/* MINIX3 */

/*
 * Compatibility jump table for renamed symbols.
 *
 * DO NOT EDIT: this file is automatically generated.
 */
.global getpwuid_r;
.global __getpwuid_r50;
getpwuid_r: jmp __getpwuid_r50
