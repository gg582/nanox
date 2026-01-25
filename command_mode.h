/* command_mode.h - F1 command mode declarations */

#ifndef COMMAND_MODE_H
#define COMMAND_MODE_H

/* Initialize command mode system */
void command_mode_init(void);

/* Check if command mode is active */
int command_mode_is_active(void);

/* Activate F1 command mode */
void command_mode_activate(void);

/* Handle key input in command mode */
int command_mode_handle_key(int c);

/* Render command mode UI (status bar) */
void command_mode_render(void);

/* Cleanup command mode */
void command_mode_cleanup(void);

/* F6 Sed Replace command */
int sed_replace_command(int f, int n);

#endif /* COMMAND_MODE_H */
