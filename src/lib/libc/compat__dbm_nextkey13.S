/* MINIX3 */

/*
 * Compatibility jump table for renamed symbols.
 *
 * DO NOT EDIT: this file is automatically generated.
 */
.global dbm_nextkey;
.global __dbm_nextkey13;
dbm_nextkey: jmp __dbm_nextkey13
