/* MINIX3 */

/*
 * Compatibility jump table for renamed symbols.
 *
 * DO NOT EDIT: this file is automatically generated.
 */
.global login_getpwclass;
.global __login_getpwclass50;
login_getpwclass: jmp __login_getpwclass50
