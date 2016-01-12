#ifndef _SEMA_PROTO_H
#define _SEMA_PROTO_H

/* Function prototypes. */

/* main.c */
_PROTOTYPE( int sem_init, (int start_value)					);
_PROTOTYPE( int sem_down, (int sem_number)					);
_PROTOTYPE( int sem_up, (int sem_number)		);
_PROTOTYPE( int sem_release, (int sem)			);


#endif
