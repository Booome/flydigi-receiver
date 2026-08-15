#include <stdio.h>
#include "sle_manager.h"

int main(void)
{
    printf("BS21 T-Node (Announce)\n");
    sle_init();
    sle_start_announce();
    while (1) {
        // SLE stack event loop
    }
    return 0;
}