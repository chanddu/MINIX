/* MINIX3 */

/*
 * Compatibility jump table for renamed symbols.
 *
 * DO NOT EDIT: this file is automatically generated.
 */
.global setusercontext;
.global __setusercontext50;
setusercontext: jmp __setusercontext50
