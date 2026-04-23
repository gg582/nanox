#ifndef CUSTOM_TYPES_H_
#define CUSTOM_TYPES_H_

#include "estruct.h"

void custom_types_mark_dirty(struct buffer *bp);
void custom_types_ensure(struct buffer *bp);
int custom_types_count(void);
const char *custom_types_get(int index);
int custom_types_contains(const char *word);

#endif /* CUSTOM_TYPES_H_ */
