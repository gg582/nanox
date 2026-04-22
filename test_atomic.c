#include <stdatomic.h>
struct c_pair { int x; int y; };
_Atomic struct c_pair cursor;
int main() {
    cursor.x = 1;
    return cursor.x;
}
