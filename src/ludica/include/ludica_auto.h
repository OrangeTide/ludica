#ifndef LUDICA_AUTO_H_
#define LUDICA_AUTO_H_

/* Register app state for automation QUERY VAR / LISTVAR commands.
 * Pointers must remain valid until shutdown. */
void lud_auto_register_int(const char *name, const int *ptr);
void lud_auto_register_str(const char *name, const char *const *ptr);

#endif /* LUDICA_AUTO_H_ */
