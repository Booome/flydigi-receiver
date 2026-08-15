#include <stdio.h>
#include "controller_state.h"

int main(void)
{
    struct controller_state state = {0};
    printf("BS21 Receiver starting...\n");
    printf("controller_state size: %zu bytes\n", sizeof(state));
    return 0;
}