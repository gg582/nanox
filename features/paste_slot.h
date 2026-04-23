/*
 * paste_slot.h - Paste preview/edit window header
 */

#ifndef PASTE_SLOT_H_
#define PASTE_SLOT_H_

/* Initialize paste slot buffer */
void paste_slot_init(void);

/* Add character to paste slot buffer */
int paste_slot_add_char(char c);

/* Clear paste slot buffer */
void paste_slot_clear(void);

/* Free paste slot buffer */
void paste_slot_free(void);

/* Get paste slot content */
char *paste_slot_get_content(void);

/* Get paste slot size */
int paste_slot_get_size(void);

/* Set paste slot active state */
void paste_slot_set_active(int active);

/* Check if paste slot is active */
int paste_slot_is_active(void);

/* Display paste slot window */
void paste_slot_display(void);

/* Insert paste slot content into document */
int paste_slot_insert(void);

#endif /* PASTE_SLOT_H_ */
